//
// Created by AbdulMuaz Aqeel on 05/04/2026.
//

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <unordered_map>

#include "avd.h"
#include "paths.h"
#include "process.h"
#include "utilities.h"

namespace CoreDeck {
    namespace {
        bool IsSafeSnapshotName(const std::string &name) {
            if (name.empty() || name == "." || name == "..") {
                return false;
            }

            const std::filesystem::path path(name);
            return !path.is_absolute() &&
                   !path.has_root_name() &&
                   path.parent_path().empty() &&
                   path.filename().string() == name;
        }

        std::int64_t ToEpochSeconds(const std::filesystem::file_time_type time) {
            const auto systemTime = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
                time - std::filesystem::file_time_type::clock::now() + std::chrono::system_clock::now()
            );
            return static_cast<std::int64_t>(std::chrono::system_clock::to_time_t(systemTime));
        }

        std::filesystem::file_time_type LatestWriteTime(const std::filesystem::path &path) {
            std::error_code ec;
            std::filesystem::file_time_type latest = std::filesystem::last_write_time(path, ec);
            if (ec) {
                latest = {};
            }

            std::filesystem::recursive_directory_iterator it(
                path,
                std::filesystem::directory_options::skip_permission_denied,
                ec
            );
            const std::filesystem::recursive_directory_iterator end;
            while (!ec && it != end) {
                std::error_code entryEc;
                const auto modified = it->last_write_time(entryEc);
                if (!entryEc && modified > latest) {
                    latest = modified;
                }
                it.increment(ec);
            }
            return latest;
        }

        std::unordered_map<std::string, std::string> ParseConfigFile(const std::string &path) {
            std::unordered_map<std::string, std::string> config;
            std::ifstream file(path);
            if (!file.is_open()) {
                return config;
            }

            std::string line;
            while (std::getline(file, line)) {
                if (!line.empty() && line.back() == '\r') {
                    line.pop_back();
                }
                if (line.empty() || line[0] == '#') {
                    continue;
                }

                auto eq = line.find('=');
                if (eq == std::string::npos) {
                    continue;
                }

                auto key = line.substr(0, eq);
                auto value = line.substr(eq + 1);

                while (!key.empty() && (key.back() == ' ' || key.back() == '\t')) {
                    key.pop_back();
                }
                while (!key.empty() && (key.front() == ' ' || key.front() == '\t')) {
                    key.erase(key.begin());
                }
                while (!value.empty() && (value.back() == ' ' || value.back() == '\t')) {
                    value.pop_back();
                }
                while (!value.empty() && (value.front() == ' ' || value.front() == '\t')) {
                    value.erase(value.begin());
                }

                config[key] = value;
            }

            return config;
        }

        std::string TrimCopy(std::string value) {
            while (!value.empty() && (value.back() == ' ' || value.back() == '\t')) {
                value.pop_back();
            }
            while (!value.empty() && (value.front() == ' ' || value.front() == '\t')) {
                value.erase(value.begin());
            }
            return value;
        }

        bool IsConfigKey(const std::string &line, const std::string &key) {
            const auto eq = line.find('=');
            if (eq == std::string::npos) {
                return false;
            }
            return TrimCopy(line.substr(0, eq)) == key;
        }

        std::string SanitizeConfigValue(std::string value) {
            std::erase(value, '\r');
            std::erase(value, '\n');
            return value;
        }

        std::vector<std::string> SplitConfigList(const std::string &value) {
            std::vector<std::string> items;
            std::stringstream stream(value);
            std::string item;
            while (std::getline(stream, item, ',')) {
                while (!item.empty() && (item.back() == ' ' || item.back() == '\t')) {
                    item.pop_back();
                }
                while (!item.empty() && (item.front() == ' ' || item.front() == '\t')) {
                    item.erase(item.begin());
                }
                if (!item.empty()) {
                    items.push_back(item);
                }
            }
            return items;
        }

        bool HasTag(const std::vector<std::string> &tags, const std::string &needle) {
            return std::ranges::any_of(tags, [&](const std::string &tag) {
                return LowerCopy(tag) == needle;
            });
        }

        void ExtractSystemImageInfo(AvdInfo &avd, const std::unordered_map<std::string, std::string> &config) {
            if (const auto it = config.find("image.sysdir.1"); it != config.end()) {
                avd.SystemImagePath = it->second;

                std::string sysdir = it->second;
                std::ranges::replace(sysdir, '\\', '/');

                if (auto start = sysdir.find("android-"); start != std::string::npos) {
                    start += 8;
                    if (const auto end = sysdir.find('/', start); end != std::string::npos) {
                        avd.ApiLevel = sysdir.substr(start, end - start);

                        const auto variantStart = end + 1;
                        if (const auto variantEnd = sysdir.find('/', variantStart); variantEnd != std::string::npos) {
                            avd.SystemImageVariant = sysdir.substr(variantStart, variantEnd - variantStart);

                            const auto abiStart = variantEnd + 1;
                            if (const auto abiEnd = sysdir.find('/', abiStart); abiEnd != std::string::npos && avd.Abi.empty()) {
                                avd.Abi = sysdir.substr(abiStart, abiEnd - abiStart);
                            }
                        }
                    }
                }
            }

            if (const auto it = config.find("tag.id"); it != config.end()) {
                avd.SystemImageTagId = it->second;
            }
            if (const auto it = config.find("tag.display"); it != config.end()) {
                avd.SystemImageTagDisplay = it->second;
            }
            if (const auto it = config.find("tag.ids"); it != config.end()) {
                avd.SystemImageTagIds = SplitConfigList(it->second);
            } else if (!avd.SystemImageTagId.empty()) {
                avd.SystemImageTagIds = {avd.SystemImageTagId};
            }
            if (const auto it = config.find("tag.displaynames"); it != config.end()) {
                avd.SystemImageTagDisplayNames = SplitConfigList(it->second);
            } else if (!avd.SystemImageTagDisplay.empty()) {
                avd.SystemImageTagDisplayNames = {avd.SystemImageTagDisplay};
            }

            const std::string variant = LowerCopy(avd.SystemImageVariant);
            const std::string tagId = LowerCopy(avd.SystemImageTagId);
            const std::string tagDisplay = LowerCopy(avd.SystemImageTagDisplay);
            const std::string tagDisplayNames = LowerCopy(StrConcat(
                avd.SystemImageTagDisplay,
                " ",
                config.contains("tag.displaynames") ? config.at("tag.displaynames") : ""
            ));

            avd.IsGooglePlayImage =
                variant.find("google_apis_playstore") != std::string::npos ||
                tagId == "google_apis_playstore" ||
                HasTag(avd.SystemImageTagIds, "google_apis_playstore") ||
                tagDisplay.find("play") != std::string::npos;

            avd.IsGoogleApisImage =
                avd.IsGooglePlayImage ||
                variant.find("google_apis") != std::string::npos ||
                tagId == "google_apis" ||
                HasTag(avd.SystemImageTagIds, "google_apis") ||
                tagDisplay.find("google apis") != std::string::npos;

            avd.Supports16KbPageSize =
                variant.find("ps16k") != std::string::npos ||
                HasTag(avd.SystemImageTagIds, "page_size_16kb") ||
                tagDisplayNames.find("16kb") != std::string::npos ||
                tagDisplayNames.find("16 kb") != std::string::npos;
        }

        AvdInfo ExtractAvdInfo(const std::string &avdName) {
            AvdInfo avd;

            const std::string avdRoot = Paths::GetAvdDirectory();
            if (avdRoot.empty()) {
                return avd;
            }

            const std::string path = Paths::JoinPaths({avdRoot, avdName + ".avd"});

            avd.Name = avdName;
            avd.DisplayName = avdName;
            avd.Path = path;

            std::string configPath = Paths::JoinPaths({avd.Path, "config.ini"});

            if (!std::filesystem::exists(configPath)) {
                return avd;
            }

            auto config = ParseConfigFile(configPath);

            if (auto it = config.find("hw.device.name"); it != config.end()) {
                avd.Device = it->second;
            }

            if (auto it = config.find("avd.ini.displayname"); it != config.end() && !it->second.empty()) {
                avd.DisplayName = it->second;
            }

            if (auto it = config.find("abi.type"); it != config.end()) {
                avd.Abi = it->second;
            }

            ExtractSystemImageInfo(avd, config);

            if (auto it = config.find("sdcard.size"); it != config.end()) {
                avd.SdCard = it->second;
            }

            if (auto it = config.find("hw.ramSize"); it != config.end()) {
                avd.RamSize = it->second;
            }

            if (auto it = config.find("hw.cpu.arch"); it != config.end()) {
                avd.Arch = it->second;
            }

            std::string width;
            std::string height;
            if (auto it = config.find("hw.lcd.width"); it != config.end()) {
                width = it->second;
            }

            if (auto it = config.find("hw.lcd.height"); it != config.end()) {
                height = it->second;
            }

            if (!width.empty() && !height.empty()) {
                std::stringstream ss;
                ss << width << "x" << height;
                avd.ScreenResolution = ss.str();
            }

            if (auto it = config.find("hw.gpu.mode"); it != config.end()) {
                avd.GpuMode = it->second;
            }

            if (auto it = config.find("skin.name"); it != config.end()) {
                avd.SkinName = it->second;
            }

            return avd;
        }
    }

    std::vector<AvdInfo> LoadAvds(const std::vector<std::string> &avdNames) {
        std::vector<AvdInfo> avds;
        avds.reserve(avdNames.size());

        for (const auto &avdName: avdNames) {
            const auto &avd = ExtractAvdInfo(avdName);
            avds.push_back(avd);
        }
        return avds;
    }

    std::vector<std::string> ListAvdNames(const SdkInfo &sdk) {
        std::vector<std::string> avds;
        if (!sdk.IsFound) {
            return avds;
        }

        const std::string output = RunCommandArgs(sdk.EmulatorPath, {"-list-avds"});
        std::istringstream stream(output);
        std::string line;
        while (std::getline(stream, line)) {
            if (!line.empty()) {
                avds.emplace_back(line);
            }
        }

        return avds;
    }

    bool CreateAvd(const SdkInfo &sdk, const AvdCreationData &data) {
        if (sdk.AvdManagerPath.empty()) {
            return false;
        }
        if (data.Name.empty() || data.SystemImagePackagePath.empty()) {
            return false;
        }

        std::vector<std::string> args = {
            "create",
            "avd",
            "-n",
            data.Name,
            "-k",
            data.SystemImagePackagePath
        };
        if (!data.DeviceId.empty()) {
            args.emplace_back("-d");
            args.push_back(data.DeviceId);
        }
        RunCommandArgsWithEnv(sdk.AvdManagerPath, args, "no\n", BuildAndroidToolEnvironment(sdk));

        const std::string avdDir = Paths::GetAvdDirectory();
        const std::string configPath = Paths::JoinPaths({avdDir, data.Name + ".avd", "config.ini"});

        if (std::filesystem::exists(configPath)) {
            std::ofstream file(configPath, std::ios::app);
            if (file.is_open()) {
                if (!data.DisplayName.empty()) {
                    file << "avd.ini.displayname=" << data.DisplayName << "\n";
                }
                if (!data.RamSize.empty()) {
                    file << "hw.ramSize=" << data.RamSize << "\n";
                }
                if (!data.SdCardSize.empty()) {
                    file << "sdcard.size=" << data.SdCardSize << "\n";
                }
                if (!data.GpuMode.empty()) {
                    file << "hw.gpu.mode=" << data.GpuMode << "\n";
                    file << "hw.gpu.enabled=yes\n";
                }
                if (!data.SkinName.empty()) {
                    file << "skin.name=" << data.SkinName << "\n";
                    if (!data.SkinPath.empty()) {
                        file << "skin.path=" << data.SkinPath << "\n";
                    }
                }
            }
        }

        const std::string avdFolder = Paths::JoinPaths({avdDir, data.Name + ".avd"});
        return std::filesystem::exists(avdFolder);
    }

    bool DeleteAvd(const SdkInfo &sdk, const std::string &avdName) {
        if (sdk.AvdManagerPath.empty()) {
            return false;
        }

        RunCommandArgsWithEnv(
            sdk.AvdManagerPath,
            {"delete", "avd", "-n", avdName},
            "",
            BuildAndroidToolEnvironment(sdk)
        );

        const std::string avdDir = Paths::GetAvdDirectory();
        const std::string avdFolder = Paths::JoinPaths({avdDir, avdName + ".avd"});
        return !std::filesystem::exists(avdFolder);
    }

    std::vector<AvdSnapshotInfo> ListAvdSnapshots(const std::string &avdPath, std::string *error) {
        if (error != nullptr) {
            error->clear();
        }

        std::vector<AvdSnapshotInfo> snapshots;
        if (avdPath.empty()) {
            if (error != nullptr) {
                *error = "AVD path is empty.";
            }
            return snapshots;
        }

        const std::filesystem::path snapshotsRoot = std::filesystem::path(avdPath) / "snapshots";
        std::error_code ec;
        const auto snapshotsStatus = std::filesystem::symlink_status(snapshotsRoot, ec);
        if (ec) {
            if (error != nullptr) {
                *error = ec.message();
            }
            return snapshots;
        }
        if (snapshotsStatus.type() == std::filesystem::file_type::not_found) {
            return snapshots;
        }
        if (std::filesystem::is_symlink(snapshotsStatus)) {
            if (error != nullptr) {
                *error = "AVD snapshots path must not be a symbolic link.";
            }
            return snapshots;
        }
        if (!std::filesystem::is_directory(snapshotsStatus)) {
            if (error != nullptr) {
                *error = "AVD snapshots path is not a directory.";
            }
            return snapshots;
        }

        std::filesystem::directory_iterator it(snapshotsRoot, ec);
        if (ec) {
            if (error != nullptr) {
                *error = ec.message();
            }
            return snapshots;
        }

        const std::filesystem::directory_iterator end;
        while (it != end) {
            const auto &entry = *it;
            std::error_code statusEc;
            if (entry.is_directory(statusEc) && !statusEc) {
                AvdSnapshotInfo snapshot;
                snapshot.Name = entry.path().filename().string();
                snapshot.SizeBytes = GetDirectorySize(entry.path().string());
                snapshot.ModifiedEpochSeconds = ToEpochSeconds(LatestWriteTime(entry.path()));
                snapshots.push_back(std::move(snapshot));
            }

            it.increment(ec);
            if (ec) {
                if (error != nullptr) {
                    *error = ec.message();
                }
                snapshots.clear();
                return snapshots;
            }
        }

        std::ranges::sort(snapshots, [](const AvdSnapshotInfo &a, const AvdSnapshotInfo &b) {
            if (a.ModifiedEpochSeconds != b.ModifiedEpochSeconds) {
                return a.ModifiedEpochSeconds > b.ModifiedEpochSeconds;
            }
            return a.Name < b.Name;
        });
        return snapshots;
    }

    bool DeleteAvdSnapshot(const std::string &avdPath, const std::string &snapshotName, std::string *error) {
        if (error != nullptr) {
            error->clear();
        }
        if (avdPath.empty()) {
            if (error != nullptr) {
                *error = "AVD path is empty.";
            }
            return false;
        }
        if (!IsSafeSnapshotName(snapshotName)) {
            if (error != nullptr) {
                *error = "Invalid snapshot name.";
            }
            return false;
        }

        const std::filesystem::path snapshotsRoot = std::filesystem::path(avdPath) / "snapshots";
        const std::filesystem::path target = snapshotsRoot / snapshotName;
        std::error_code ec;
        const auto snapshotsStatus = std::filesystem::symlink_status(snapshotsRoot, ec);
        if (ec) {
            if (error != nullptr) {
                *error = ec.message();
            }
            return false;
        }
        if (snapshotsStatus.type() == std::filesystem::file_type::not_found) {
            return true;
        }
        if (std::filesystem::is_symlink(snapshotsStatus)) {
            if (error != nullptr) {
                *error = "AVD snapshots path must not be a symbolic link.";
            }
            return false;
        }
        if (!std::filesystem::is_directory(snapshotsStatus)) {
            if (error != nullptr) {
                *error = "AVD snapshots path is not a directory.";
            }
            return false;
        }

        if (!std::filesystem::exists(target, ec)) {
            return !ec;
        }
        if (ec) {
            if (error != nullptr) {
                *error = ec.message();
            }
            return false;
        }

        const auto status = std::filesystem::symlink_status(target, ec);
        if (ec) {
            if (error != nullptr) {
                *error = ec.message();
            }
            return false;
        }
        if (!std::filesystem::is_directory(status)) {
            if (error != nullptr) {
                *error = "Snapshot path is not a directory.";
            }
            return false;
        }

        const auto canonicalRoot = std::filesystem::canonical(snapshotsRoot, ec);
        if (ec) {
            if (error != nullptr) {
                *error = ec.message();
            }
            return false;
        }
        const auto canonicalTarget = std::filesystem::canonical(target, ec);
        if (ec) {
            if (error != nullptr) {
                *error = ec.message();
            }
            return false;
        }
        if (canonicalTarget.parent_path() != canonicalRoot) {
            if (error != nullptr) {
                *error = "Snapshot path escapes the snapshots directory.";
            }
            return false;
        }

        std::filesystem::remove_all(target, ec);
        if (ec) {
            if (error != nullptr) {
                *error = ec.message();
            }
            return false;
        }
        return true;
    }

    bool SetAvdDisplayName(const std::string &avdPath, const std::string &displayName) {
        if (avdPath.empty()) {
            return false;
        }

        const std::string configPath = Paths::JoinPaths({avdPath, "config.ini"});
        if (!std::filesystem::exists(configPath)) {
            return false;
        }

        std::ifstream input(configPath);
        if (!input.is_open()) {
            return false;
        }

        constexpr const char *DISPLAY_NAME_KEY = "avd.ini.displayname";
        std::vector<std::string> lines;
        std::string line;
        bool foundDisplayName = false;
        const std::string cleanDisplayName = SanitizeConfigValue(displayName);

        while (std::getline(input, line)) {
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }

            if (IsConfigKey(line, DISPLAY_NAME_KEY)) {
                if (!foundDisplayName && !cleanDisplayName.empty()) {
                    lines.push_back(StrConcat(DISPLAY_NAME_KEY, "=", cleanDisplayName));
                }
                foundDisplayName = true;
                continue;
            }

            lines.push_back(std::move(line));
        }
        input.close();

        if (!foundDisplayName && !cleanDisplayName.empty()) {
            lines.push_back(StrConcat(DISPLAY_NAME_KEY, "=", cleanDisplayName));
        }

        std::ofstream output(configPath, std::ios::trunc);
        if (!output.is_open()) {
            return false;
        }

        for (const auto &savedLine: lines) {
            output << savedLine << '\n';
        }
        return true;
    }
}
