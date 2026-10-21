//
// Created by AbdulMuaz Aqeel on 19/04/2026.
//

#include <algorithm>
#include <chrono>
#include <stdexcept>
#include <cstddef>
#include <cctype>
#include <cstdlib>
#include <unordered_map>
#include <unordered_set>
#include <filesystem>
#include <sstream>
#include <vector>

#include "system_image.h"
#include "paths.h"
#include "process.h"
#include "sdk_progress.h"
#include "utilities.h"
#include "log.h"

namespace CoreDeck {
    namespace {
        void ParseProgressLine(const std::string &line, const std::shared_ptr<InstallProgressData> &progress) {
            if (!progress) {
                return;
            }

            const auto parsed = ParseSdkManagerProgressLine(line);
            if (parsed.Percent < 0) {
                if (!line.empty()) {
                    std::lock_guard lock(progress->Mutex);
                    progress->DetailText = line;
                }
                return;
            }

            std::lock_guard lock(progress->Mutex);
            progress->Percent = static_cast<float>(parsed.Percent) / 100.0F;
            if (!parsed.Status.empty()) {
                progress->StatusText = parsed.Status;
            }
            progress->DetailText = line;
        }

        bool IsDeviceProfileId(const std::string &line) {
            return !line.empty() && std::ranges::all_of(line, [](const char c) {
                       return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-' || c == '.';
                   });
        }

        std::string CanonicalImagePackagePath(std::string path) {
            if (path.starts_with("system-images/")) {
                std::ranges::replace(path, '/', ';');
            }
            return path;
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

        bool RunSdkMaintenanceCommand(const SdkInfo &sdk, const std::vector<std::string> &args,
                                      const std::string &input, std::string &output,
                                      const std::function<bool()> &shouldCancel) {
            if (shouldCancel && shouldCancel()) {
                return false;
            }
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(120);
            bool timedOut = false;
            const bool succeeded = StreamCommandArgsWithEnvCancelable(
                sdk.SdkManagerPath, BuildSdkManagerArgs(sdk, args), input, BuildAndroidToolEnvironment(sdk),
                [&output](const std::string &line) { output += line + '\n'; },
                [&] {
                    timedOut = std::chrono::steady_clock::now() >= deadline;
                    return timedOut || (shouldCancel && shouldCancel());
                });
            if (timedOut) {
                Log::Error("SDK command timed out: ", args.front());
            } else if (!succeeded && !(shouldCancel && shouldCancel())) {
                Log::Error("SDK command failed: ", args.front(), ": ", output);
            }
            return succeeded;
        }

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

        std::string AbiDisplayName(const std::string &abi) {
            if (abi == "arm64-v8a") {
                return "ARM 64 v8a";
            }
            if (abi == "armeabi-v7a") {
                return "ARM EABI v7a";
            }
            if (abi == "x86_64") {
                return "Intel x86_64 Atom";
            }
            if (abi == "x86") {
                return "Intel x86 Atom";
            }
            return abi;
        }

        std::string BaseDisplayNameForVariant(const std::string &variant) {
            const std::string lower = LowerCopy(variant);
            if (lower.find("android-automotive-playstore") != std::string::npos) {
                return "Android Automotive with Google Play";
            }
            if (lower.find("android-automotive") != std::string::npos) {
                return "Android Automotive with Google APIs";
            }
            if (lower.find("android-wear") != std::string::npos) {
                return "Wear OS";
            }
            if (lower.find("android-tv") != std::string::npos) {
                return "Android TV";
            }
            if (lower.find("google-tv") != std::string::npos) {
                return "Google TV";
            }
            if (lower.find("android-desktop") != std::string::npos) {
                return "Desktop";
            }
            if (lower.find("android-xr-preview-playstore") != std::string::npos) {
                return "Google Play XR Preview";
            }
            if (lower.find("google-xr") != std::string::npos) {
                return "Google Play XR";
            }
            if (lower.find("google_apis_playstore") != std::string::npos) {
                return "Google Play";
            }
            if (lower.find("google_apis") != std::string::npos) {
                return "Google APIs";
            }
            if (lower.find("google_atd") != std::string::npos) {
                return "Google APIs ATD";
            }
            if (lower.find("aosp_atd") != std::string::npos) {
                return "AOSP ATD";
            }
            if (lower == "default") {
                return "";
            }

            std::string fallback = variant;
            std::ranges::replace(fallback, '_', ' ');
            std::ranges::replace(fallback, '-', ' ');
            if (!fallback.empty()) {
                fallback[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(fallback[0])));
            }
            return fallback;
        }

        std::string BuildSystemImageDisplayName(
            const std::string &apiLevel,
            const std::string &variant,
            const std::string &abi
        ) {
            const std::string lowerVariant = LowerCopy(variant);
            const std::string abiLabel = AbiDisplayName(abi);
            std::string prefix;
            if (lowerVariant.find("ps16k") != std::string::npos ||
                lowerVariant.find("16kb") != std::string::npos ||
                lowerVariant.find("16k") != std::string::npos) {
                prefix = "16 KB Page Size ";
            }

            std::string base = BaseDisplayNameForVariant(variant);
            if (lowerVariant.find("tablet") != std::string::npos && LowerCopy(base).find("tablet") == std::string::npos) {
                base = base.empty() ? "Tablet" : StrConcat(base, " Tablet");
            }

            const std::string suffix = lowerVariant.find("signed") != std::string::npos ? " (signed)" : "";
            if (base.empty()) {
                if (!abiLabel.empty()) {
                    return StrConcat(prefix, abiLabel, " System Image", suffix);
                }
                return apiLevel.empty() ? "System Image" : StrConcat("Android ", apiLevel, " System Image", suffix);
            }
            if (abiLabel.empty()) {
                return StrConcat(prefix, base, " System Image", suffix);
            }
            return StrConcat(prefix, base, " ", abiLabel, " System Image", suffix);
        }
    }

    std::vector<DeviceProfile> ListDeviceProfiles(const SdkInfo &sdk, const std::function<bool()> &shouldCancel) {
        std::vector<DeviceProfile> devices;

        if (sdk.AvdManagerPath.empty()) {
            return devices;
        }

        if (shouldCancel && shouldCancel()) {
            return devices;
        }
        std::string output;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
        const bool ok = StreamCommandArgsWithEnvCancelable(
            sdk.AvdManagerPath,
            {"list", "device", "-c"},
            "",
            BuildAndroidToolEnvironment(sdk),
            [&output](const std::string &line) { output += line + '\n'; },
            [&] { return std::chrono::steady_clock::now() >= deadline || (shouldCancel && shouldCancel()); }
        );
        if (!ok) {
            if (shouldCancel && shouldCancel()) {
                return devices;
            }
            Log::Error("Device profile query failed: ", output);
            throw std::runtime_error("Could not load device profiles. The command failed or timed out.");
        }
        std::istringstream stream(output);
        std::string line;
        while (std::getline(stream, line)) {
            if (line.empty()) {
                continue;
            }
            while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) {
                line.pop_back();
            }
            if (!IsDeviceProfileId(line)) {
                continue;
            }

            DeviceProfile device;
            device.Id = line;
            device.Name = line;
            std::ranges::replace(device.Name, '_', ' ');
            if (!device.Name.empty()) {
                device.Name[0] = static_cast<char>(std::toupper(device.Name[0]));
            }
            devices.push_back(device);
        }

        return devices;
    }

    std::vector<std::string> SupportedSystemImageAbis() {
#if defined(__aarch64__) || defined(_M_ARM64)
        return {"arm64-v8a"};
#elif defined(__arm__) || defined(_M_ARM)
        return {"armeabi-v7a"};
#elif defined(__x86_64__) || defined(_M_X64) || defined(__i386) || defined(_M_IX86)
        return {"x86_64", "x86"};
#else
        return {};
#endif
    }

    bool IsSystemImageAbiSupportedOnHost(const std::string &abi) {
        const std::vector<std::string> supportedAbis = SupportedSystemImageAbis();
        return supportedAbis.empty() || std::ranges::find(supportedAbis, abi) != supportedAbis.end();
    }

    std::vector<SystemImage> ListSystemImages(const SdkInfo &sdk) {
        std::vector<SystemImage> images;
        if (sdk.SdkPath.empty()) {
            return images;
        }

        const std::string sysImgRoot = Paths::JoinPaths({sdk.SdkPath, "system-images"});
        if (!std::filesystem::exists(sysImgRoot)) {
            return images;
        }

        // Structure: system-images/android-XX/variant/abi/
        for (const auto &apiEntry: std::filesystem::directory_iterator(sysImgRoot)) {
            if (!apiEntry.is_directory()) {
                continue;
            }
            const std::string apiDirName = apiEntry.path().filename().string();

            std::string apiLevel;
            if (apiDirName.starts_with("android-")) {
                apiLevel = apiDirName.substr(8);
            } else {
                continue;
            }

            for (const auto &variantEntry: std::filesystem::directory_iterator(apiEntry.path())) {
                if (!variantEntry.is_directory()) {
                    continue;
                }
                const std::string variant = variantEntry.path().filename().string();

                for (const auto &abiEntry: std::filesystem::directory_iterator(variantEntry.path())) {
                    if (!abiEntry.is_directory()) {
                        continue;
                    }
                    const std::string abi = abiEntry.path().filename().string();
                    if (!IsSystemImageAbiSupportedOnHost(abi)) {
                        continue;
                    }

                    const std::string sysImg = Paths::JoinPaths({abiEntry.path().string(), "system.img"});
                    if (!std::filesystem::exists(sysImg)) {
                        continue;
                    }

                    SystemImage img;
                    img.ApiLevel = apiLevel;
                    img.Variant = variant;
                    img.Abi = abi;
                    img.PackagePath = StrConcat("system-images;", apiDirName, ";", variant, ";", abi);
                    img.DisplayName = BuildSystemImageDisplayName(apiLevel, variant, abi);
                    images.push_back(img);
                }
            }
        }

        // Sort by API level descending (newest first)
        std::ranges::sort(images, [](const SystemImage &a, const SystemImage &b) {
            const int apiA = static_cast<int>(std::strtol(a.ApiLevel.c_str(), nullptr, 10));
            const int apiB = static_cast<int>(std::strtol(b.ApiLevel.c_str(), nullptr, 10));
            return apiA > apiB;
        });

        return images;
    }

    std::vector<RemoteSystemImage> ListRemoteSystemImages(
        const SdkInfo &sdk,
        const std::vector<SystemImage> &installedImages,
        const std::function<bool()> &shouldCancel
    ) {
        std::vector<RemoteSystemImage> results;
        if (sdk.SdkManagerPath.empty()) {
            return results;
        }

        std::string output;
        if (!RunSdkMaintenanceCommand(sdk, {"--list"}, "", output, shouldCancel)) {
            if (shouldCancel && shouldCancel()) {
                return results;
            }
            throw std::runtime_error("Could not load system images. The command failed or timed out.");
        }

        std::unordered_map<std::string, bool> installedSet;
        for (const auto &img: installedImages) {
            installedSet[CanonicalImagePackagePath(img.PackagePath)] = true;
        }

        std::unordered_set<std::string> seenPackages;
        std::istringstream stream(output);
        std::string line;
        while (std::getline(stream, line)) {
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }

            auto start = line.find_first_not_of(" \t");
            if (start == std::string::npos) {
                continue;
            }
            line = line.substr(start);

            if (!line.starts_with("system-images;") && !line.starts_with("system-images/")) {
                continue;
            }

            const std::vector<std::string> columns = SplitColumns(line);
            const std::string packagePath = CanonicalImagePackagePath(line.substr(0, line.find_first_of(" \t|")));
            if (packagePath.empty() || !seenPackages.insert(packagePath).second) {
                continue;
            }

            std::vector<std::string> parts;
            std::istringstream partStream(packagePath);
            std::string part;
            while (std::getline(partStream, part, ';')) {
                parts.push_back(part);
            }
            if (parts.size() != 4 || parts[1].size() <= 8 || parts[2].empty() || parts[3].empty()) {
                continue;
            }

            RemoteSystemImage img;
            img.PackagePath = packagePath;

            if (parts[1].starts_with("android-")) {
                img.ApiLevel = parts[1].substr(8);
            } else {
                continue;
            }

            img.Variant = parts[2];
            img.Abi = parts[3];
            if (!IsSystemImageAbiSupportedOnHost(img.Abi)) {
                continue;
            }

            img.IsInstalled = installedSet.contains(packagePath);
            img.DisplayName = columns.size() >= 3 && !columns[2].empty()
                                  ? columns[2]
                                  : BuildSystemImageDisplayName(img.ApiLevel, img.Variant, img.Abi);

            results.push_back(std::move(img));
        }

        std::ranges::sort(results, [](const RemoteSystemImage &a, const RemoteSystemImage &b) {
            const int apiA = static_cast<int>(std::strtol(a.ApiLevel.c_str(), nullptr, 10));
            const int apiB = static_cast<int>(std::strtol(b.ApiLevel.c_str(), nullptr, 10));
            if (apiA != apiB) {
                return apiA > apiB;
            }
            if (a.IsInstalled != b.IsInstalled) {
                return a.IsInstalled;
            }
            return a.DisplayName < b.DisplayName;
        });

        return results;
    }

    bool InstallSystemImage(
        const SdkInfo &sdk,
        const std::string &packagePath,
        const std::shared_ptr<InstallProgressData> &progress
    ) {
        if (sdk.SdkManagerPath.empty() || packagePath.empty()) {
            return false;
        }

        if (progress) {
            std::lock_guard lock(progress->Mutex);
            progress->StatusText = "Starting download...";
            progress->Percent = 0.0F;
        }

        const bool installed = !(progress && progress->CancelRequested.load()) && StreamCommandArgsWithEnvCancelable(
            sdk.SdkManagerPath,
            BuildSdkManagerArgs(sdk, {"--install", CanonicalImagePackagePath(packagePath)}),
            "",
            BuildAndroidToolEnvironment(sdk),
            [&progress](const std::string &line) {
                ParseProgressLine(line, progress);
            },
            [progress] {
                return progress && progress->CancelRequested.load();
            }
        );

        // Verify
        std::string fsPath = packagePath;
        std::ranges::replace(fsPath, ';', '/');
        const std::string sysImg = Paths::JoinPaths({sdk.SdkPath, fsPath, "system.img"});
        std::error_code error;
        const bool cancelled = progress && progress->CancelRequested.load();
        const bool ok = installed && !cancelled && std::filesystem::exists(sysImg, error);

        if (progress) {
            std::lock_guard lock(progress->Mutex);
            progress->Finished = true;
            progress->Succeeded = ok;
            progress->Percent = ok ? 1.0F : progress->Percent;
            progress->StatusText = cancelled ? "Cancelled." : ok ? "Installation Completed!" : "Installation Failed!";
        }

        return ok;
    }

    bool UninstallSystemImage(const SdkInfo &sdk, const std::string &packagePath,
                              const std::function<bool()> &shouldCancel) {
        if (sdk.SdkManagerPath.empty() || packagePath.empty()) {
            return false;
        }

        std::string output;
        const bool removed = RunSdkMaintenanceCommand(
            sdk, {"--uninstall", CanonicalImagePackagePath(packagePath)}, "y\n", output, shouldCancel);

        std::string fsPath = packagePath;
        std::ranges::replace(fsPath, ';', '/');
        const std::string sysImg = Paths::JoinPaths({sdk.SdkPath, fsPath, "system.img"});
        std::error_code error;
        const bool exists = std::filesystem::exists(sysImg, error);
        return removed && !error && !exists;
    }

    LicenseCheckResult CheckSdkLicensesDetailed(const SdkInfo &sdk, const std::function<bool()> &shouldCancel) {
        if (sdk.SdkManagerPath.empty()) {
            return {.Status = LicenseStatus::CheckFailed, .Output = "SDK Manager was not found."};
        }

        std::string output;
        const bool ok = RunSdkMaintenanceCommand(sdk, {"--licenses"}, "N\n", output, shouldCancel);
        return {.Status = ok ? InterpretSdkLicenseOutput(output) : LicenseStatus::CheckFailed, .Output = output};
    }

    LicenseStatus InterpretSdkLicenseOutput(const std::string &output) {
        // Acceptance output can include the initial prompt before the final success.
        if (output.find("All SDK package licenses accepted") != std::string::npos ||
            output.find("The --licenses option is no longer needed") != std::string::npos) {
            return LicenseStatus::AllAccepted;
        }
        if (output.find("licenses not accepted") != std::string::npos ||
            output.find("license not accepted") != std::string::npos) {
            return LicenseStatus::SomeUnaccepted;
        }
        return LicenseStatus::CheckFailed;
    }

    LicenseStatus CheckSdkLicenses(const SdkInfo &sdk, const std::function<bool()> &shouldCancel) {
        return CheckSdkLicensesDetailed(sdk, shouldCancel).Status;
    }

    bool AcceptSdkLicenses(const SdkInfo &sdk, const std::function<bool()> &shouldCancel) {
        if (sdk.SdkManagerPath.empty()) {
            return false;
        }

        std::string yes;
        yes.reserve(static_cast<size_t>(64 * 2));
        for (int i = 0; i < 64; ++i) {
            yes += "y\n";
        }

        std::string output;
        const bool ok = RunSdkMaintenanceCommand(sdk, {"--licenses"}, yes, output, shouldCancel);
        return ok && InterpretSdkLicenseOutput(output) == LicenseStatus::AllAccepted;
    }
}
