#include "apk_analyzer.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <regex>
#include <sstream>
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

    ApkReport AnalyzeApk(const SdkInfo &sdk, const std::string &path, const std::function<bool()> &cancel) {
        ApkReport report;
        report.Path = path;
        if (cancel && cancel()) {
            report.Cancelled = true;
            return report;
        }
        if (!std::filesystem::is_regular_file(path)) {
            report.Error = "Select an existing APK file.";
            return report;
        }
        const auto tool = FindAapt2(sdk);
        if (tool.empty()) {
            report.Error = "Install Android SDK Build-Tools in SDK settings to analyze APKs.";
            return report;
        }
        const auto command = Run(sdk, tool, {"dump", "badging", std::filesystem::absolute(path).string()}, 30, cancel);
        if (command.Ok) report = ParseApkBadging(command.Output);
        else report.Error = command.TimedOut ? "APK analysis timed out." : "Could not analyze APK.";
        report.Path = path;
        report.Cancelled = command.Cancelled;
        report.Details = command.Output;
        if (report.Cancelled) report.Error.clear();
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
}
