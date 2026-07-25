//
// Created by AbdulMuaz Aqeel on 19/04/2026.
//

#include "sdk_packages.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <sstream>
#include <string_view>
#include <unordered_map>
#include <utility>

#include "paths.h"
#include "process.h"
#include "utilities.h"

namespace CoreDeck {
    namespace {
        enum class ListSection : uint8_t {
            None,
            Installed,
            Available,
            Updates,
        };

        std::string Trim(std::string value) {
            while (!value.empty() && (value.back() == '\r' || value.back() == '\n' || value.back() == ' ' || value.back() == '\t')) {
                value.pop_back();
            }
            while (!value.empty() && (value.front() == '\r' || value.front() == '\n' || value.front() == ' ' || value.front() == '\t')) {
                value.erase(value.begin());
            }
            return value;
        }

        std::vector<std::string> SplitColumns(const std::string &line) {
            std::vector<std::string> columns;
            std::stringstream stream(line);
            std::string column;
            while (std::getline(stream, column, '|')) {
                columns.push_back(Trim(column));
            }
            return columns;
        }

        bool IsHeaderOrSeparator(const std::string &line) {
            if (line.empty()) {
                return true;
            }
            if (line.find('|') == std::string::npos) {
                return false;
            }
            return line.find("Path") != std::string::npos ||
                   line.find("ID") != std::string::npos ||
                   line.find("---") != std::string::npos;
        }

        bool LooksObsolete(const std::string &path, const std::string &description) {
            const std::string text = LowerCopy(StrConcat(path, " ", description));
            return text.find("obsolete") != std::string::npos ||
                   text.find("deprecated") != std::string::npos;
        }

        int SortApiValue(const SdkPackage &package) {
            const std::string &path = package.Path;
            const auto pos = path.find("android-");
            if (pos == std::string::npos) {
                return -1;
            }
            return static_cast<int>(std::strtol(path.c_str() + pos + 8, nullptr, 10));
        }

        std::vector<std::string> BuildSdkManagerArgs(const SdkInfo &sdk, const std::vector<std::string> &args) {
            std::vector<std::string> result;
            result.reserve(args.size() + 1);
            if (!sdk.SdkPath.empty()) {
                result.push_back(StrConcat("--sdk_root=", sdk.SdkPath));
            }
            result.insert(result.end(), args.begin(), args.end());
            return result;
        }

        void ParseProgressLine(const std::string &line, const std::shared_ptr<SdkOperationProgress> &progress) {
            if (!progress) {
                return;
            }

            const auto bracket = line.find('[');
            const auto closeBracket = line.find(']', bracket);
            if (bracket == std::string::npos || closeBracket == std::string::npos) {
                if (!line.empty()) {
                    std::lock_guard lock(progress->Mutex);
                    progress->DetailText = line;
                }
                return;
            }

            auto afterBracket = line.substr(closeBracket + 1);
            const auto start = afterBracket.find_first_not_of(" \t");
            if (start == std::string::npos) {
                return;
            }
            afterBracket = afterBracket.substr(start);

            const auto pctEnd = afterBracket.find('%');
            if (pctEnd == std::string::npos) {
                return;
            }

            const int pct = static_cast<int>(std::strtol(afterBracket.substr(0, pctEnd).c_str(), nullptr, 10));

            std::string description;
            if (pctEnd + 1 < afterBracket.size()) {
                description = afterBracket.substr(pctEnd + 1);
                if (const auto ds = description.find_first_not_of(" \t"); ds != std::string::npos) {
                    description = description.substr(ds);
                }
            }

            std::lock_guard lock(progress->Mutex);
            progress->Percent = static_cast<float>(pct) / 100.0F;
            if (!description.empty()) {
                progress->StatusText = description;
            }
            progress->DetailText = line;
        }

        void MarkFinished(const std::shared_ptr<SdkOperationProgress> &progress, const bool ok, const char *success, const char *failure) {
            if (!progress) {
                return;
            }

            std::lock_guard lock(progress->Mutex);
            progress->Finished = true;
            progress->Succeeded = ok;
            progress->Percent = ok ? 1.0F : progress->Percent;
            progress->StatusText = ok ? success : failure;
        }
    }

    bool HasSdkManager(const std::string &sdkRoot) {
        if (sdkRoot.empty()) {
            return false;
        }

        auto hasSdkManagerInBinDir = [](const std::string &binDir) {
#if defined(_WIN32)
            return std::filesystem::exists(Paths::JoinPaths({binDir, "sdkmanager.bat"})) ||
                   std::filesystem::exists(Paths::JoinPaths({binDir, "sdkmanager.exe"}));
#else
            return std::filesystem::exists(Paths::JoinPaths({binDir, "sdkmanager"}));
#endif
        };

        const std::string latestBinDir = Paths::JoinPaths({sdkRoot, "cmdline-tools", "latest", "bin"});
        if (hasSdkManagerInBinDir(latestBinDir)) {
            return true;
        }

        const std::string cmdlineRoot = Paths::JoinPaths({sdkRoot, "cmdline-tools"});
        std::error_code ec;
        if (!std::filesystem::exists(cmdlineRoot, ec) || !std::filesystem::is_directory(cmdlineRoot, ec)) {
            return false;
        }

        for (const auto &entry: std::filesystem::directory_iterator(cmdlineRoot, ec)) {
            if (ec) {
                return false;
            }
            std::error_code entryEc;
            if (!entry.is_directory(entryEc)) {
                continue;
            }
            if (hasSdkManagerInBinDir(Paths::JoinPaths({entry.path().string(), "bin"}))) {
                return true;
            }
        }
        return false;
    }

    bool IsStableSdkPlatformPackage(const std::string &path) {
        static constexpr std::string_view PREFIX = "platforms;android-";
        if (!path.starts_with(PREFIX)) {
            return false;
        }

        const std::string version = path.substr(PREFIX.size());
        bool hasDigit = false;
        return std::ranges::all_of(version, [&](const char c) {
            if (std::isdigit(static_cast<unsigned char>(c)) != 0) {
                hasDigit = true;
                return true;
            }
            return c == '.';
        }) && hasDigit;
    }

    std::string SelectLatestSdkPlatformPackage(const std::vector<SdkPackage> &packages) {
        auto it = std::ranges::find_if(packages, [](const SdkPackage &package) {
            return IsStableSdkPlatformPackage(package.Path);
        });
        return it == packages.end() ? std::string{} : it->Path;
    }

    SdkPackageListResult ListSdkPackages(const SdkInfo &sdk, const bool includeObsolete) {
        SdkPackageListResult result;
        if (sdk.SdkManagerPath.empty()) {
            result.SdkManagerMissing = true;
            result.Error = "SDK Manager was not found.";
            return result;
        }

        std::vector<std::string> args = {"--list"};
        if (includeObsolete) {
            args.emplace_back("--include_obsolete");
        }

        const std::string output = RunCommandArgsWithEnv(
            sdk.SdkManagerPath,
            BuildSdkManagerArgs(sdk, args),
            "",
            BuildAndroidToolEnvironment(sdk)
        );

        std::unordered_map<std::string, SdkPackage> packages;
        ListSection section = ListSection::None;
        bool sawWarning = false;
        std::string firstDiagnostic;

        std::istringstream stream(output);
        std::string rawLine;
        while (std::getline(stream, rawLine)) {
            std::string line = Trim(rawLine);
            if (line.empty()) {
                continue;
            }
            if (line.starts_with("Warning:") || line.starts_with("Error:")) {
                sawWarning = true;
                if (firstDiagnostic.empty()) {
                    firstDiagnostic = line;
                }
                continue;
            }
            if (line == "Installed packages:") {
                section = ListSection::Installed;
                continue;
            }
            if (line == "Available Packages:") {
                section = ListSection::Available;
                continue;
            }
            if (line == "Available Updates:") {
                section = ListSection::Updates;
                continue;
            }
            if (IsHeaderOrSeparator(line)) {
                continue;
            }
            if (section == ListSection::None && firstDiagnostic.empty()) {
                firstDiagnostic = line;
            }

            const std::vector<std::string> columns = SplitColumns(line);
            if (columns.empty() || columns[0].empty()) {
                continue;
            }

            SdkPackage &package = packages[columns[0]];
            package.Path = columns[0];

            if (section == ListSection::Installed) {
                if (columns.size() >= 2) {
                    package.InstalledVersion = columns[1];
                    package.Version = columns[1];
                }
                if (columns.size() >= 3 && package.Description.empty()) {
                    package.Description = columns[2];
                }
                if (columns.size() >= 4) {
                    package.Location = columns[3];
                }
                package.Installed = true;
            } else if (section == ListSection::Available) {
                if (columns.size() >= 2) {
                    package.AvailableVersion = columns[1];
                    if (package.Version.empty()) {
                        package.Version = columns[1];
                    }
                }
                if (columns.size() >= 3 && package.Description.empty()) {
                    package.Description = columns[2];
                }
                package.Available = true;
            } else if (section == ListSection::Updates) {
                if (columns.size() >= 2) {
                    package.InstalledVersion = columns[1];
                }
                if (columns.size() >= 3) {
                    package.AvailableVersion = columns[2];
                    package.Version = columns[2];
                }
                package.UpdateAvailable = true;
                package.Installed = true;
            }

            package.Obsolete = LooksObsolete(package.Path, package.Description);
        }

        result.Packages.reserve(packages.size());
        for (auto &[_, package]: packages) {
            if (!includeObsolete && package.Obsolete) {
                continue;
            }
            result.Packages.push_back(std::move(package));
        }

        std::ranges::sort(result.Packages, [](const SdkPackage &a, const SdkPackage &b) {
            const int apiA = SortApiValue(a);
            const int apiB = SortApiValue(b);
            if (apiA != apiB) {
                return apiA > apiB;
            }
            if (a.Installed != b.Installed) {
                return a.Installed;
            }
            return a.Path < b.Path;
        });

        if (result.Packages.empty()) {
            if (!firstDiagnostic.empty()) {
                result.Error = firstDiagnostic;
            } else if (sawWarning) {
                result.Error = "Could not fetch SDK package lists. Check your network connection.";
            }
        }

        return result;
    }

    bool InstallSdkPackages(
        const SdkInfo &sdk,
        const std::vector<std::string> &packagePaths,
        const std::shared_ptr<SdkOperationProgress> &progress
    ) {
        if (sdk.SdkManagerPath.empty() || packagePaths.empty()) {
            return false;
        }

        if (progress) {
            std::lock_guard lock(progress->Mutex);
            progress->StatusText = "Starting installation...";
            progress->Percent = 0.0F;
            progress->Finished = false;
            progress->Succeeded = false;
        }

        std::vector<std::string> args = {"--install"};
        args.insert(args.end(), packagePaths.begin(), packagePaths.end());

        StreamCommandArgsWithEnv(
            sdk.SdkManagerPath,
            BuildSdkManagerArgs(sdk, args),
            "",
            BuildAndroidToolEnvironment(sdk),
            [&progress](const std::string &line) {
                ParseProgressLine(line, progress);
            }
        );

        const SdkPackageListResult packages = ListSdkPackages(sdk, true);
        const bool ok = std::ranges::all_of(packagePaths, [&](const std::string &path) {
            return std::ranges::any_of(packages.Packages, [&](const SdkPackage &package) {
                return package.Path == path && package.Installed;
            });
        });

        MarkFinished(progress, ok, "Installation completed.", "Installation failed.");
        return ok;
    }

    bool UninstallSdkPackages(
        const SdkInfo &sdk,
        const std::vector<std::string> &packagePaths,
        const std::shared_ptr<SdkOperationProgress> &progress
    ) {
        if (sdk.SdkManagerPath.empty() || packagePaths.empty()) {
            return false;
        }

        if (progress) {
            std::lock_guard lock(progress->Mutex);
            progress->StatusText = "Starting removal...";
            progress->Percent = 0.0F;
            progress->Finished = false;
            progress->Succeeded = false;
        }

        std::vector<std::string> args = {"--uninstall"};
        args.insert(args.end(), packagePaths.begin(), packagePaths.end());

        StreamCommandArgsWithEnv(
            sdk.SdkManagerPath,
            BuildSdkManagerArgs(sdk, args),
            "y\n",
            BuildAndroidToolEnvironment(sdk),
            [&progress](const std::string &line) {
                ParseProgressLine(line, progress);
            }
        );

        const SdkPackageListResult packages = ListSdkPackages(sdk, true);
        const bool ok = std::ranges::none_of(packagePaths, [&](const std::string &path) {
            return std::ranges::any_of(packages.Packages, [&](const SdkPackage &package) {
                return package.Path == path && package.Installed;
            });
        });

        MarkFinished(progress, ok, "Removal completed.", "Removal failed.");
        return ok;
    }
}
