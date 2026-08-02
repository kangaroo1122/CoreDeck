//
// Created by AbdulMuaz Aqeel on 19/04/2026.
//

#ifndef COREDECK_SDK_PACKAGES_H
#define COREDECK_SDK_PACKAGES_H

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "sdk.h"
#include "sdk_progress.h"

namespace CoreDeck {
    enum class SdkPackageViewTab : uint8_t {
        Platforms,
        Tools,
    };

    enum class SdkPackageViewMode : uint8_t {
        Summary,
        Details,
    };

    enum class SdkPackageDisplayStatus : uint8_t {
        Installed,
        NotInstalled,
        UpdateAvailable,
    };

    struct SdkPackage {
        std::string Path;
        std::string Version;
        std::string InstalledVersion;
        std::string AvailableVersion;
        std::string Description;
        std::string Location;
        bool Installed = false;
        bool Available = false;
        bool UpdateAvailable = false;
        bool Obsolete = false;
    };

    struct SdkPackageDisplayRow {
        std::string Id;
        std::string Name;
        std::string ApiLevel;
        std::string Revision;
        std::string Version;
        std::string PackagePath;
        std::string Location;
        SdkPackageDisplayStatus Status = SdkPackageDisplayStatus::NotInstalled;
        std::vector<std::string> InstallPackagePaths;
        std::vector<std::string> RemovePackagePaths;
    };

    struct SdkPackageListResult {
        std::vector<SdkPackage> Packages;
        std::string Error;
        bool SdkManagerMissing = false;
    };

    SdkPackageListResult ListSdkPackages(const SdkInfo &sdk, bool includeObsolete = false);

    std::vector<SdkPackageDisplayRow> BuildSdkPackageDisplayRows(
        const std::vector<SdkPackage> &packages,
        SdkPackageViewTab tab,
        SdkPackageViewMode mode
    );

    const char *SdkPackageDisplayStatusText(SdkPackageDisplayStatus status);

    bool IsStableSdkPlatformPackage(const std::string &path);

    std::string SelectLatestSdkPlatformPackage(const std::vector<SdkPackage> &packages);

    bool InstallSdkPackages(
        const SdkInfo &sdk,
        const std::vector<std::string> &packagePaths,
        const std::shared_ptr<SdkOperationProgress> &progress = nullptr
    );

    bool UninstallSdkPackages(
        const SdkInfo &sdk,
        const std::vector<std::string> &packagePaths,
        const std::shared_ptr<SdkOperationProgress> &progress = nullptr
    );

    bool HasSdkManager(const std::string &sdkRoot);
}

#endif // COREDECK_SDK_PACKAGES_H
