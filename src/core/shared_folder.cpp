#include "shared_folder.h"

#include <cerrno>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <filesystem>
#include <map>
#include <set>
#include <sstream>
#include <string_view>
#include <system_error>
#include <utility>

#include "adb.h"
#include "device_file.h"
#include "paths.h"
#include "process.h"
#include "utilities.h"

namespace CoreDeck {
    namespace {
        constexpr const char *UNSUPPORTED_SYMLINK_ERROR = "Symbolic links are not supported in shared folder.";

        std::string TrimCopy(const std::string &value) {
            const auto start = value.find_first_not_of(" \t\r\n");
            if (start == std::string::npos) {
                return "";
            }
            const auto end = value.find_last_not_of(" \t\r\n");
            return value.substr(start, end - start + 1);
        }

        std::string ShellQuote(const std::string &value) {
            std::string quoted = "'";
            for (const char c: value) {
                if (c == '\'') {
                    quoted += "'\\''";
                } else {
                    quoted.push_back(c);
                }
            }
            quoted.push_back('\'');
            return quoted;
        }

        std::string SanitizeFolderName(const std::string &value) {
            std::string result;
            for (const unsigned char ch: TrimCopy(value)) {
                if (std::isalnum(ch) || ch == '-' || ch == '_' || ch == '.') {
                    result.push_back(static_cast<char>(ch));
                } else {
                    result.push_back('_');
                }
            }
            return result == "." || result == ".." ? "" : result;
        }

        SharedFolderSyncResult Failure(std::string message) {
            return {.Success = false, .Output = std::move(message)};
        }

        struct PullSharedFolderResult : SharedFolderSyncResult {
            std::filesystem::path StagingPath;
            std::string HostPath;
        };

        PullSharedFolderResult PullFailure(std::string message) {
            PullSharedFolderResult result;
            result.Success = false;
            result.Output = std::move(message);
            return result;
        }

        SharedFolderSyncResult RunSharedFolderAdbCommand(
            const SdkInfo &sdk,
            const std::vector<std::string> &args,
            const std::function<bool()> &shouldCancel
        ) {
            if (!HasAdb(sdk)) {
                return Failure("ADB was not found.");
            }

            std::string output;
            const bool ok = StreamCommandArgsWithEnvCancelable(
                sdk.AdbPath,
                args,
                "",
                BuildAndroidToolEnvironment(sdk),
                [&output](const std::string &line) {
                    output += line;
                    output.push_back('\n');
                },
                shouldCancel
            );
            return {.Success = ok, .Output = std::move(output)};
        }

        std::string BuildAvdStateFolderName(const std::string &avdName) {
            std::string folderName = SanitizeFolderName(avdName);
            if (folderName.empty()) {
                folderName = "default";
            }
            return folderName;
        }

        std::filesystem::path BuildStagingPath(const std::string &avdName) {
            const std::string folderName = BuildAvdStateFolderName(avdName);
            return Paths::JoinPaths({Paths::GetAppConfigPath("shared_staging"), folderName});
        }

        std::filesystem::path BuildSnapshotPath(const std::string &avdName) {
            return Paths::JoinPaths({Paths::GetAppConfigPath("shared_state"), BuildAvdStateFolderName(avdName) + ".txt"});
        }

        bool IsSafeSnapshotRelativePath(const std::string &relativePath) {
            if (relativePath.empty()) {
                return false;
            }

            for (const unsigned char ch: relativePath) {
                if (ch == '\t' || ch == '\r' || ch == '\n') {
                    return false;
                }
            }

            const std::filesystem::path path(relativePath);
            if (path.is_absolute() || path.has_root_name()) {
                return false;
            }

            for (const auto &part: path) {
                const std::string segment = part.string();
                if (segment.empty() || segment == "." || segment == "..") {
                    return false;
                }
            }
            return true;
        }

        struct FileState {
            std::uintmax_t SizeBytes = 0;
            std::int64_t ModifiedTick = 0;
            std::uint64_t Hash = 0;
            bool HasMetadata = false;
        };

        using FileStateMap = std::map<std::string, FileState>;

        struct DeviceMetadataResult : SharedFolderSyncResult {
            FileStateMap Files;
        };

        struct SharedFolderSnapshotEntry {
            bool HasHost = false;
            FileState Host;
            bool HasDevice = false;
            FileState Device;
        };

        using SharedFolderSnapshotMap = std::map<std::string, SharedFolderSnapshotEntry>;

        enum class SharedFolderSnapshotFormat {
            Missing,
            Legacy,
            V3,
        };

        struct SharedFolderSnapshot {
            SharedFolderSnapshotFormat Format = SharedFolderSnapshotFormat::Missing;
            SharedFolderSnapshotMap Entries;
        };

        bool ParseUintmax(const std::string &value, std::uintmax_t *out) {
            if (out == nullptr || value.empty()) {
                return false;
            }

            errno = 0;
            char *end = nullptr;
            const unsigned long long parsed = std::strtoull(value.c_str(), &end, 10);
            if (errno != 0 || end == value.c_str() || *end != '\0') {
                return false;
            }
            *out = static_cast<std::uintmax_t>(parsed);
            return true;
        }

        bool ParseInt64(const std::string &value, std::int64_t *out) {
            if (out == nullptr || value.empty()) {
                return false;
            }

            errno = 0;
            char *end = nullptr;
            const long long parsed = std::strtoll(value.c_str(), &end, 10);
            if (errno != 0 || end == value.c_str() || *end != '\0') {
                return false;
            }
            *out = static_cast<std::int64_t>(parsed);
            return true;
        }

        bool ParseUint64(const std::string &value, std::uint64_t *out) {
            if (out == nullptr || value.empty()) {
                return false;
            }

            errno = 0;
            char *end = nullptr;
            const unsigned long long parsed = std::strtoull(value.c_str(), &end, 10);
            if (errno != 0 || end == value.c_str() || *end != '\0') {
                return false;
            }
            *out = static_cast<std::uint64_t>(parsed);
            return true;
        }

        bool ParseSnapshotBool(const std::string &value, bool *out) {
            if (out == nullptr) {
                return false;
            }
            if (value == "1") {
                *out = true;
                return true;
            }
            if (value == "0") {
                *out = false;
                return true;
            }
            return false;
        }

        bool TakeTabField(const std::string &line, std::size_t *start, std::string *field) {
            if (start == nullptr || field == nullptr || *start > line.size()) {
                return false;
            }
            const auto end = line.find('\t', *start);
            if (end == std::string::npos) {
                return false;
            }
            *field = line.substr(*start, end - *start);
            *start = end + 1;
            return true;
        }

        bool HashFile(const std::filesystem::path &path, std::uint64_t *hash, std::string *error) {
            if (hash == nullptr) {
                return false;
            }

            std::ifstream file(path, std::ios::binary);
            if (!file.is_open()) {
                if (error != nullptr) {
                    *error = "Could not read shared folder file.";
                }
                return false;
            }

            std::uint64_t value = 14695981039346656037ULL;
            char buffer[65536] = {};
            while (file.read(buffer, sizeof(buffer)) || file.gcount() > 0) {
                for (std::streamsize i = 0; i < file.gcount(); i++) {
                    value ^= static_cast<unsigned char>(buffer[i]);
                    value *= 1099511628211ULL;
                }
            }
            if (file.bad()) {
                if (error != nullptr) {
                    *error = "Could not read shared folder file.";
                }
                return false;
            }

            *hash = value;
            return true;
        }

        bool CollectRelativeFileStates(
            const std::filesystem::path &rootPath,
            FileStateMap *files,
            std::string *error
        ) {
            if (files == nullptr) {
                return false;
            }
            files->clear();

            std::error_code ec;
            if (!std::filesystem::is_directory(rootPath, ec) || ec) {
                if (error != nullptr) {
                    *error = ec ? ec.message() : "Shared folder path is not a directory.";
                }
                return false;
            }

            std::filesystem::recursive_directory_iterator it(
                rootPath,
                std::filesystem::directory_options::skip_permission_denied,
                ec
            );
            if (ec) {
                if (error != nullptr) {
                    *error = ec.message();
                }
                return false;
            }

            const std::filesystem::recursive_directory_iterator end;
            while (it != end) {
                const auto status = it->symlink_status(ec);
                if (ec) {
                    if (error != nullptr) {
                        *error = ec.message();
                    }
                    return false;
                }
                if (std::filesystem::is_symlink(status)) {
                    if (error != nullptr) {
                        *error = UNSUPPORTED_SYMLINK_ERROR;
                    }
                    return false;
                }
                if (!std::filesystem::is_regular_file(status)) {
                    it.increment(ec);
                    if (ec) {
                        if (error != nullptr) {
                            *error = ec.message();
                        }
                        return false;
                    }
                    continue;
                }

                std::filesystem::path relative = std::filesystem::relative(it->path(), rootPath, ec);
                if (ec) {
                    if (error != nullptr) {
                        *error = ec.message();
                    }
                    return false;
                }

                FileState state;
                state.HasMetadata = true;
                state.SizeBytes = it->file_size(ec);
                if (ec) {
                    if (error != nullptr) {
                        *error = ec.message();
                    }
                    return false;
                }

                const auto modified = it->last_write_time(ec);
                if (ec) {
                    if (error != nullptr) {
                        *error = ec.message();
                    }
                    return false;
                }
                state.ModifiedTick = modified.time_since_epoch().count();
                if (!HashFile(it->path(), &state.Hash, error)) {
                    return false;
                }
                (*files)[relative.generic_string()] = state;

                it.increment(ec);
                if (ec) {
                    if (error != nullptr) {
                        *error = ec.message();
                    }
                    return false;
                }
            }
            return true;
        }

        bool ParseSnapshotV2Line(const std::string &line, std::string *relativePath, FileState *state) {
            static constexpr std::string_view PREFIX = "v2\t";
            if (!line.starts_with(PREFIX)) {
                return false;
            }

            std::size_t start = PREFIX.size();
            const auto sizeEnd = line.find('\t', start);
            if (sizeEnd == std::string::npos) {
                return false;
            }
            const auto timeEnd = line.find('\t', sizeEnd + 1);
            if (timeEnd == std::string::npos) {
                return false;
            }
            const auto hashEnd = line.find('\t', timeEnd + 1);
            if (hashEnd == std::string::npos) {
                return false;
            }

            FileState parsed;
            if (!ParseUintmax(line.substr(start, sizeEnd - start), &parsed.SizeBytes) ||
                !ParseInt64(line.substr(sizeEnd + 1, timeEnd - sizeEnd - 1), &parsed.ModifiedTick) ||
                !ParseUint64(line.substr(timeEnd + 1, hashEnd - timeEnd - 1), &parsed.Hash)) {
                return false;
            }

            std::string parsedPath = line.substr(hashEnd + 1);
            if (!IsSafeSnapshotRelativePath(parsedPath)) {
                return false;
            }

            parsed.HasMetadata = true;
            if (relativePath != nullptr) {
                *relativePath = std::move(parsedPath);
            }
            if (state != nullptr) {
                *state = parsed;
            }
            return true;
        }

        bool ParseSnapshotV3Line(
            const std::string &line,
            std::string *relativePath,
            SharedFolderSnapshotEntry *entry
        ) {
            static constexpr std::string_view PREFIX = "v3\t";
            if (!line.starts_with(PREFIX)) {
                return false;
            }

            std::size_t start = PREFIX.size();
            std::string hostPresentText;
            std::string hostSizeText;
            std::string hostTimeText;
            std::string hostHashText;
            std::string devicePresentText;
            std::string deviceSizeText;
            std::string deviceTimeText;
            if (!TakeTabField(line, &start, &hostPresentText) ||
                !TakeTabField(line, &start, &hostSizeText) ||
                !TakeTabField(line, &start, &hostTimeText) ||
                !TakeTabField(line, &start, &hostHashText) ||
                !TakeTabField(line, &start, &devicePresentText) ||
                !TakeTabField(line, &start, &deviceSizeText) ||
                !TakeTabField(line, &start, &deviceTimeText)) {
                return false;
            }

            std::string parsedPath = line.substr(start);
            if (!IsSafeSnapshotRelativePath(parsedPath)) {
                return false;
            }

            SharedFolderSnapshotEntry parsed;
            if (!ParseSnapshotBool(hostPresentText, &parsed.HasHost) ||
                !ParseSnapshotBool(devicePresentText, &parsed.HasDevice)) {
                return false;
            }

            parsed.Host.HasMetadata = parsed.HasHost;
            parsed.Device.HasMetadata = parsed.HasDevice;
            if (parsed.HasHost &&
                (!ParseUintmax(hostSizeText, &parsed.Host.SizeBytes) ||
                 !ParseInt64(hostTimeText, &parsed.Host.ModifiedTick) ||
                 !ParseUint64(hostHashText, &parsed.Host.Hash))) {
                return false;
            }
            if (parsed.HasDevice &&
                (!ParseUintmax(deviceSizeText, &parsed.Device.SizeBytes) ||
                 !ParseInt64(deviceTimeText, &parsed.Device.ModifiedTick))) {
                return false;
            }

            if (!parsed.HasHost && !parsed.HasDevice) {
                return false;
            }

            if (relativePath != nullptr) {
                *relativePath = std::move(parsedPath);
            }
            if (entry != nullptr) {
                *entry = parsed;
            }
            return true;
        }

        bool LoadSharedFolderSnapshot(
            const std::string &snapshotPath,
            FileStateMap *files,
            std::string *error
        ) {
            if (files == nullptr) {
                return false;
            }
            files->clear();

            std::error_code ec;
            if (snapshotPath.empty() || !std::filesystem::exists(snapshotPath, ec)) {
                return !ec;
            }
            if (ec) {
                if (error != nullptr) {
                    *error = ec.message();
                }
                return false;
            }

            std::ifstream file(snapshotPath);
            if (!file.is_open()) {
                if (error != nullptr) {
                    *error = "Could not read shared folder sync state.";
                }
                return false;
            }

            std::string line;
            while (std::getline(file, line)) {
                if (!line.empty() && line.back() == '\r') {
                    line.pop_back();
                }

                if (line.starts_with("v3\t")) {
                    std::string relativePath;
                    SharedFolderSnapshotEntry entry;
                    if (!ParseSnapshotV3Line(line, &relativePath, &entry)) {
                        if (error != nullptr) {
                            *error = "Could not read shared folder sync state.";
                        }
                        return false;
                    }
                    if (entry.HasHost) {
                        (*files)[relativePath] = entry.Host;
                    } else if (entry.HasDevice) {
                        (*files)[relativePath] = FileState{};
                    }
                    continue;
                }

                if (line.starts_with("v2\t")) {
                    std::string relativePath;
                    FileState state;
                    if (!ParseSnapshotV2Line(line, &relativePath, &state)) {
                        if (error != nullptr) {
                            *error = "Could not read shared folder sync state.";
                        }
                        return false;
                    }
                    (*files)[relativePath] = state;
                    continue;
                }

                if (IsSafeSnapshotRelativePath(line)) {
                    (*files)[line] = FileState{};
                }
            }
            return true;
        }

        bool LoadSharedFolderIncrementalSnapshot(
            const std::string &snapshotPath,
            SharedFolderSnapshot *snapshot,
            std::string *error
        ) {
            if (snapshot == nullptr) {
                return false;
            }
            snapshot->Format = SharedFolderSnapshotFormat::Missing;
            snapshot->Entries.clear();

            std::error_code ec;
            if (snapshotPath.empty() || !std::filesystem::exists(snapshotPath, ec)) {
                return !ec;
            }
            if (ec) {
                if (error != nullptr) {
                    *error = ec.message();
                }
                return false;
            }

            std::ifstream file(snapshotPath);
            if (!file.is_open()) {
                if (error != nullptr) {
                    *error = "Could not read shared folder sync state.";
                }
                return false;
            }

            bool sawV3 = false;
            bool sawLegacy = false;
            std::string line;
            while (std::getline(file, line)) {
                if (!line.empty() && line.back() == '\r') {
                    line.pop_back();
                }
                if (line.empty()) {
                    continue;
                }

                if (line.starts_with("v3\t")) {
                    std::string relativePath;
                    SharedFolderSnapshotEntry entry;
                    if (!ParseSnapshotV3Line(line, &relativePath, &entry)) {
                        if (error != nullptr) {
                            *error = "Could not read shared folder sync state.";
                        }
                        return false;
                    }
                    snapshot->Entries[relativePath] = entry;
                    sawV3 = true;
                    continue;
                }

                if (line.starts_with("v2\t")) {
                    std::string relativePath;
                    FileState state;
                    if (!ParseSnapshotV2Line(line, &relativePath, &state)) {
                        if (error != nullptr) {
                            *error = "Could not read shared folder sync state.";
                        }
                        return false;
                    }
                    sawLegacy = true;
                    continue;
                }

                if (IsSafeSnapshotRelativePath(line)) {
                    sawLegacy = true;
                    continue;
                }

                if (error != nullptr) {
                    *error = "Could not read shared folder sync state.";
                }
                return false;
            }

            if (sawV3 && sawLegacy) {
                if (error != nullptr) {
                    *error = "Could not read shared folder sync state.";
                }
                return false;
            }
            snapshot->Format = sawV3 || !sawLegacy
                                   ? SharedFolderSnapshotFormat::V3
                                   : SharedFolderSnapshotFormat::Legacy;
            return true;
        }

        bool ParseDeviceMetadataLine(const std::string &line, std::string *relativePath, FileState *state) {
            const auto sizeEnd = line.find('\t');
            if (sizeEnd == std::string::npos) {
                return false;
            }
            const auto timeEnd = line.find('\t', sizeEnd + 1);
            if (timeEnd == std::string::npos) {
                return false;
            }

            FileState parsed;
            if (!ParseUintmax(line.substr(0, sizeEnd), &parsed.SizeBytes) ||
                !ParseInt64(line.substr(sizeEnd + 1, timeEnd - sizeEnd - 1), &parsed.ModifiedTick)) {
                return false;
            }

            std::string parsedPath = line.substr(timeEnd + 1);
            if (parsedPath.starts_with("./")) {
                parsedPath.erase(0, 2);
            }
            if (!IsSafeSnapshotRelativePath(parsedPath)) {
                return false;
            }

            parsed.Hash = 0;
            parsed.HasMetadata = true;
            if (relativePath != nullptr) {
                *relativePath = std::move(parsedPath);
            }
            if (state != nullptr) {
                *state = parsed;
            }
            return true;
        }

        DeviceMetadataResult CollectDeviceSharedFolderStates(
            const SdkInfo &sdk,
            const std::string &serial,
            const std::function<bool()> &shouldCancel
        ) {
            const std::string deviceRoot = NormalizeDevicePath(GetSharedFolderDevicePath());
            const std::string statFormat = "%s\t%Y\t%n";
            const std::string script = StrConcat(
                "root=", ShellQuote(deviceRoot), "; ",
                "mkdir -p -- \"$root\" || exit 1; ",
                "cd \"$root\" || exit 1; ",
                "symlink=$(find . -type l -print -quit); ",
                "if [ -n \"$symlink\" ]; then echo ", ShellQuote(UNSUPPORTED_SYMLINK_ERROR), "; exit 1; fi; ",
                "find . -type f -exec stat -c ", ShellQuote(statFormat), " -- {} \\;"
            );

            auto listed = RunSharedFolderAdbCommand(sdk, {"-s", serial, "shell", script}, shouldCancel);
            DeviceMetadataResult result;
            result.Success = listed.Success;
            result.Output = std::move(listed.Output);
            if (!result.Success) {
                if (result.Output.empty()) {
                    result.Output = "Could not read shared folder file metadata.";
                }
                return result;
            }

            std::istringstream lines(result.Output);
            std::string line;
            while (std::getline(lines, line)) {
                if (!line.empty() && line.back() == '\r') {
                    line.pop_back();
                }
                if (line.empty()) {
                    continue;
                }

                std::string relativePath;
                FileState state;
                if (!ParseDeviceMetadataLine(line, &relativePath, &state)) {
                    result.Success = false;
                    result.Output = "Could not read shared folder file metadata.";
                    result.Files.clear();
                    return result;
                }
                result.Files[relativePath] = state;
            }
            result.Output.clear();
            return result;
        }

        bool PrepareCleanDirectory(const std::filesystem::path &path, std::string *error) {
            std::error_code ec;
            std::filesystem::remove_all(path, ec);
            if (ec) {
                if (error != nullptr) {
                    *error = ec.message();
                }
                return false;
            }
            std::filesystem::create_directories(path, ec);
            if (ec) {
                if (error != nullptr) {
                    *error = ec.message();
                }
                return false;
            }
            return true;
        }

        void AppendOutput(std::string *target, const std::string &extra) {
            if (target == nullptr || extra.empty()) {
                return;
            }
            if (!target->empty() && target->back() != '\n') {
                target->push_back('\n');
            }
            *target += extra;
        }

        bool CopyFilePreservingTime(
            const std::filesystem::path &source,
            const std::filesystem::path &target,
            std::string *error
        ) {
            std::error_code ec;
            const auto sourceTime = std::filesystem::last_write_time(source, ec);
            if (ec) {
                if (error != nullptr) {
                    *error = ec.message();
                }
                return false;
            }

            const std::filesystem::path parent = target.parent_path();
            if (!parent.empty() && std::filesystem::exists(parent, ec) && !ec && !std::filesystem::is_directory(parent, ec)) {
                return true;
            }
            if (ec) {
                if (error != nullptr) {
                    *error = ec.message();
                }
                return false;
            }

            std::filesystem::create_directories(target.parent_path(), ec);
            if (ec) {
                if (error != nullptr) {
                    *error = ec.message();
                }
                return false;
            }

            std::filesystem::copy_file(source, target, std::filesystem::copy_options::overwrite_existing, ec);
            if (ec) {
                if (error != nullptr) {
                    *error = ec.message();
                }
                return false;
            }
            std::filesystem::last_write_time(target, sourceTime, ec);
            if (ec) {
                if (error != nullptr) {
                    *error = ec.message();
                }
                return false;
            }
            return true;
        }

        bool CopySharedFolderFile(
            const std::filesystem::path &sourceRoot,
            const std::filesystem::path &targetRoot,
            const std::string &relativePath,
            std::string *error
        ) {
            if (!IsSafeSnapshotRelativePath(relativePath)) {
                return true;
            }

            const std::filesystem::path target = targetRoot / std::filesystem::path(relativePath);
            std::error_code ec;
            if (std::filesystem::exists(target, ec) && !ec) {
                const auto status = std::filesystem::symlink_status(target, ec);
                if (ec) {
                    if (error != nullptr) {
                        *error = ec.message();
                    }
                    return false;
                }
                if (std::filesystem::is_symlink(status)) {
                    if (error != nullptr) {
                        *error = UNSUPPORTED_SYMLINK_ERROR;
                    }
                    return false;
                }
                if (!std::filesystem::is_regular_file(status)) {
                    return true;
                }
            } else if (ec) {
                if (error != nullptr) {
                    *error = ec.message();
                }
                return false;
            }

            return CopyFilePreservingTime(
                sourceRoot / std::filesystem::path(relativePath),
                target,
                error
            );
        }

        bool RemoveSharedFolderFile(
            const std::filesystem::path &root,
            const std::string &relativePath,
            std::string *error
        ) {
            if (!IsSafeSnapshotRelativePath(relativePath)) {
                return true;
            }

            std::error_code ec;
            const std::filesystem::path target = root / std::filesystem::path(relativePath);
            const auto status = std::filesystem::symlink_status(target, ec);
            if (ec) {
                if (error != nullptr) {
                    *error = ec.message();
                }
                return false;
            }
            if (!std::filesystem::exists(status)) {
                return true;
            }
            if (!std::filesystem::is_regular_file(status) && !std::filesystem::is_symlink(status)) {
                return true;
            }

            std::filesystem::remove(target, ec);
            if (ec) {
                if (error != nullptr) {
                    *error = ec.message();
                }
                return false;
            }
            return true;
        }

        std::filesystem::path BuildConflictPath(
            const std::filesystem::path &hostRoot,
            const std::string &relativePath,
            const char *side,
            std::string *conflictRelativePath
        ) {
            const std::filesystem::path relative(relativePath);
            const std::filesystem::path parent = relative.parent_path();
            const std::string stem = relative.stem().string();
            const std::string extension = relative.extension().string();
            const std::string sideLabel = side == nullptr ? "copy" : side;

            for (int suffix = 0; suffix < 1000; suffix++) {
                std::string name = stem + ".conflict-" + sideLabel;
                if (suffix > 0) {
                    name += "-" + std::to_string(suffix);
                }
                name += extension;

                const std::filesystem::path candidateRelative = parent / name;
                const std::filesystem::path candidate = hostRoot / candidateRelative;

                std::error_code ec;
                if (!std::filesystem::exists(candidate, ec) && !ec) {
                    if (conflictRelativePath != nullptr) {
                        *conflictRelativePath = candidateRelative.generic_string();
                    }
                    return candidate;
                }
            }

            const std::filesystem::path fallbackRelative =
                parent / (stem + ".conflict-" + sideLabel + "-copy" + extension);
            if (conflictRelativePath != nullptr) {
                *conflictRelativePath = fallbackRelative.generic_string();
            }
            return hostRoot / fallbackRelative;
        }

        bool CopyConflictFile(
            const std::filesystem::path &source,
            const std::filesystem::path &hostRoot,
            const std::string &relativePath,
            const char *side,
            SharedFolderReconcileChanges *changes,
            std::string *error,
            std::string *createdRelativePath = nullptr
        ) {
            std::string conflictRelativePath;
            const std::filesystem::path conflictPath = BuildConflictPath(hostRoot, relativePath, side, &conflictRelativePath);
            if (!CopyFilePreservingTime(source, conflictPath, error)) {
                return false;
            }
            if (createdRelativePath != nullptr) {
                *createdRelativePath = conflictRelativePath;
            }
            if (changes != nullptr) {
                changes->ConflictPaths.push_back(std::move(conflictRelativePath));
            }
            return true;
        }

        const FileState *FindFileState(const FileStateMap &files, const std::string &relativePath) {
            const auto it = files.find(relativePath);
            return it == files.end() ? nullptr : &it->second;
        }

        bool SameContent(const FileState &a, const FileState &b) {
            return a.HasMetadata && b.HasMetadata && a.SizeBytes == b.SizeBytes && a.Hash == b.Hash;
        }

        bool SameDeviceMetadata(const FileState &a, const FileState &b) {
            return a.HasMetadata && b.HasMetadata && a.SizeBytes == b.SizeBytes && a.ModifiedTick == b.ModifiedTick;
        }

        bool ChangedFromBase(const FileState *current, const FileState *base) {
            if (base == nullptr) {
                return current != nullptr;
            }
            if (current == nullptr) {
                return true;
            }
            if (!base->HasMetadata) {
                return false;
            }
            return !SameContent(*current, *base);
        }

        const SharedFolderSnapshotEntry *FindSnapshotEntry(
            const SharedFolderSnapshotMap &files,
            const std::string &relativePath
        ) {
            const auto it = files.find(relativePath);
            return it == files.end() ? nullptr : &it->second;
        }

        bool HostChangedFromBase(const FileState *current, const SharedFolderSnapshotEntry *base) {
            if (base == nullptr || !base->HasHost) {
                return current != nullptr;
            }
            if (current == nullptr) {
                return true;
            }
            return !SameContent(*current, base->Host);
        }

        bool DeviceChangedFromBase(const FileState *current, const SharedFolderSnapshotEntry *base) {
            if (base == nullptr || !base->HasDevice) {
                return current != nullptr;
            }
            if (current == nullptr) {
                return true;
            }
            return !SameDeviceMetadata(*current, base->Device);
        }

        bool CopyDeviceChangeToHost(
            const std::filesystem::path &stagingRoot,
            const std::filesystem::path &hostRoot,
            const std::string &relativePath,
            std::string *error
        ) {
            return CopySharedFolderFile(stagingRoot, hostRoot, relativePath, error);
        }

        bool ReconcileLegacyPath(
            const std::filesystem::path &stagingRoot,
            const std::filesystem::path &hostRoot,
            const std::string &relativePath,
            const FileState *host,
            const FileState *device,
            std::string *error
        ) {
            if (host != nullptr && device == nullptr) {
                return RemoveSharedFolderFile(hostRoot, relativePath, error);
            }
            if (device != nullptr && host == nullptr) {
                return CopyDeviceChangeToHost(stagingRoot, hostRoot, relativePath, error);
            }
            if (host == nullptr || device == nullptr || SameContent(*host, *device)) {
                return true;
            }
            if (device->ModifiedTick > host->ModifiedTick) {
                return CopyDeviceChangeToHost(stagingRoot, hostRoot, relativePath, error);
            }
            return true;
        }

        bool SaveSharedFolderSnapshot(
            const std::string &snapshotPath,
            const std::string &rootPath,
            std::string *error
        ) {
            FileStateMap files;
            if (!CollectRelativeFileStates(rootPath, &files, error)) {
                return false;
            }

            const std::filesystem::path path(snapshotPath);
            std::error_code ec;
            std::filesystem::create_directories(path.parent_path(), ec);
            if (ec) {
                if (error != nullptr) {
                    *error = ec.message();
                }
                return false;
            }

            const std::filesystem::path tempPath = path.string() + ".tmp";
            std::ofstream file(tempPath);
            if (!file.is_open()) {
                if (error != nullptr) {
                    *error = "Could not write shared folder sync state.";
                }
                return false;
            }
            for (const auto &[relativePath, state]: files) {
                file << "v2\t"
                     << state.SizeBytes << '\t'
                     << state.ModifiedTick << '\t'
                     << state.Hash << '\t'
                     << relativePath << '\n';
            }
            file.close();
            if (!file.good()) {
                if (error != nullptr) {
                    *error = "Could not write shared folder sync state.";
                }
                return false;
            }

            std::filesystem::remove(path, ec);
            ec.clear();
            std::filesystem::rename(tempPath, path, ec);
            if (ec) {
                if (error != nullptr) {
                    *error = ec.message();
                }
                return false;
            }
            return true;
        }

        bool SaveSharedFolderSnapshotV3(
            const std::string &snapshotPath,
            const FileStateMap &hostFiles,
            const FileStateMap &deviceFiles,
            std::string *error
        ) {
            const std::filesystem::path path(snapshotPath);
            std::error_code ec;
            std::filesystem::create_directories(path.parent_path(), ec);
            if (ec) {
                if (error != nullptr) {
                    *error = ec.message();
                }
                return false;
            }

            const std::filesystem::path tempPath = path.string() + ".tmp";
            std::ofstream file(tempPath);
            if (!file.is_open()) {
                if (error != nullptr) {
                    *error = "Could not write shared folder sync state.";
                }
                return false;
            }

            std::set<std::string> allPaths;
            for (const auto &[relativePath, _]: hostFiles) {
                allPaths.insert(relativePath);
            }
            for (const auto &[relativePath, _]: deviceFiles) {
                allPaths.insert(relativePath);
            }

            for (const auto &relativePath: allPaths) {
                if (!IsSafeSnapshotRelativePath(relativePath)) {
                    continue;
                }

                const FileState *host = FindFileState(hostFiles, relativePath);
                const FileState *device = FindFileState(deviceFiles, relativePath);
                file << "v3\t"
                     << (host != nullptr ? 1 : 0) << '\t'
                     << (host != nullptr ? host->SizeBytes : 0) << '\t'
                     << (host != nullptr ? host->ModifiedTick : 0) << '\t'
                     << (host != nullptr ? host->Hash : 0) << '\t'
                     << (device != nullptr ? 1 : 0) << '\t'
                     << (device != nullptr ? device->SizeBytes : 0) << '\t'
                     << (device != nullptr ? device->ModifiedTick : 0) << '\t'
                     << relativePath << '\n';
            }
            file.close();
            if (!file.good()) {
                if (error != nullptr) {
                    *error = "Could not write shared folder sync state.";
                }
                return false;
            }

            std::filesystem::remove(path, ec);
            ec.clear();
            std::filesystem::rename(tempPath, path, ec);
            if (ec) {
                if (error != nullptr) {
                    *error = ec.message();
                }
                return false;
            }
            return true;
        }

        bool SaveDeviceToHostSnapshotV3(
            const std::string &snapshotPath,
            const SharedFolderSnapshotMap &baseFiles,
            const FileStateMap &hostFiles,
            const FileStateMap &deviceFiles,
            const FileStateMap &stagingFiles,
            std::string *error
        ) {
            FileStateMap deviceContentBaseline;
            for (const auto &[relativePath, _]: deviceFiles) {
                if (const FileState *staged = FindFileState(stagingFiles, relativePath)) {
                    deviceContentBaseline[relativePath] = *staged;
                    continue;
                }
                if (const auto *base = FindSnapshotEntry(baseFiles, relativePath);
                    base != nullptr && base->HasHost) {
                    deviceContentBaseline[relativePath] = base->Host;
                    continue;
                }
                if (const FileState *host = FindFileState(hostFiles, relativePath)) {
                    deviceContentBaseline[relativePath] = *host;
                }
            }
            return SaveSharedFolderSnapshotV3(snapshotPath, deviceContentBaseline, deviceFiles, error);
        }

        bool DeleteSharedFolderDevicePaths(
            const SdkInfo &sdk,
            const std::string &serial,
            const std::vector<std::string> &relativePaths,
            const std::function<bool()> &shouldCancel,
            std::string *output
        ) {
            const std::string deviceRoot = NormalizeDevicePath(GetSharedFolderDevicePath());
            for (const auto &relativePath: relativePaths) {
                if (!IsSafeSnapshotRelativePath(relativePath)) {
                    continue;
                }

                const std::string devicePath = JoinDevicePath(deviceRoot, relativePath);
                auto removed = RunSharedFolderAdbCommand(
                    sdk,
                    {"-s", serial, "shell", StrConcat("rm -f -- ", ShellQuote(devicePath))},
                    shouldCancel
                );
                AppendOutput(output, removed.Output);
                if (!removed.Success) {
                    if (output != nullptr && output->empty()) {
                        *output = "Could not delete shared folder file on device.";
                    }
                    return false;
                }
            }
            return true;
        }

        SharedFolderSyncResult PullSharedFolderDevicePaths(
            const SdkInfo &sdk,
            const std::string &serial,
            const std::filesystem::path &stagingRoot,
            const std::set<std::string> &relativePaths,
            const std::function<bool()> &shouldCancel
        ) {
            std::string error;
            if (!PrepareCleanDirectory(stagingRoot, &error)) {
                return Failure(error.empty() ? "Could not prepare shared folder staging." : error);
            }

            std::string output;
            const std::string deviceRoot = NormalizeDevicePath(GetSharedFolderDevicePath());
            for (const auto &relativePath: relativePaths) {
                if (!IsSafeSnapshotRelativePath(relativePath)) {
                    continue;
                }

                const std::filesystem::path localPath = stagingRoot / std::filesystem::path(relativePath);
                std::error_code ec;
                std::filesystem::create_directories(localPath.parent_path(), ec);
                if (ec) {
                    return Failure(ec.message());
                }

                const auto pulled = RunSharedFolderAdbCommand(
                    sdk,
                    {
                        "-s",
                        serial,
                        "pull",
                        "-a",
                        JoinDevicePath(deviceRoot, relativePath),
                        localPath.string(),
                    },
                    shouldCancel
                );
                AppendOutput(&output, pulled.Output);
                if (!pulled.Success) {
                    return Failure(output.empty() ? "Could not pull shared folder file from device." : output);
                }
            }

            return {.Success = true, .Output = std::move(output)};
        }

        SharedFolderSyncResult PushSharedFolderHostPaths(
            const SdkInfo &sdk,
            const std::string &serial,
            const std::filesystem::path &hostRoot,
            const std::set<std::string> &relativePaths,
            const std::function<bool()> &shouldCancel
        ) {
            std::string output;
            const std::string deviceRoot = NormalizeDevicePath(GetSharedFolderDevicePath());
            for (const auto &relativePath: relativePaths) {
                if (!IsSafeSnapshotRelativePath(relativePath)) {
                    continue;
                }

                const std::filesystem::path localPath = hostRoot / std::filesystem::path(relativePath);
                std::error_code ec;
                const auto status = std::filesystem::symlink_status(localPath, ec);
                if (ec) {
                    return Failure(ec.message());
                }
                if (!std::filesystem::exists(status)) {
                    continue;
                }
                if (std::filesystem::is_symlink(status)) {
                    return Failure(UNSUPPORTED_SYMLINK_ERROR);
                }
                if (!std::filesystem::is_regular_file(status)) {
                    continue;
                }

                const std::filesystem::path parent = std::filesystem::path(relativePath).parent_path();
                const std::string remoteDirectory = parent.empty()
                                                        ? deviceRoot
                                                        : JoinDevicePath(deviceRoot, parent.generic_string());
                const auto created = CreateDeviceDirectory(sdk, serial, remoteDirectory, shouldCancel);
                AppendOutput(&output, created.Output);
                if (!created.Success) {
                    return Failure(output.empty() ? "Could not create shared folder on device." : output);
                }

                const auto pushed = RunSharedFolderAdbCommand(
                    sdk,
                    {
                        "-s",
                        serial,
                        "push",
                        "-a",
                        localPath.string(),
                        remoteDirectory,
                    },
                    shouldCancel
                );
                AppendOutput(&output, pushed.Output);
                if (!pushed.Success) {
                    return Failure(output.empty() ? "Could not push shared folder file to device." : output);
                }
            }

            return {.Success = true, .Output = std::move(output)};
        }

        void AppendConflictSummary(std::string *output, const SharedFolderReconcileChanges &changes) {
            if (output == nullptr || changes.ConflictPaths.empty()) {
                return;
            }

            if (!output->empty() && output->back() != '\n') {
                output->push_back('\n');
            }
            *output += "Shared folder conflicts were saved as:\n";
            for (const auto &path: changes.ConflictPaths) {
                *output += path;
                output->push_back('\n');
            }
        }

        std::set<std::string> BuildIncrementalPathSet(
            const SharedFolderSnapshotMap &baseFiles,
            const FileStateMap &hostFiles,
            const FileStateMap &deviceFiles
        ) {
            std::set<std::string> allPaths;
            for (const auto &[path, _]: baseFiles) {
                allPaths.insert(path);
            }
            for (const auto &[path, _]: hostFiles) {
                allPaths.insert(path);
            }
            for (const auto &[path, _]: deviceFiles) {
                allPaths.insert(path);
            }
            return allPaths;
        }

        std::set<std::string> DetermineIncrementalDevicePullPaths(
            const SharedFolderSnapshotMap &baseFiles,
            const FileStateMap &hostFiles,
            const FileStateMap &deviceFiles
        ) {
            std::set<std::string> pullPaths;
            const std::set<std::string> allPaths = BuildIncrementalPathSet(baseFiles, hostFiles, deviceFiles);
            for (const auto &relativePath: allPaths) {
                const FileState *device = FindFileState(deviceFiles, relativePath);
                if (device == nullptr) {
                    continue;
                }
                const SharedFolderSnapshotEntry *base = FindSnapshotEntry(baseFiles, relativePath);
                if (DeviceChangedFromBase(device, base)) {
                    pullPaths.insert(relativePath);
                }
            }
            return pullPaths;
        }

        bool RequireStagedDeviceFile(
            const FileStateMap &stagingFiles,
            const std::string &relativePath,
            const FileState **state,
            std::string *error
        ) {
            const FileState *staged = FindFileState(stagingFiles, relativePath);
            if (staged == nullptr) {
                if (error != nullptr) {
                    *error = "Could not read changed device shared folder file.";
                }
                return false;
            }
            if (state != nullptr) {
                *state = staged;
            }
            return true;
        }

        bool ReconcileIncrementalSharedFolder(
            const std::filesystem::path &stagingRoot,
            const std::filesystem::path &hostRoot,
            const SharedFolderSnapshotMap &baseFiles,
            const FileStateMap &hostFiles,
            const FileStateMap &deviceFiles,
            const FileStateMap &stagingFiles,
            const SharedFolderSyncMode mode,
            SharedFolderReconcileChanges *changes,
            std::set<std::string> *hostPathsToPush,
            bool *hostMutated,
            std::string *error
        ) {
            if (hostPathsToPush == nullptr) {
                return false;
            }
            if (hostMutated != nullptr) {
                *hostMutated = false;
            }
            if (changes != nullptr) {
                changes->DeviceDeletedPaths.clear();
                changes->ConflictPaths.clear();
            }

            const std::set<std::string> allPaths = BuildIncrementalPathSet(baseFiles, hostFiles, deviceFiles);
            for (const auto &relativePath: allPaths) {
                const SharedFolderSnapshotEntry *base = FindSnapshotEntry(baseFiles, relativePath);
                const FileState *host = FindFileState(hostFiles, relativePath);
                const FileState *device = FindFileState(deviceFiles, relativePath);
                const bool hostExists = host != nullptr;
                const bool deviceExists = device != nullptr;
                const bool hostChanged = HostChangedFromBase(host, base);
                const bool deviceChanged = DeviceChangedFromBase(device, base);

                if (hostExists && deviceExists) {
                    if (!hostChanged && !deviceChanged) {
                        continue;
                    }
                    if (hostChanged && !deviceChanged) {
                        if (mode == SharedFolderSyncMode::Bidirectional) {
                            hostPathsToPush->insert(relativePath);
                        }
                        continue;
                    }

                    const FileState *staged = nullptr;
                    if (!RequireStagedDeviceFile(stagingFiles, relativePath, &staged, error)) {
                        return false;
                    }
                    if (!hostChanged && deviceChanged) {
                        if (!CopyDeviceChangeToHost(stagingRoot, hostRoot, relativePath, error)) {
                            return false;
                        }
                        if (hostMutated != nullptr) {
                            *hostMutated = true;
                        }
                        continue;
                    }

                    if (SameContent(*host, *staged)) {
                        continue;
                    }

                    std::string conflictRelativePath;
                    if (!CopyConflictFile(stagingRoot / std::filesystem::path(relativePath),
                                          hostRoot,
                                          relativePath,
                                          "device",
                                          changes,
                                          error,
                                          &conflictRelativePath)) {
                        return false;
                    }
                    if (hostMutated != nullptr) {
                        *hostMutated = true;
                    }
                    if (mode == SharedFolderSyncMode::Bidirectional) {
                        hostPathsToPush->insert(relativePath);
                        hostPathsToPush->insert(conflictRelativePath);
                    }
                    continue;
                }

                if (hostExists && !deviceExists) {
                    if (!hostChanged && !deviceChanged) {
                        continue;
                    }
                    if (hostChanged && !deviceChanged) {
                        if (mode == SharedFolderSyncMode::Bidirectional) {
                            hostPathsToPush->insert(relativePath);
                        }
                        continue;
                    }
                    if (!hostChanged && deviceChanged) {
                        if (!RemoveSharedFolderFile(hostRoot, relativePath, error)) {
                            return false;
                        }
                        if (hostMutated != nullptr) {
                            *hostMutated = true;
                        }
                        continue;
                    }

                    std::string conflictRelativePath;
                    if (!CopyConflictFile(hostRoot / std::filesystem::path(relativePath),
                                          hostRoot,
                                          relativePath,
                                          "host",
                                          changes,
                                          error,
                                          &conflictRelativePath)) {
                        return false;
                    }
                    if (!RemoveSharedFolderFile(hostRoot, relativePath, error)) {
                        return false;
                    }
                    if (hostMutated != nullptr) {
                        *hostMutated = true;
                    }
                    if (mode == SharedFolderSyncMode::Bidirectional) {
                        hostPathsToPush->insert(conflictRelativePath);
                    }
                    continue;
                }

                if (!hostExists && deviceExists) {
                    if (!hostChanged && !deviceChanged) {
                        continue;
                    }
                    if (!hostChanged && deviceChanged) {
                        if (!CopyDeviceChangeToHost(stagingRoot, hostRoot, relativePath, error)) {
                            return false;
                        }
                        if (hostMutated != nullptr) {
                            *hostMutated = true;
                        }
                        continue;
                    }
                    if (hostChanged && !deviceChanged) {
                        if (changes != nullptr) {
                            changes->DeviceDeletedPaths.push_back(relativePath);
                        }
                        continue;
                    }

                    if (!RequireStagedDeviceFile(stagingFiles, relativePath, nullptr, error)) {
                        return false;
                    }

                    std::string conflictRelativePath;
                    if (!CopyConflictFile(stagingRoot / std::filesystem::path(relativePath),
                                          hostRoot,
                                          relativePath,
                                          "device",
                                          changes,
                                          error,
                                          &conflictRelativePath)) {
                        return false;
                    }
                    if (hostMutated != nullptr) {
                        *hostMutated = true;
                    }
                    if (changes != nullptr) {
                        changes->DeviceDeletedPaths.push_back(relativePath);
                    }
                    if (mode == SharedFolderSyncMode::Bidirectional) {
                        hostPathsToPush->insert(conflictRelativePath);
                    }
                }
            }
            return true;
        }

        SharedFolderSyncResult PushSharedFolderToDevice(
            const SdkInfo &sdk,
            const std::string &serial,
            const std::string &avdName,
            const std::function<bool()> &shouldCancel
        ) {
            std::string error;
            if (!EnsureSharedFolderHostPath(avdName, &error)) {
                return Failure(error.empty() ? "Could not prepare shared folder." : error);
            }

            const std::string hostPath = GetSharedFolderHostPath(avdName);
            const std::string devicePath = GetSharedFolderDevicePath();
            const auto created = CreateDeviceDirectory(sdk, serial, devicePath, shouldCancel);
            if (!created.Success) {
                return Failure(created.Output.empty() ? "Could not create shared folder on device." : created.Output);
            }

            std::error_code ec;
            if (std::filesystem::is_empty(hostPath, ec) && !ec) {
                return {.Success = true, .Output = created.Output};
            }

            auto pushed = RunSharedFolderAdbCommand(
                sdk,
                BuildPushSharedFolderToDeviceArgs(serial, hostPath, devicePath),
                shouldCancel
            );
            if (!created.Output.empty()) {
                pushed.Output = created.Output + pushed.Output;
            }
            return pushed;
        }

        PullSharedFolderResult PullSharedFolderFromDevice(
            const SdkInfo &sdk,
            const std::string &serial,
            const std::string &avdName,
            const std::function<bool()> &shouldCancel
        ) {
            std::string error;
            if (!EnsureSharedFolderHostPath(avdName, &error)) {
                return PullFailure(error.empty() ? "Could not prepare shared folder." : error);
            }

            const std::string hostPath = GetSharedFolderHostPath(avdName);
            const std::string devicePath = GetSharedFolderDevicePath();
            const auto created = CreateDeviceDirectory(sdk, serial, devicePath, shouldCancel);
            if (!created.Success) {
                return PullFailure(created.Output.empty() ? "Could not create shared folder on device." : created.Output);
            }

            const std::filesystem::path stagingPath = BuildStagingPath(avdName);
            std::error_code ec;
            std::filesystem::remove_all(stagingPath, ec);
            ec.clear();
            std::filesystem::create_directories(stagingPath, ec);
            if (ec) {
                return PullFailure(ec.message());
            }

            auto pulled = RunSharedFolderAdbCommand(
                sdk,
                BuildPullSharedFolderFromDeviceArgs(serial, devicePath, stagingPath.string()),
                shouldCancel
            );
            PullSharedFolderResult result;
            result.Success = pulled.Success;
            result.Output = std::move(pulled.Output);
            result.StagingPath = stagingPath;
            result.HostPath = hostPath;
            if (!pulled.Success) {
                std::filesystem::remove_all(stagingPath, ec);
                return result;
            }

            if (!created.Output.empty()) {
                result.Output = created.Output + result.Output;
            }
            return result;
        }
    }

    std::string GetSharedFolderHostPath(const std::string &avdName) {
        const std::string rootPath = Paths::GetAppConfigPath("shared");
        const std::string folderName = SanitizeFolderName(avdName);
        if (rootPath.empty() || folderName.empty()) {
            return rootPath;
        }
        return Paths::JoinPaths({rootPath, folderName});
    }

    std::string GetSharedFolderDevicePath() {
        return "/sdcard/CoreDeckShared";
    }

    const char *GetOpenSharedFolderHostLabel() {
#if defined(_WIN32)
        return "Open Shared Folder in File Explorer";
#elif defined(__APPLE__)
        return "Open Shared Folder in Finder";
#else
        return "Open Shared Folder in File Manager";
#endif
    }

    OpenFolderCommand BuildOpenFolderCommand(const std::string &folderPath) {
#if defined(_WIN32)
        return {.Executable = "explorer.exe", .Args = {folderPath}};
#elif defined(__APPLE__)
        return {.Executable = "/usr/bin/open", .Args = {folderPath}};
#else
        return {.Executable = "xdg-open", .Args = {folderPath}};
#endif
    }

    std::string BuildSharedFolderContentPath(const std::string &folderPath) {
        return (std::filesystem::path(folderPath) / ".").string();
    }

    std::string BuildSharedFolderDeviceContentPath(const std::string &devicePath) {
        const std::string normalized = NormalizeDevicePath(devicePath);
        return normalized == "/" ? "/." : normalized + "/.";
    }

    std::vector<std::string> BuildPushSharedFolderToDeviceArgs(
        const std::string &serial,
        const std::string &hostPath,
        const std::string &devicePath
    ) {
        return {
            "-s",
            serial,
            "push",
            "-a",
            BuildSharedFolderContentPath(hostPath),
            NormalizeDevicePath(devicePath),
        };
    }

    std::vector<std::string> BuildPullSharedFolderFromDeviceArgs(
        const std::string &serial,
        const std::string &devicePath,
        const std::string &hostPath
    ) {
        return {
            "-s",
            serial,
            "pull",
            "-a",
            BuildSharedFolderDeviceContentPath(devicePath),
            hostPath,
        };
    }

        bool MergePulledSharedFolder(
            const std::string &stagingPath,
            const std::string &hostPath,
            std::string *error
        ) {
        if (error != nullptr) {
            error->clear();
        }

        const std::filesystem::path stagingRoot = stagingPath;
        const std::filesystem::path hostRoot = hostPath;

        std::error_code ec;
        if (!std::filesystem::is_directory(stagingRoot, ec) || ec) {
            if (error != nullptr) {
                *error = ec ? ec.message() : "Pulled shared folder staging path is not a directory.";
            }
            return false;
        }

        std::filesystem::create_directories(hostRoot, ec);
        if (ec) {
            if (error != nullptr) {
                *error = ec.message();
            }
            return false;
        }

        for (std::filesystem::recursive_directory_iterator it(
                 stagingRoot,
                 std::filesystem::directory_options::skip_permission_denied,
                 ec
             ),
             end;
             it != end;
             it.increment(ec)) {
            if (ec) {
                if (error != nullptr) {
                    *error = ec.message();
                }
                return false;
            }

            const std::filesystem::path source = it->path();
            std::filesystem::path relative = std::filesystem::relative(source, stagingRoot, ec);
            if (ec) {
                if (error != nullptr) {
                    *error = ec.message();
                }
                return false;
            }

            const std::filesystem::path target = hostRoot / relative;
            const auto status = it->symlink_status(ec);
            if (ec) {
                if (error != nullptr) {
                    *error = ec.message();
                }
                return false;
            }
            if (std::filesystem::is_symlink(status)) {
                if (error != nullptr) {
                    *error = UNSUPPORTED_SYMLINK_ERROR;
                }
                return false;
            }
            if (std::filesystem::is_directory(status)) {
                if (std::filesystem::exists(target, ec) && !ec && !std::filesystem::is_directory(target, ec)) {
                    it.disable_recursion_pending();
                    continue;
                }
                if (ec) {
                    if (error != nullptr) {
                        *error = ec.message();
                    }
                    return false;
                }
                std::filesystem::create_directories(target, ec);
                if (ec) {
                    if (error != nullptr) {
                        *error = ec.message();
                    }
                    return false;
                }
                continue;
            }

            if (!std::filesystem::is_regular_file(status)) {
                continue;
            }

            const std::filesystem::file_time_type sourceTime = std::filesystem::last_write_time(source, ec);
            if (ec) {
                if (error != nullptr) {
                    *error = ec.message();
                }
                return false;
            }

            bool shouldCopy = true;
            if (std::filesystem::exists(target, ec) && !ec) {
                if (!std::filesystem::is_regular_file(target, ec) || ec) {
                    if (ec) {
                        if (error != nullptr) {
                            *error = ec.message();
                        }
                        return false;
                    }
                    continue;
                }

                const std::filesystem::file_time_type targetTime = std::filesystem::last_write_time(target, ec);
                if (ec) {
                    if (error != nullptr) {
                        *error = ec.message();
                    }
                    return false;
                }
                shouldCopy = sourceTime > targetTime;
            } else if (ec) {
                if (error != nullptr) {
                    *error = ec.message();
                }
                return false;
            }

            if (!shouldCopy) {
                continue;
            }

            std::filesystem::create_directories(target.parent_path(), ec);
            if (ec) {
                if (error != nullptr) {
                    *error = ec.message();
                }
                return false;
            }
            std::filesystem::copy_file(source, target, std::filesystem::copy_options::overwrite_existing, ec);
            if (ec) {
                if (error != nullptr) {
                    *error = ec.message();
                }
                return false;
            }
            std::filesystem::last_write_time(target, sourceTime, ec);
            if (ec) {
                if (error != nullptr) {
                    *error = ec.message();
                }
                return false;
            }
        }

        return true;
    }

    bool ReconcilePulledSharedFolder(
        const std::string &stagingPath,
        const std::string &hostPath,
        const std::string &snapshotPath,
        std::string *error,
        SharedFolderReconcileChanges *changes
    ) {
        if (error != nullptr) {
            error->clear();
        }
        if (changes != nullptr) {
            changes->DeviceDeletedPaths.clear();
            changes->ConflictPaths.clear();
        }

        const std::filesystem::path stagingRoot = stagingPath;
        const std::filesystem::path hostRoot = hostPath;

        std::error_code ec;
        std::filesystem::create_directories(hostRoot, ec);
        if (ec) {
            if (error != nullptr) {
                *error = ec.message();
            }
            return false;
        }

        FileStateMap baseFiles;
        FileStateMap hostFiles;
        FileStateMap deviceFiles;
        if (!LoadSharedFolderSnapshot(snapshotPath, &baseFiles, error)) {
            return false;
        }
        if (!CollectRelativeFileStates(hostRoot, &hostFiles, error)) {
            return false;
        }
        if (!CollectRelativeFileStates(stagingRoot, &deviceFiles, error)) {
            return false;
        }

        std::set<std::string> allPaths;
        for (const auto &[path, _]: baseFiles) {
            allPaths.insert(path);
        }
        for (const auto &[path, _]: hostFiles) {
            allPaths.insert(path);
        }
        for (const auto &[path, _]: deviceFiles) {
            allPaths.insert(path);
        }

        for (const auto &relativePath: allPaths) {
            const FileState *base = FindFileState(baseFiles, relativePath);
            const FileState *host = FindFileState(hostFiles, relativePath);
            const FileState *device = FindFileState(deviceFiles, relativePath);

            if (base != nullptr && !base->HasMetadata) {
                if (!ReconcileLegacyPath(stagingRoot, hostRoot, relativePath, host, device, error)) {
                    return false;
                }
                continue;
            }

            const bool hostExists = host != nullptr;
            const bool deviceExists = device != nullptr;
            const bool hostChanged = ChangedFromBase(host, base);
            const bool deviceChanged = ChangedFromBase(device, base);

            if (hostExists && deviceExists) {
                if (!hostChanged && !deviceChanged) {
                    continue;
                }
                if (hostChanged && !deviceChanged) {
                    continue;
                }
                if (!hostChanged && deviceChanged) {
                    if (!CopyDeviceChangeToHost(stagingRoot, hostRoot, relativePath, error)) {
                        return false;
                    }
                    continue;
                }
                if (SameContent(*host, *device)) {
                    continue;
                }
                if (!CopyConflictFile(stagingRoot / std::filesystem::path(relativePath),
                                      hostRoot,
                                      relativePath,
                                      "device",
                                      changes,
                                      error)) {
                    return false;
                }
                continue;
            }

            if (hostExists && !deviceExists) {
                if (base == nullptr) {
                    continue;
                }
                if (!hostChanged) {
                    if (!RemoveSharedFolderFile(hostRoot, relativePath, error)) {
                        return false;
                    }
                    continue;
                }
                // Preserve the edited host copy under a conflict name and remove
                // the original path so the deleted side does not come back next sync.
                if (!CopyConflictFile(hostRoot / std::filesystem::path(relativePath),
                                      hostRoot,
                                      relativePath,
                                      "host",
                                      changes,
                                      error)) {
                    return false;
                }
                if (!RemoveSharedFolderFile(hostRoot, relativePath, error)) {
                    return false;
                }
                continue;
            }

            if (!hostExists && deviceExists) {
                if (base == nullptr) {
                    if (!CopyDeviceChangeToHost(stagingRoot, hostRoot, relativePath, error)) {
                        return false;
                    }
                    continue;
                }
                if (!deviceChanged) {
                    if (changes != nullptr) {
                        changes->DeviceDeletedPaths.push_back(relativePath);
                    }
                    continue;
                }
                // Preserve the edited device copy under a conflict name and delete
                // the original path so the resolved state stays stable.
                if (!CopyConflictFile(stagingRoot / std::filesystem::path(relativePath),
                                      hostRoot,
                                      relativePath,
                                      "device",
                                      changes,
                                      error)) {
                    return false;
                }
                if (changes != nullptr) {
                    changes->DeviceDeletedPaths.push_back(relativePath);
                }
                continue;
            }
        }

        return true;
    }

    bool EnsureSharedFolderHostPath(const std::string &avdName, std::string *error) {
        if (error != nullptr) {
            error->clear();
        }

        const std::string folderPath = GetSharedFolderHostPath(avdName);
        if (folderPath.empty()) {
            if (error != nullptr) {
                *error = "Could not determine the shared folder location.";
            }
            return false;
        }

        std::error_code ec;
        std::filesystem::create_directories(folderPath, ec);
        if (ec) {
            if (error != nullptr) {
                *error = ec.message();
            }
            return false;
        }

        return std::filesystem::is_directory(folderPath, ec) && !ec;
    }

    bool EnsureSharedFolderHostPath(std::string *error) {
        return EnsureSharedFolderHostPath("", error);
    }

    bool OpenSharedFolderHostPath(const std::string &avdName, std::string *error) {
        if (!EnsureSharedFolderHostPath(avdName, error)) {
            return false;
        }

        const OpenFolderCommand command = BuildOpenFolderCommand(GetSharedFolderHostPath(avdName));
        return StreamCommandArgsWithEnvCancelable(command.Executable, command.Args, "", {}, nullptr, {});
    }

    bool OpenSharedFolderHostPath(std::string *error) {
        return OpenSharedFolderHostPath("", error);
    }

    SharedFolderSyncResult SyncSharedFolder(
        const SdkInfo &sdk,
        const std::string &serial,
        const std::string &avdName,
        const SharedFolderSyncMode mode,
        const std::function<bool()> &shouldCancel
    ) {
        if (serial.empty()) {
            return Failure("No device selected.");
        }

        const std::string snapshotPath = BuildSnapshotPath(avdName).string();
        SharedFolderSnapshot snapshot;
        std::string error;
        if (!LoadSharedFolderIncrementalSnapshot(snapshotPath, &snapshot, &error)) {
            return Failure(error.empty() ? "Could not update shared folder sync state." : error);
        }

        if (!EnsureSharedFolderHostPath(avdName, &error)) {
            return Failure(error.empty() ? "Could not prepare shared folder." : error);
        }

        const std::filesystem::path hostPath = GetSharedFolderHostPath(avdName);
        const std::filesystem::path stagingPath = BuildStagingPath(avdName);
        const std::string devicePath = GetSharedFolderDevicePath();
        const auto created = CreateDeviceDirectory(sdk, serial, devicePath, shouldCancel);
        if (!created.Success) {
            return Failure(created.Output.empty() ? "Could not create shared folder on device." : created.Output);
        }
        const std::string startupOutput = created.Output;

        if (snapshot.Format != SharedFolderSnapshotFormat::V3) {
            auto pulled = PullSharedFolderFromDevice(sdk, serial, avdName, shouldCancel);
            if (!pulled.Success) {
                return pulled;
            }

            SharedFolderReconcileChanges changes;
            auto cleanupStaging = [&] {
                std::error_code ec;
                std::filesystem::remove_all(pulled.StagingPath, ec);
            };

            if (!ReconcilePulledSharedFolder(
                    pulled.StagingPath.string(),
                    pulled.HostPath,
                    snapshotPath,
                    &error,
                    &changes
                )) {
                cleanupStaging();
                return Failure(error.empty() ? "Could not sync shared folder." : error);
            }

            if (mode == SharedFolderSyncMode::DeviceToHost) {
                FileStateMap stagingFiles;
                if (!CollectRelativeFileStates(pulled.StagingPath, &stagingFiles, &error)) {
                    cleanupStaging();
                    return Failure(error.empty() ? "Could not update shared folder sync state." : error);
                }
                DeviceMetadataResult deviceState = CollectDeviceSharedFolderStates(sdk, serial, shouldCancel);
                if (!deviceState.Success) {
                    cleanupStaging();
                    return Failure(deviceState.Output.empty() ? "Could not update shared folder sync state." : deviceState.Output);
                }
                if (!SaveSharedFolderSnapshotV3(snapshotPath, stagingFiles, deviceState.Files, &error)) {
                    cleanupStaging();
                    return Failure(error.empty() ? "Could not update shared folder sync state." : error);
                }
                cleanupStaging();
                AppendConflictSummary(&pulled.Output, changes);
                return pulled;
            }

            std::string deviceOutput;
            if (!DeleteSharedFolderDevicePaths(sdk, serial, changes.DeviceDeletedPaths, shouldCancel, &deviceOutput)) {
                cleanupStaging();
                return Failure(deviceOutput.empty() ? "Could not update shared folder on device." : deviceOutput);
            }

            auto pushed = PushSharedFolderToDevice(sdk, serial, avdName, shouldCancel);
            if (!pushed.Success) {
                cleanupStaging();
                if (!pulled.Output.empty()) {
                    pushed.Output = pulled.Output + pushed.Output;
                }
                if (!deviceOutput.empty()) {
                    pushed.Output = deviceOutput + pushed.Output;
                }
                return pushed;
            }
            FileStateMap hostFiles;
            if (!CollectRelativeFileStates(pulled.HostPath, &hostFiles, &error)) {
                cleanupStaging();
                return Failure(error.empty() ? "Could not update shared folder sync state." : error);
            }
            DeviceMetadataResult deviceState = CollectDeviceSharedFolderStates(sdk, serial, shouldCancel);
            if (!deviceState.Success) {
                cleanupStaging();
                return Failure(deviceState.Output.empty() ? "Could not update shared folder sync state." : deviceState.Output);
            }
            if (!SaveSharedFolderSnapshotV3(snapshotPath, hostFiles, deviceState.Files, &error)) {
                cleanupStaging();
                return Failure(error.empty() ? "Could not update shared folder sync state." : error);
            }
            cleanupStaging();
            AppendConflictSummary(&pushed.Output, changes);
            if (!deviceOutput.empty()) {
                pushed.Output = deviceOutput + pushed.Output;
            }
            if (!pulled.Output.empty()) {
                pushed.Output = pulled.Output + pushed.Output;
            }
            return pushed;
        }

        FileStateMap hostFiles;
        FileStateMap deviceFiles;
        if (!CollectRelativeFileStates(hostPath, &hostFiles, &error)) {
            return Failure(error.empty() ? "Could not sync shared folder." : error);
        }
        DeviceMetadataResult deviceState = CollectDeviceSharedFolderStates(sdk, serial, shouldCancel);
        if (!deviceState.Success) {
            return Failure(deviceState.Output.empty() ? "Could not sync shared folder." : deviceState.Output);
        }
        deviceFiles = std::move(deviceState.Files);

        const std::set<std::string> pullPaths = DetermineIncrementalDevicePullPaths(snapshot.Entries, hostFiles, deviceFiles);
        auto pulled = PullSharedFolderDevicePaths(sdk, serial, stagingPath, pullPaths, shouldCancel);
        if (!pulled.Success) {
            std::error_code ec;
            std::filesystem::remove_all(stagingPath, ec);
            return pulled;
        }

        FileStateMap stagingFiles;
        if (!pullPaths.empty() && !CollectRelativeFileStates(stagingPath, &stagingFiles, &error)) {
            std::error_code ec;
            std::filesystem::remove_all(stagingPath, ec);
            return Failure(error.empty() ? "Could not sync shared folder." : error);
        }

        std::set<std::string> pushPaths;
        SharedFolderReconcileChanges changes;
        bool hostMutated = false;
        if (!ReconcileIncrementalSharedFolder(
                stagingPath,
                hostPath,
                snapshot.Entries,
                hostFiles,
                deviceFiles,
                stagingFiles,
                mode,
                &changes,
                &pushPaths,
                &hostMutated,
                &error
            )) {
            std::error_code ec;
            std::filesystem::remove_all(stagingPath, ec);
            return Failure(error.empty() ? "Could not sync shared folder." : error);
        }

        auto cleanupStaging = [&] {
            std::error_code ec;
            std::filesystem::remove_all(stagingPath, ec);
        };

        if (mode == SharedFolderSyncMode::DeviceToHost) {
            FileStateMap hostSaveFiles = hostFiles;
            if (hostMutated) {
                if (!CollectRelativeFileStates(hostPath, &hostSaveFiles, &error)) {
                    cleanupStaging();
                    return Failure(error.empty() ? "Could not update shared folder sync state." : error);
                }
            }
            if (hostMutated || !pullPaths.empty()) {
                if (!SaveDeviceToHostSnapshotV3(snapshotPath, snapshot.Entries, hostSaveFiles, deviceFiles, stagingFiles, &error)) {
                    cleanupStaging();
                    return Failure(error.empty() ? "Could not update shared folder sync state." : error);
                }
            }
            cleanupStaging();
            std::string resultOutput = startupOutput;
            AppendOutput(&resultOutput, pulled.Output);
            AppendConflictSummary(&resultOutput, changes);
            return {.Success = true, .Output = std::move(resultOutput)};
        }

        std::string deviceOutput;
        if (!changes.DeviceDeletedPaths.empty() &&
            !DeleteSharedFolderDevicePaths(sdk, serial, changes.DeviceDeletedPaths, shouldCancel, &deviceOutput)) {
            cleanupStaging();
            return Failure(deviceOutput.empty() ? "Could not update shared folder on device." : deviceOutput);
        }

        std::set<std::string> finalPushPaths = pushPaths;
        if (!finalPushPaths.empty()) {
            auto pushed = PushSharedFolderHostPaths(sdk, serial, hostPath, finalPushPaths, shouldCancel);
            if (!pushed.Success) {
                cleanupStaging();
                if (!pulled.Output.empty()) {
                    pushed.Output = pulled.Output + pushed.Output;
                }
                if (!deviceOutput.empty()) {
                    pushed.Output = deviceOutput + pushed.Output;
                }
                return pushed;
            }
            AppendOutput(&deviceOutput, pushed.Output);
        }

        const bool deviceMutated = !changes.DeviceDeletedPaths.empty() || !finalPushPaths.empty();
        if (hostMutated || deviceMutated || !pullPaths.empty()) {
            FileStateMap hostSaveFiles = hostFiles;
            FileStateMap deviceSaveFiles = deviceFiles;
            if (hostMutated && !CollectRelativeFileStates(hostPath, &hostSaveFiles, &error)) {
                cleanupStaging();
                return Failure(error.empty() ? "Could not update shared folder sync state." : error);
            }
            if (deviceMutated) {
                DeviceMetadataResult finalDeviceState = CollectDeviceSharedFolderStates(sdk, serial, shouldCancel);
                if (!finalDeviceState.Success) {
                    cleanupStaging();
                    return Failure(finalDeviceState.Output.empty() ? "Could not update shared folder sync state." : finalDeviceState.Output);
                }
                deviceSaveFiles = std::move(finalDeviceState.Files);
            }
            if (!SaveSharedFolderSnapshotV3(snapshotPath, hostSaveFiles, deviceSaveFiles, &error)) {
                cleanupStaging();
                return Failure(error.empty() ? "Could not update shared folder sync state." : error);
            }
        }
        cleanupStaging();
        std::string output = startupOutput;
        AppendOutput(&output, pulled.Output);
        AppendOutput(&output, deviceOutput);
        AppendConflictSummary(&output, changes);
        return {.Success = true, .Output = std::move(output)};
    }
}
