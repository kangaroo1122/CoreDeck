//
// Created by AbdulMuaz Aqeel on 19/04/2026.
//

#ifndef COREDECK_SYSTEM_IMAGE_H
#define COREDECK_SYSTEM_IMAGE_H

#include <memory>
#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

#include "sdk.h"

namespace CoreDeck {
    struct DeviceProfile {
        std::string Id;
        std::string Name;
    };

    struct SystemImage {
        std::string ApiLevel;
        std::string Variant;
        std::string Abi;
        std::string PackagePath;
        std::string DisplayName;
    };

    struct RemoteSystemImage {
        std::string PackagePath;
        std::string ApiLevel;
        std::string Variant;
        std::string Abi;
        std::string DisplayName;
        bool IsInstalled = false;
    };

    struct InstallProgressData {
        std::atomic<bool> CancelRequested{false};
        std::mutex Mutex;
        float Percent = 0.0F;
        std::string StatusText;
        std::string DetailText;
        bool Finished = false;
        bool Succeeded = false;
    };

    std::vector<DeviceProfile> ListDeviceProfiles(const SdkInfo &sdk, const std::function<bool()> &shouldCancel = {});

    std::vector<std::string> SupportedSystemImageAbis();

    bool IsSystemImageAbiSupportedOnHost(const std::string &abi);

    std::vector<SystemImage> ListSystemImages(const SdkInfo &sdk);

    std::vector<RemoteSystemImage> ListRemoteSystemImages(
        const SdkInfo &sdk,
        const std::vector<SystemImage> &installedImages,
        const std::function<bool()> &shouldCancel = {}
    );

    bool InstallSystemImage(
        const SdkInfo &sdk,
        const std::string &packagePath,
        const std::shared_ptr<InstallProgressData> &progress = nullptr
    );

    bool UninstallSystemImage(const SdkInfo &sdk, const std::string &packagePath,
                              const std::function<bool()> &shouldCancel = {});

    enum class LicenseStatus : uint8_t {
        AllAccepted,
        SomeUnaccepted,
        CheckFailed,
    };

    struct LicenseCheckResult {
        LicenseStatus Status = LicenseStatus::CheckFailed;
        std::string Output;
    };

    LicenseCheckResult CheckSdkLicensesDetailed(const SdkInfo &sdk, const std::function<bool()> &shouldCancel = {});

    LicenseStatus InterpretSdkLicenseOutput(const std::string &output);

    LicenseStatus CheckSdkLicenses(const SdkInfo &sdk, const std::function<bool()> &shouldCancel = {});

    bool AcceptSdkLicenses(const SdkInfo &sdk, const std::function<bool()> &shouldCancel = {});
}

#endif // COREDECK_SYSTEM_IMAGE_H
