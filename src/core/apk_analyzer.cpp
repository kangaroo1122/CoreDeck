// Analysis queries and ZIP layout support adapted from devmuaz/CoreDeck (56e7fa2).
#include "apk_analyzer.h"

#include <algorithm>
#include <stdexcept>
#include <chrono>
#include <filesystem>
#include <regex>
#include <sstream>
#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <unordered_map>
#include <map>
#include <utility>
#include <miniz.h>
#include "paths.h"

namespace CoreDeck {
    namespace {
        struct CommandResult {
            bool Ok = false;
            bool Cancelled = false;
            bool TimedOut = false;
            std::string Output;
        };

        CommandResult Run(const SdkInfo &sdk, const std::string &tool, const std::vector<std::string> &args,
                          int seconds, const std::function<bool()> &cancel) {
            CommandResult result;
            if (cancel && cancel()) {
                result.Cancelled = true;
                return result;
            }
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(seconds);
            result.Ok = StreamCommandArgsWithEnvCancelable(tool, args, "", BuildAndroidToolEnvironment(sdk),
                [&](const std::string &line) {
                    constexpr std::size_t limit = 1024 * 1024;
                    if (result.Output.size() < limit) {
                        result.Output.append(line + '\n', 0, limit - result.Output.size());
                    }
                }, [&] {
                    result.Cancelled = cancel && cancel();
                    result.TimedOut = std::chrono::steady_clock::now() >= deadline;
                    return result.Cancelled || result.TimedOut;
                });
            return result;
        }

        std::string FindAapt2(const SdkInfo &sdk) {
            const auto root = std::filesystem::path(sdk.SdkPath) / "build-tools";
            if (sdk.SdkPath.empty() || !std::filesystem::is_directory(root)) return {};
            std::vector<std::pair<std::vector<int>, std::string>> tools;
            const std::regex version(R"((\d+)\.(\d+)\.(\d+)(.*))");
            for (const auto &entry: std::filesystem::directory_iterator(root)) {
                const auto path = entry.path() / ("aapt2" + Paths::GetExecutableExtension());
                std::smatch match;
                const auto name = entry.path().filename().string();
                if (std::filesystem::is_regular_file(path) && std::regex_match(name, match, version)) {
                    tools.push_back({{std::stoi(match[1]), std::stoi(match[2]), std::stoi(match[3]), match[4].length() == 0 ? 1 : 0}, path.string()});
                }
            }
            std::ranges::sort(tools);
            return tools.empty() ? "" : tools.back().second;
        }

        std::string Attribute(const std::string &line, const std::string &key) {
            const auto start = line.find(key + "='");
            if (start == std::string::npos) return {};
            const auto value = start + key.size() + 2;
            const auto end = line.find('\'', value);
            return end == std::string::npos ? "" : line.substr(value, end - value);
        }

        std::string QuotedValue(const std::string &line) {
            const auto start = line.find('\'');
            const auto end = line.rfind('\'');
            return start == std::string::npos || end <= start ? "" : line.substr(start + 1, end - start - 1);
        }
    }

    ApkReport ParseApkBadging(const std::string &output) {
        ApkReport report;
        std::istringstream lines(output);
        std::string line;
        while (std::getline(lines, line)) {
            if (line.starts_with("package:")) {
                report.PackageName = Attribute(line, "name");
                report.VersionName = Attribute(line, "versionName");
                report.VersionCode = Attribute(line, "versionCode");
            } else if (line.starts_with("application-label:")) {
                report.Label = QuotedValue(line);
            } else if (line.starts_with("sdkVersion:") || line.starts_with("minSdkVersion:")) {
                report.MinSdk = QuotedValue(line);
            } else if (line.starts_with("targetSdkVersion:")) {
                report.TargetSdk = QuotedValue(line);
            } else if (line.starts_with("uses-permission:") || line.starts_with("uses-permission-sdk-23:")) {
                auto name = Attribute(line, "name");
                if (!name.empty() && std::ranges::find(report.Permissions, name) == report.Permissions.end()) {
                    report.Permissions.push_back(std::move(name));
                }
            } else if (line.starts_with("native-code:")) {
                const std::regex quoted("'([^']+)'");
                for (auto it = std::sregex_iterator(line.begin(), line.end(), quoted); it != std::sregex_iterator(); ++it) {
                    report.Abis.push_back((*it)[1]);
                }
            }
        }
        if (report.PackageName.empty()) report.Error = "Could not read APK metadata.";
        return report;
    }

    ApkReport AnalyzeApk(const SdkInfo &sdk, const std::string &path, const std::function<bool()> &cancel,
                         const std::function<void(float)> &onProgress) {
        ApkReport report;
        report.Path = path;
        try {
            if (cancel && cancel()) { report.Cancelled = true; return report; }
            if (!std::filesystem::is_regular_file(path)) {
                report.Error = "Select an existing APK file.";
                return report;
            }
            auto progress = onProgress;
            report = LoadApkReport(sdk, std::filesystem::absolute(path).string(),
                [](void *user, float fraction, int) {
                    const auto &callback = *static_cast<std::function<void(float)> *>(user);
                    if (callback) callback(fraction * 0.85F);
                    return true;
                }, &progress, cancel);
            if (report.ToolMissing) {
                report.Error = "Install Android SDK Command-line Tools in SDK settings to analyze APKs.";
                return report;
            }
            if (report.Cancelled) return report;
            if (report.Summary.Ok) {
                report.PackageName = report.Summary.Value.ApplicationId;
                report.VersionName = report.Summary.Value.VersionName;
                report.VersionCode = report.Summary.Value.VersionCode;
            }
            // Optional Build-Tools enrichment preserves our metadata view.
            const auto tool = FindAapt2(sdk);
            bool metadataRead = false;
            if (!tool.empty()) {
                const auto command = Run(sdk, tool, {"dump", "badging", report.Path}, 30, cancel);
                if (command.Ok) {
                    auto metadata = ParseApkBadging(command.Output);
                    metadataRead = metadata.Error.empty();
                    report.Label = std::move(metadata.Label);
                    report.MinSdk = std::move(metadata.MinSdk);
                    report.TargetSdk = std::move(metadata.TargetSdk);
                    report.Abis = std::move(metadata.Abis);
                    report.Permissions = std::move(metadata.Permissions);
                }
            }
            if (!metadataRead && !(cancel && cancel())) {
                // These fields remain available even when Build-Tools is absent.
                auto readManifest = [&](const char *field) {
                    const auto result = Run(sdk, sdk.ApkAnalyzerPath, {"manifest", field, report.Path}, 30, cancel);
                    return result.Ok ? result.Output : std::string{};
                };
                auto firstLine = [](std::string text) {
                    const auto end = text.find_first_of("\r\n");
                    if (end != std::string::npos) text.resize(end);
                    return text;
                };
                report.MinSdk = firstLine(readManifest("min-sdk"));
                report.TargetSdk = firstLine(readManifest("target-sdk"));
                std::istringstream permissions(readManifest("permissions"));
                std::string permission;
                while (std::getline(permissions, permission)) {
                    if (!permission.empty() && permission.back() == '\r') permission.pop_back();
                    if (!permission.empty()) report.Permissions.push_back(permission);
                }
                for (const auto &file: report.Files.Value) {
                    if (!file.Path.starts_with("/lib/")) continue;
                    const auto end = file.Path.find('/', 5);
                    if (end == std::string::npos) continue;
                    const auto abi = file.Path.substr(5, end - 5);
                    if (!abi.empty() && std::ranges::find(report.Abis, abi) == report.Abis.end()) report.Abis.push_back(abi);
                }
            }
            report.Cancelled = cancel && cancel();
            if (!report.Summary.Ok && !report.Files.Ok) {
                report.Error = "Could not analyze APK.";
                report.Details = !report.Summary.Ok ? report.Summary.Output : report.Files.Output;
            }
            if (onProgress) onProgress(1.0F);
        } catch (const std::exception &e) {
            report.Cancelled = cancel && cancel();
            if (!report.Cancelled) { report.Error = "Could not analyze APK."; report.Details = e.what(); }
        }
        return report;
    }

    ApkInstallResult InstallApk(const SdkInfo &sdk, const std::string &serial, const std::string &path,
                                const std::function<bool()> &cancel) {
        if (sdk.AdbPath.empty() || serial.empty() || !std::filesystem::is_regular_file(path)) {
            return {.Error = "Select a running emulator and an existing APK file."};
        }
        const auto command = Run(sdk, sdk.AdbPath, {"-s", serial, "install", "-r", std::filesystem::absolute(path).string()}, 180, cancel);
        std::istringstream lines(command.Output);
        std::string line;
        bool success = false;
        while (std::getline(lines, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line == "Success") success = true;
        }
        const bool ok = command.Ok && success;
        return {.Succeeded = ok, .Cancelled = command.Cancelled,
                .Error = ok || command.Cancelled ? "" : command.TimedOut ? "APK installation timed out." : "Could not install APK.",
                .Details = command.Output};
    }
    namespace {
        std::string TrimLine(std::string line) {
            while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\t')) {
                line.pop_back();
            }
            return line;
        }

        bool IsWarningLine(const std::string &line) {
            return line.starts_with("Warning:") || line.starts_with("WARNING:");
        }

        bool IsFailureLine(const std::string &line) {
            return line.starts_with("Exception in thread") || line.starts_with("ERROR:") || line.starts_with("Error:");
        }

        bool ApkAnalyzerFailed(const std::string &output) {
            std::istringstream stream(output);
            std::string line;
            while (std::getline(stream, line)) {
                line = TrimLine(std::move(line));
                if (IsFailureLine(line)) {
                    return true;
                }
            }
            return false;
        }

        std::optional<std::string> FirstContentLine(const std::string &output) {
            std::istringstream stream(output);
            std::string line;
            while (std::getline(stream, line)) {
                line = TrimLine(std::move(line));
                if (line.empty() || IsWarningLine(line)) {
                    continue;
                }
                return line;
            }
            return std::nullopt;
        }

        std::optional<std::int64_t> ParseByteToken(const std::string &token) {
            if (token.empty()) {
                return std::nullopt;
            }
            errno = 0;
            char *end = nullptr;
            const int64_t value = std::strtoll(token.c_str(), &end, 10);
            if (end == token.c_str() || end == nullptr || *end != '\0' || errno == ERANGE) {
                return std::nullopt;
            }
            return static_cast<std::int64_t>(value);
        }

        std::optional<std::vector<std::string>> SplitTabs(const std::string &line, const std::size_t count) {
            std::vector<std::string> parts;
            parts.reserve(count);
            std::size_t start = 0;
            for (std::size_t i = 0; i + 1 < count; ++i) {
                const auto tab = line.find('\t', start);
                if (tab == std::string::npos) {
                    return std::nullopt;
                }
                parts.push_back(line.substr(start, tab - start));
                start = tab + 1;
            }
            parts.push_back(line.substr(start));
            return parts;
        }

        template<typename T, typename Parse>
        ApkAnalyzerQuery<T> RunQuery(const SdkInfo &sdk, const std::vector<std::string> &args, Parse parse, const std::function<bool()> &cancel) {
            ApkAnalyzerQuery<T> result;
            std::optional<std::string> output;
            try {
                output = RunApkAnalyzer(sdk, args, cancel);
            } catch (const std::exception &e) {
                result.Output = e.what();
                return result;
            }
            if (!output) {
                result.ToolMissing = true;
                return result;
            }
            result.Output = *output;
            if (ApkAnalyzerFailed(result.Output)) {
                return result;
            }
            if (auto parsed = parse(result.Output)) {
                result.Ok = true;
                result.Value = std::move(*parsed);
            }
            return result;
        }

        std::vector<std::string> ApkArgs(const std::string &verb, const std::string &apkPath) {
            return {"apk", verb, apkPath};
        }

        std::vector<std::string> ManifestArgs(const std::string &verb, const std::string &apkPath) {
            return {"manifest", verb, apkPath};
        }
    }

    std::optional<std::string> RunApkAnalyzer(const SdkInfo &sdk, const std::vector<std::string> &args, const std::function<bool()> &cancel) {
        if (sdk.ApkAnalyzerPath.empty()) {
            return std::nullopt;
        }
        if (cancel && cancel()) throw std::runtime_error("APK analysis cancelled.");
        std::string output;
        bool truncated = false;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(120);
        const bool ok = StreamCommandArgsWithEnvCancelable(sdk.ApkAnalyzerPath, args, "", BuildAndroidToolEnvironment(sdk),
            [&](const std::string &line) {
                constexpr size_t limit = 32 * 1024 * 1024;
                if (output.size() + line.size() + 1 > limit) truncated = true;
                else { output += line; output += '\n'; }
            }, [&] { return truncated || (cancel && cancel()) || std::chrono::steady_clock::now() >= deadline; });
        if (!ok || truncated) {
            throw std::runtime_error(truncated ? "APK analyzer output exceeded the limit." :
                (cancel && cancel()) ? "APK analysis cancelled." :
                "APK analyzer command failed or timed out.\n" + output);
        }
        return output;
    }

    std::optional<ApkSummary> ParseApkSummary(const std::string &output) {
        const auto line = FirstContentLine(output);
        if (!line) {
            return std::nullopt;
        }
        const auto parts = SplitTabs(*line, 3);
        if (!parts || parts->at(0).empty() || parts->at(1).empty()) {
            return std::nullopt;
        }
        ApkSummary summary;
        summary.ApplicationId = parts->at(0);
        summary.VersionCode = parts->at(1);
        summary.VersionName = parts->at(2);
        return summary;
    }

    std::optional<std::int64_t> ParseApkByteSize(const std::string &output) {
        const auto line = FirstContentLine(output);
        if (!line) {
            return std::nullopt;
        }
        return ParseByteToken(*line);
    }

    std::optional<bool> ParseApkDebuggable(const std::string &output) {
        const auto line = FirstContentLine(output);
        if (!line) {
            return std::nullopt;
        }
        if (*line == "true") {
            return true;
        }
        if (*line == "false") {
            return false;
        }
        return std::nullopt;
    }

    std::optional<std::vector<ApkFileEntry>> ParseApkFileList(const std::string &output) {
        std::vector<ApkFileEntry> entries;
        std::istringstream stream(output);
        std::string line;
        while (std::getline(stream, line)) {
            line = TrimLine(std::move(line));
            if (line.empty() || IsWarningLine(line)) {
                continue;
            }
            const auto parts = SplitTabs(line, 3);
            if (!parts || parts->at(2).empty()) {
                return std::nullopt;
            }
            const auto raw = ParseByteToken(parts->at(0));
            const auto download = ParseByteToken(parts->at(1));
            if (!raw || !download) {
                return std::nullopt;
            }
            ApkFileEntry entry;
            entry.RawSize = *raw;
            entry.DownloadSize = *download;
            entry.Path = parts->at(2);
            entries.push_back(std::move(entry));
        }
        return entries;
    }

    std::optional<std::vector<ApkCompareEntry>> ParseApkCompare(const std::string &output) {
        std::vector<ApkCompareEntry> entries;
        std::istringstream stream(output);
        std::string line;
        while (std::getline(stream, line)) {
            line = TrimLine(std::move(line));
            if (line.empty() || IsWarningLine(line)) {
                continue;
            }
            const auto parts = SplitTabs(line, 4);
            if (!parts || parts->at(3).empty()) {
                return std::nullopt;
            }
            const auto oldSize = ParseByteToken(parts->at(0));
            const auto newSize = ParseByteToken(parts->at(1));
            const auto difference = ParseByteToken(parts->at(2));
            if (!oldSize || !newSize || !difference) {
                return std::nullopt;
            }
            ApkCompareEntry entry;
            entry.OldSize = *oldSize;
            entry.NewSize = *newSize;
            entry.Difference = *difference;
            entry.Path = parts->at(3);
            entries.push_back(std::move(entry));
        }
        return entries;
    }

    ApkAnalyzerQuery<ApkSummary> QueryApkSummary(const SdkInfo &sdk, const std::string &apkPath, const std::function<bool()> &cancel) {
        if (apkPath.empty()) {
            return {};
        }
        return RunQuery<ApkSummary>(sdk, ApkArgs("summary", apkPath), ParseApkSummary, cancel);
    }

    ApkAnalyzerQuery<std::int64_t> QueryApkFileSize(const SdkInfo &sdk, const std::string &apkPath, const std::function<bool()> &cancel) {
        if (apkPath.empty()) {
            return {};
        }
        return RunQuery<std::int64_t>(sdk, ApkArgs("file-size", apkPath), ParseApkByteSize, cancel);
    }

    ApkAnalyzerQuery<std::int64_t> QueryApkDownloadSize(const SdkInfo &sdk, const std::string &apkPath, const std::function<bool()> &cancel) {
        if (apkPath.empty()) {
            return {};
        }
        return RunQuery<std::int64_t>(sdk, ApkArgs("download-size", apkPath), ParseApkByteSize, cancel);
    }

    ApkAnalyzerQuery<std::vector<ApkCompareEntry>> QueryApkCompare(
        const SdkInfo &sdk,
        const std::string &baselineApkPath,
        const std::string &apkPath,
        const std::function<bool()> &cancel
    ) {
        if (baselineApkPath.empty() || apkPath.empty()) {
            return {};
        }
        if (cancel && cancel()) return {};
        // Some apkanalyzer versions fail with "ERROR: null" when both paths
        // refer to the same file. There can be no size differences in that case.
        std::error_code ec;
        if (std::filesystem::equivalent(baselineApkPath, apkPath, ec) && !ec) {
            return {.Ok = true};
        }
        return RunQuery<std::vector<ApkCompareEntry>>(
            sdk,
            {"apk", "compare", "--different-only", baselineApkPath, apkPath},
            ParseApkCompare, cancel
        );
    }

    ApkAnalyzerQuery<bool> QueryApkDebuggable(const SdkInfo &sdk, const std::string &apkPath, const std::function<bool()> &cancel) {
        if (apkPath.empty()) {
            return {};
        }
        return RunQuery<bool>(sdk, ManifestArgs("debuggable", apkPath), ParseApkDebuggable, cancel);
    }

    ApkAnalyzerQuery<std::vector<ApkFileEntry>> QueryApkFiles(const SdkInfo &sdk, const std::string &apkPath, const std::function<bool()> &cancel) {
        if (apkPath.empty()) {
            return {};
        }
        auto result = RunQuery<std::vector<ApkFileEntry>>(
            sdk,
            {"files", "list", "--raw-size", "--download-size", apkPath},
            ParseApkFileList, cancel
        );
        if (result.Ok || (cancel && cancel())) return result;

        // Read only ZIP headers: nested archives remain ordinary files. Their
        // contents need not be mounted through Java's ZIP filesystem provider.
        mz_zip_archive zip{};
        if (!mz_zip_reader_init_file(&zip, apkPath.c_str(), 0)) return result;
        struct ZipCloser {
            mz_zip_archive *Zip;
            ~ZipCloser() { mz_zip_reader_end(Zip); }
        } closer{&zip};
        std::map<std::string, ApkFileEntry> entries;
        entries["/"].Path = "/";
        for (mz_uint i = 0; i < mz_zip_reader_get_num_files(&zip); ++i) {
            if (cancel && cancel()) return result;
            mz_zip_archive_file_stat stat{};
            if (!mz_zip_reader_file_stat(&zip, i, &stat)) return result;
            const auto length = mz_zip_reader_get_filename(&zip, i, nullptr, 0);
            if (!length) return result;
            std::string name(length, '\0');
            mz_zip_reader_get_filename(&zip, i, name.data(), length);
            name.resize(length - 1);
            if (name.empty()) continue;
            if (name.front() != '/') name.insert(name.begin(), '/');
            if (stat.m_is_directory) {
                if (name.back() != '/') name += '/';
                entries[name].Path = name;
                continue;
            }
            auto &entry = entries[name];
            entry.Path = name;
            entry.RawSize = static_cast<std::int64_t>(stat.m_uncomp_size);
            entry.DownloadSize = static_cast<std::int64_t>(stat.m_comp_size);
            entry.Stored = stat.m_method == 0;
            for (size_t slash = 0; slash != std::string::npos; slash = name.find('/', slash + 1)) {
                const auto directory = name.substr(0, slash + 1);
                auto &parent = entries[directory];
                parent.Path = directory;
                parent.RawSize += entry.RawSize;
                parent.DownloadSize += entry.DownloadSize;
            }
        }
        result.Value.clear();
        for (auto &[path, entry]: entries) result.Value.push_back(std::move(entry));
        result.Ok = true;
        result.ToolMissing = false;
        result.FromZip = true;
        return result;
    }

    std::uint32_t ZipDataAlignment(const std::uint64_t dataOffset) {
        if (dataOffset == 0) {
            return 0;
        }
        std::uint32_t alignment = 1;
        std::uint64_t value = dataOffset;
        while ((value % 2) == 0 && alignment < 16384U) {
            value /= 2;
            alignment *= 2;
        }
        return alignment;
    }

    namespace {
        std::string ZipLookupKey(std::string path) {
            for (char &character: path) {
                if (character == '\\') {
                    character = '/';
                }
            }
            while (path.starts_with("./")) {
                path.erase(0, 2);
            }
            if (!path.empty() && path.front() != '/') {
                path.insert(path.begin(), '/');
            }
            return path;
        }

        bool ReadLocalExtraLength(std::ifstream &file, const std::uint64_t localOffset, std::uint16_t &extraLength) {
            file.clear();
            file.seekg(static_cast<std::streamoff>(localOffset + 28));
            unsigned char bytes[2] = {};
            file.read(reinterpret_cast<char *>(bytes), 2);
            if (!file) {
                return false;
            }
            extraLength = static_cast<std::uint16_t>(bytes[0] | (bytes[1] << 8));
            return true;
        }
    }

    void ApplyApkZipAlignment(std::vector<ApkFileEntry> &entries, const std::string &apkPath, const std::function<bool()> &cancel) {
        if (entries.empty() || apkPath.empty()) {
            return;
        }

        mz_zip_archive zip = {};
        if (!mz_zip_reader_init_file(&zip, apkPath.c_str(), 0)) {
            return;
        }

        std::ifstream file(apkPath, std::ios::binary);
        std::unordered_map<std::string, std::pair<std::uint32_t, bool>> layouts;
        const mz_uint count = mz_zip_reader_get_num_files(&zip);
        layouts.reserve(count);
        for (mz_uint index = 0; index < count; ++index) {
            if (cancel && cancel()) break;
            mz_zip_archive_file_stat stat = {};
            if (!mz_zip_reader_file_stat(&zip, index, &stat) || stat.m_is_directory) {
                continue;
            }
            std::uint16_t extraLength = 0;
            if (!ReadLocalExtraLength(file, stat.m_local_header_ofs, extraLength)) {
                continue;
            }
            const std::uint64_t dataOffset = stat.m_local_header_ofs + 30 + std::char_traits<char>::length(stat.m_filename) + extraLength;
            layouts.emplace(ZipLookupKey(stat.m_filename), std::pair{ZipDataAlignment(dataOffset), stat.m_method == 0});
        }
        mz_zip_reader_end(&zip);

        for (auto &entry: entries) {
            const auto found = layouts.find(ZipLookupKey(entry.Path));
            if (found == layouts.end()) {
                continue;
            }
            entry.Alignment = found->second.first;
            entry.Stored = found->second.second;
        }
    }

    namespace {
        bool AdvanceApkLoad(const ApkLoadProgressFn onProgress, void *user, const float progress, const int stage) {
            return onProgress == nullptr || onProgress(user, progress, stage);
        }
    }

    ApkReport LoadApkReport(
        const SdkInfo &sdk,
        const std::string &apkPath,
        const ApkLoadProgressFn onProgress,
        void *progressUser,
        const std::function<bool()> &cancel
    ) {
        ApkReport report;
        report.Path = apkPath;
        if (sdk.ApkAnalyzerPath.empty() || !std::filesystem::exists(sdk.ApkAnalyzerPath)) {
            report.ToolMissing = true;
            return report;
        }
        if (apkPath.empty()) {
            return report;
        }

        const auto step = [&](const float progress, const int stage) {
            if (!(cancel && cancel()) && AdvanceApkLoad(onProgress, progressUser, progress, stage)) {
                return true;
            }
            report.Cancelled = true;
            return false;
        };

        if (!step(0.02F, 1)) {
            return report;
        }
        report.Summary = QueryApkSummary(sdk, apkPath, cancel);
        if (!step(0.08F, 1)) {
            return report;
        }
        report.FileSize = QueryApkFileSize(sdk, apkPath, cancel);
        if (!step(0.14F, 1)) {
            return report;
        }
        report.DownloadSize = QueryApkDownloadSize(sdk, apkPath, cancel);
        if (!step(0.55F, 1)) {
            return report;
        }
        report.Debuggable = QueryApkDebuggable(sdk, apkPath, cancel);

        if (!step(0.75F, 2)) {
            return report;
        }
        report.Files = QueryApkFiles(sdk, apkPath, cancel);
        if (report.Files.Ok) {
            ApplyApkZipAlignment(report.Files.Value, apkPath, cancel);
        }
        step(1.0F, 2);
        return report;
    }

    ApkAnalyzerQuery<std::vector<ApkCompareEntry>> LoadApkCompare(
        const SdkInfo &sdk,
        const std::string &baselineApkPath,
        const std::string &apkPath,
        const std::function<bool()> &cancel
    ) {
        return QueryApkCompare(sdk, baselineApkPath, apkPath, cancel);
    }
}
