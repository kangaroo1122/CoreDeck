//
// Created by AbdulMuaz Aqeel on 19/04/2026.
//

#ifndef COREDECK_SDK_PACKAGES_H
#define COREDECK_SDK_PACKAGES_H

#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "sdk.h"

namespace CoreDeck {
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

    struct SdkPackageListResult {
        std::vector<SdkPackage> Packages;
        std::string Error;
        bool SdkManagerMissing = false;
    };

    struct SdkOperationProgress {
        std::mutex Mutex;
        float Percent = 0.0F;
        std::string StatusText;
        std::string DetailText;
        bool Finished = false;
        bool Succeeded = false;
    };

    SdkPackageListResult ListSdkPackages(const SdkInfo &sdk, bool includeObsolete = false);

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
