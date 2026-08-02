//
// Created by AbdulMuaz Aqeel on 19/04/2026.
//

#include "sdk_packages.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <map>
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

        constexpr std::string_view ANDROID_PACKAGE_PREFIX = "android-";
        constexpr std::string_view SDK_PLATFORM_PREFIX = "platforms;android-";
        constexpr std::string_view SDK_SOURCE_PREFIX = "sources;android-";
        constexpr std::string_view SDK_SYSTEM_IMAGE_PREFIX = "system-images;android-";

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

        bool IsDiagnosticLine(const std::string &line) {
            const std::string lower = LowerCopy(line);
            return lower.starts_with("warning:") || lower.starts_with("error:");
        }

        int SortApiValue(const SdkPackage &package) {
            const std::string &path = package.Path;
            const auto pos = path.find("android-");
            if (pos == std::string::npos) {
                return -1;
            }
            return static_cast<int>(std::strtol(path.c_str() + pos + 8, nullptr, 10));
        }

        std::string PackageVersion(const SdkPackage &package) {
            if (!package.AvailableVersion.empty()) {
                return package.AvailableVersion;
            }
            if (!package.Version.empty()) {
                return package.Version;
            }
            if (!package.InstalledVersion.empty()) {
                return package.InstalledVersion;
            }
            return "";
        }

        std::string PackageVersionLabel(const SdkPackage &package) {
            if (package.UpdateAvailable && !package.InstalledVersion.empty() && !package.AvailableVersion.empty()) {
                return StrConcat(package.InstalledVersion, " -> ", package.AvailableVersion);
            }
            const std::string version = PackageVersion(package);
            return version.empty() ? "-" : version;
        }

        std::vector<int> VersionNumbers(const std::string &version) {
            std::vector<int> numbers;
            std::string token;
            for (const char c: version) {
                if (std::isdigit(static_cast<unsigned char>(c)) != 0) {
                    token.push_back(c);
                } else {
                    if (!token.empty()) {
                        numbers.push_back(static_cast<int>(std::strtol(token.c_str(), nullptr, 10)));
                        token.clear();
                    }
                }
            }
            if (!token.empty()) {
                numbers.push_back(static_cast<int>(std::strtol(token.c_str(), nullptr, 10)));
            }
            return numbers;
        }

        struct ParsedVersion {
            std::vector<int> ReleaseNumbers;
            bool Preview = false;
            int PreviewRank = 0;
            int PreviewNumber = 0;
        };

        ParsedVersion ParseVersion(const std::string &version) {
            struct Marker {
                std::string_view Text;
                int Rank;
            };

            static constexpr Marker MARKERS[] = {
                {.Text = "canary", .Rank = 0},
                {.Text = "alpha", .Rank = 1},
                {.Text = "preview", .Rank = 2},
                {.Text = "beta", .Rank = 2},
                {.Text = "rc", .Rank = 3},
            };

            ParsedVersion parsed;
            const std::string lower = LowerCopy(version);
            std::size_t markerPos = std::string::npos;
            for (const Marker marker: MARKERS) {
                const std::size_t pos = lower.find(marker.Text);
                if (pos != std::string::npos && (markerPos == std::string::npos || pos < markerPos)) {
                    markerPos = pos;
                    parsed.Preview = true;
                    parsed.PreviewRank = marker.Rank;
                }
            }

            const std::string releasePart = markerPos == std::string::npos ? version : version.substr(0, markerPos);
            parsed.ReleaseNumbers = VersionNumbers(releasePart);

            if (markerPos != std::string::npos) {
                const std::vector<int> previewNumbers = VersionNumbers(version.substr(markerPos));
                if (!previewNumbers.empty()) {
                    parsed.PreviewNumber = previewNumbers.front();
                }
            }
            return parsed;
        }

        int CompareVersions(const std::string &left, const std::string &right) {
            const ParsedVersion leftVersion = ParseVersion(left);
            const ParsedVersion rightVersion = ParseVersion(right);
            const std::vector<int> &leftNumbers = leftVersion.ReleaseNumbers;
            const std::vector<int> &rightNumbers = rightVersion.ReleaseNumbers;
            const std::size_t count = std::max(leftNumbers.size(), rightNumbers.size());
            for (std::size_t i = 0; i < count; i++) {
                const int a = i < leftNumbers.size() ? leftNumbers[i] : 0;
                const int b = i < rightNumbers.size() ? rightNumbers[i] : 0;
                if (a != b) {
                    return a < b ? -1 : 1;
                }
            }

            if (leftVersion.Preview != rightVersion.Preview) {
                return leftVersion.Preview ? -1 : 1;
            }
            if (leftVersion.Preview && rightVersion.Preview) {
                if (leftVersion.PreviewRank != rightVersion.PreviewRank) {
                    return leftVersion.PreviewRank < rightVersion.PreviewRank ? -1 : 1;
                }
                if (leftVersion.PreviewNumber != rightVersion.PreviewNumber) {
                    return leftVersion.PreviewNumber < rightVersion.PreviewNumber ? -1 : 1;
                }
            }
            return 0;
        }

        bool IsNewerPackage(const SdkPackage *candidate, const SdkPackage *current) {
            if (current == nullptr) {
                return true;
            }

            const int byVersion = CompareVersions(PackageVersion(*candidate), PackageVersion(*current));
            if (byVersion != 0) {
                return byVersion > 0;
            }
            return candidate->Path > current->Path;
        }

        std::string AndroidApiLevelFromPath(const std::string &path) {
            const auto pos = path.find(ANDROID_PACKAGE_PREFIX);
            if (pos == std::string::npos) {
                return "";
            }
            const auto start = pos + ANDROID_PACKAGE_PREFIX.size();
            const auto end = path.find(';', start);
            return path.substr(start, end == std::string::npos ? std::string::npos : end - start);
        }

        bool IsSdkPlatformComponent(const std::string &path) {
            return path.starts_with(SDK_PLATFORM_PREFIX) ||
                   path.starts_with(SDK_SOURCE_PREFIX) ||
                   path.starts_with(SDK_SYSTEM_IMAGE_PREFIX);
        }

        int PlatformComponentOrder(const std::string &path) {
            if (path.starts_with(SDK_PLATFORM_PREFIX)) {
                return 0;
            }
            if (path.starts_with(SDK_SOURCE_PREFIX)) {
                return 1;
            }
            if (path.starts_with(SDK_SYSTEM_IMAGE_PREFIX)) {
                return 2;
            }
            return 3;
        }

        std::string PlatformPackageName(const SdkPackage &package) {
            if (!package.Description.empty()) {
                return package.Description;
            }
            const std::string api = AndroidApiLevelFromPath(package.Path);
            return api.empty() ? package.Path : StrConcat("Android SDK Platform ", api);
        }

        SdkPackageDisplayStatus PackageStatus(const SdkPackage &package) {
            if (package.UpdateAvailable) {
                return SdkPackageDisplayStatus::UpdateAvailable;
            }
            if (package.Installed) {
                return SdkPackageDisplayStatus::Installed;
            }
            return SdkPackageDisplayStatus::NotInstalled;
        }

        SdkPackageDisplayRow DetailRowForPackage(const SdkPackage &package, const bool platformDetails) {
            SdkPackageDisplayRow row;
            row.Id = StrConcat("package:", package.Path);
            row.Name = platformDetails ? PlatformPackageName(package) : (package.Description.empty() ? package.Path : package.Description);
            row.ApiLevel = platformDetails ? AndroidApiLevelFromPath(package.Path) : "";
            row.Revision = platformDetails ? PackageVersionLabel(package) : "";
            row.Version = platformDetails ? "" : PackageVersionLabel(package);
            row.PackagePath = package.Path;
            row.Location = package.Location;
            row.Status = PackageStatus(package);
            if (!package.Installed || package.UpdateAvailable) {
                row.InstallPackagePaths.push_back(package.Path);
            }
            if (package.Installed) {
                row.RemovePackagePaths.push_back(package.Path);
            }
            return row;
        }

        std::vector<SdkPackageDisplayRow> BuildPlatformRows(
            const std::vector<SdkPackage> &packages,
            const SdkPackageViewMode mode
        ) {
            std::vector<SdkPackageDisplayRow> rows;
            rows.reserve(packages.size());

            for (const SdkPackage &package: packages) {
                if (mode == SdkPackageViewMode::Summary && package.Path.starts_with(SDK_PLATFORM_PREFIX)) {
                    SdkPackageDisplayRow row;
                    row.Id = StrConcat("platform:", AndroidApiLevelFromPath(package.Path));
                    row.Name = PlatformPackageName(package);
                    row.ApiLevel = AndroidApiLevelFromPath(package.Path);
                    row.Revision = PackageVersionLabel(package);
                    row.PackagePath = package.Path;
                    row.Location = package.Location;
                    row.Status = PackageStatus(package);
                    if (!package.Installed || package.UpdateAvailable) {
                        row.InstallPackagePaths.push_back(package.Path);
                    }
                    if (package.Installed) {
                        row.RemovePackagePaths.push_back(package.Path);
                    }
                    rows.push_back(std::move(row));
                } else if (mode == SdkPackageViewMode::Details && IsSdkPlatformComponent(package.Path)) {
                    rows.push_back(DetailRowForPackage(package, true));
                }
            }

            std::ranges::sort(rows, [](const SdkPackageDisplayRow &a, const SdkPackageDisplayRow &b) {
                const int apiA = static_cast<int>(std::strtol(a.ApiLevel.c_str(), nullptr, 10));
                const int apiB = static_cast<int>(std::strtol(b.ApiLevel.c_str(), nullptr, 10));
                if (apiA != apiB) {
                    return apiA > apiB;
                }
                if (a.ApiLevel != b.ApiLevel) {
                    return a.ApiLevel > b.ApiLevel;
                }
                const int orderA = PlatformComponentOrder(a.PackagePath);
                const int orderB = PlatformComponentOrder(b.PackagePath);
                if (orderA != orderB) {
                    return orderA < orderB;
                }
                return a.Name < b.Name;
            });
            return rows;
        }

        std::string ToolGroupKey(const std::string &path) {
            for (const std::string_view prefix: {
                     std::string_view("build-tools;"),
                     std::string_view("cmdline-tools;"),
                     std::string_view("cmake;"),
                     std::string_view("ndk;"),
                 }) {
                if (path.starts_with(prefix)) {
                    return std::string(prefix.substr(0, prefix.size() - 1));
                }
            }
            return path;
        }

        std::string ToolGroupName(const std::string &key, const SdkPackage &package) {
            if (key == "build-tools") {
                return "Android SDK Build-Tools";
            }
            if (key == "cmdline-tools") {
                return "Android SDK Command-line Tools (latest)";
            }
            if (key == "cmake") {
                return "CMake";
            }
            if (key == "ndk") {
                return "NDK (Side by side)";
            }
            return package.Description.empty() ? package.Path : package.Description;
        }

        std::vector<SdkPackageDisplayRow> BuildToolSummaryRows(const std::vector<SdkPackage> &packages) {
            std::map<std::string, std::vector<const SdkPackage *>> groups;
            for (const SdkPackage &package: packages) {
                if (IsSdkPlatformComponent(package.Path)) {
                    continue;
                }
                groups[ToolGroupKey(package.Path)].push_back(&package);
            }

            std::vector<SdkPackageDisplayRow> rows;
            rows.reserve(groups.size());
            for (const auto &[key, groupPackages]: groups) {
                const SdkPackage *latest = nullptr;
                bool anyInstalled = false;
                for (const SdkPackage *package: groupPackages) {
                    if (IsNewerPackage(package, latest)) {
                        latest = package;
                    }
                    anyInstalled = anyInstalled || package->Installed;
                }
                if (latest == nullptr) {
                    continue;
                }

                SdkPackageDisplayRow row;
                row.Id = StrConcat("tool:", key);
                row.Name = ToolGroupName(key, *latest);
                row.Version = PackageVersionLabel(*latest);
                row.PackagePath = latest->Path;
                row.Location = latest->Location;
                row.Status = anyInstalled && latest->Installed && !latest->UpdateAvailable
                                 ? SdkPackageDisplayStatus::Installed
                                 : (anyInstalled ? SdkPackageDisplayStatus::UpdateAvailable : SdkPackageDisplayStatus::NotInstalled);

                if (!latest->Installed || latest->UpdateAvailable) {
                    row.InstallPackagePaths.push_back(latest->Path);
                }
                for (const SdkPackage *package: groupPackages) {
                    if (package->Installed) {
                        row.RemovePackagePaths.push_back(package->Path);
                    }
                }
                rows.push_back(std::move(row));
            }

            std::ranges::sort(rows, [](const SdkPackageDisplayRow &a, const SdkPackageDisplayRow &b) {
                return a.Name < b.Name;
            });
            return rows;
        }

        std::vector<SdkPackageDisplayRow> BuildToolDetailRows(const std::vector<SdkPackage> &packages) {
            std::vector<SdkPackageDisplayRow> rows;
            rows.reserve(packages.size());
            for (const SdkPackage &package: packages) {
                if (!IsSdkPlatformComponent(package.Path)) {
                    rows.push_back(DetailRowForPackage(package, false));
                }
            }

            std::ranges::sort(rows, [](const SdkPackageDisplayRow &a, const SdkPackageDisplayRow &b) {
                const std::string groupA = ToolGroupKey(a.PackagePath);
                const std::string groupB = ToolGroupKey(b.PackagePath);
                if (groupA != groupB) {
                    return ToolGroupName(groupA, SdkPackage{.Path = a.PackagePath, .Description = a.Name}) <
                           ToolGroupName(groupB, SdkPackage{.Path = b.PackagePath, .Description = b.Name});
                }
                const int byVersion = CompareVersions(a.Version, b.Version);
                if (byVersion != 0) {
                    return byVersion > 0;
                }
                return a.PackagePath < b.PackagePath;
            });
            return rows;
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
            if (progress->CancelRequested.load()) {
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

            ReportSdkProgress(
                progress,
                static_cast<float>(pct) / 100.0F,
                description.empty() ? "Installing SDK packages..." : description,
                line
            );
        }

        void MarkFinished(const std::shared_ptr<SdkOperationProgress> &progress, const bool ok, const char *success, const char *failure) {
            if (!progress) {
                return;
            }

            if (ok) {
                ReportSdkProgress(progress, 1.0F, success);
            }
            std::lock_guard lock(progress->Mutex);
            progress->Finished = true;
            progress->Succeeded = ok;
            progress->StatusText = ok ? success : failure;
        }

        bool IsCancelRequested(const std::shared_ptr<SdkOperationProgress> &progress) {
            return progress && progress->CancelRequested.load();
        }

        void MarkCancelled(const std::shared_ptr<SdkOperationProgress> &progress) {
            if (!progress) {
                return;
            }

            std::lock_guard lock(progress->Mutex);
            progress->Finished = true;
            progress->Succeeded = false;
            progress->StatusText = "Cancelled.";
            progress->DetailText.clear();
        }
    }

    std::vector<SdkPackageDisplayRow> BuildSdkPackageDisplayRows(
        const std::vector<SdkPackage> &packages,
        const SdkPackageViewTab tab,
        const SdkPackageViewMode mode
    ) {
        if (tab == SdkPackageViewTab::Platforms) {
            return BuildPlatformRows(packages, mode);
        }
        return mode == SdkPackageViewMode::Summary ? BuildToolSummaryRows(packages) : BuildToolDetailRows(packages);
    }

    const char *SdkPackageDisplayStatusText(const SdkPackageDisplayStatus status) {
        switch (status) {
            case SdkPackageDisplayStatus::Installed:
                return "Installed";
            case SdkPackageDisplayStatus::NotInstalled:
                return "Not installed";
            case SdkPackageDisplayStatus::UpdateAvailable:
                return "Update available";
        }
        return "Not installed";
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
        if (!path.starts_with(SDK_PLATFORM_PREFIX)) {
            return false;
        }

        const std::string version = path.substr(SDK_PLATFORM_PREFIX.size());
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
        const SdkPackage *latest = nullptr;
        for (const SdkPackage &package: packages) {
            if (!IsStableSdkPlatformPackage(package.Path)) {
                continue;
            }
            if (latest == nullptr) {
                latest = &package;
                continue;
            }

            const std::string candidateApi = package.Path.substr(SDK_PLATFORM_PREFIX.size());
            const std::string latestApi = latest->Path.substr(SDK_PLATFORM_PREFIX.size());
            const int byApi = CompareVersions(candidateApi, latestApi);
            if (byApi > 0 || (byApi == 0 && IsNewerPackage(&package, latest))) {
                latest = &package;
            }
        }
        return latest == nullptr ? std::string{} : latest->Path;
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

        std::string output;
        const bool commandOk = StreamCommandArgsWithEnvCancelable(
            sdk.SdkManagerPath,
            BuildSdkManagerArgs(sdk, args),
            "",
            BuildAndroidToolEnvironment(sdk),
            [&output](const std::string &line) {
                output += line;
                output.push_back('\n');
            },
            {}
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
            if (IsDiagnosticLine(line)) {
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
            if (section == ListSection::None) {
                continue;
            }

            const std::vector<std::string> columns = SplitColumns(line);
            if (columns.size() < 2 || columns[0].empty()) {
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

        if (!commandOk) {
            result.Packages.clear();
            result.Error = firstDiagnostic.empty()
                               ? "Could not fetch SDK package lists. Check your network connection."
                               : firstDiagnostic;
        } else if (result.Packages.empty()) {
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
        if (IsCancelRequested(progress)) {
            MarkCancelled(progress);
            return false;
        }

        ReportSdkProgress(progress, 0.0F, "Starting installation...");

        std::vector<std::string> args = {"--install"};
        args.insert(args.end(), packagePaths.begin(), packagePaths.end());

        const bool completed = StreamCommandArgsWithEnvCancelable(
            sdk.SdkManagerPath,
            BuildSdkManagerArgs(sdk, args),
            "",
            BuildAndroidToolEnvironment(sdk),
            [&progress](const std::string &line) {
                ParseProgressLine(line, progress);
            },
            [&progress] {
                return IsCancelRequested(progress);
            }
        );
        if (IsCancelRequested(progress)) {
            MarkCancelled(progress);
            return false;
        }
        if (!completed) {
            MarkFinished(progress, false, "Installation completed.", "Installation failed.");
            return false;
        }

        const SdkPackageListResult packages = ListSdkPackages(sdk, true);
        if (IsCancelRequested(progress)) {
            MarkCancelled(progress);
            return false;
        }
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
        if (IsCancelRequested(progress)) {
            MarkCancelled(progress);
            return false;
        }

        ReportSdkProgress(progress, 0.0F, "Starting removal...");

        std::vector<std::string> args = {"--uninstall"};
        args.insert(args.end(), packagePaths.begin(), packagePaths.end());

        const bool completed = StreamCommandArgsWithEnvCancelable(
            sdk.SdkManagerPath,
            BuildSdkManagerArgs(sdk, args),
            "y\n",
            BuildAndroidToolEnvironment(sdk),
            [&progress](const std::string &line) {
                ParseProgressLine(line, progress);
            },
            [&progress] {
                return IsCancelRequested(progress);
            }
        );
        if (IsCancelRequested(progress)) {
            MarkCancelled(progress);
            return false;
        }
        if (!completed) {
            MarkFinished(progress, false, "Removal completed.", "Removal failed.");
            return false;
        }

        const SdkPackageListResult packages = ListSdkPackages(sdk, true);
        if (IsCancelRequested(progress)) {
            MarkCancelled(progress);
            return false;
        }
        const bool ok = std::ranges::none_of(packagePaths, [&](const std::string &path) {
            return std::ranges::any_of(packages.Packages, [&](const SdkPackage &package) {
                return package.Path == path && package.Installed;
            });
        });

        MarkFinished(progress, ok, "Removal completed.", "Removal failed.");
        return ok;
    }
}
