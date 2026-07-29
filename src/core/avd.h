//
// Created by AbdulMuaz Aqeel on 05/04/2026.
//

#ifndef EMU_LAUNCHER_AVD_INFO_H
#define EMU_LAUNCHER_AVD_INFO_H

#include <cstdint>
#include <string>
#include <vector>

#include "sdk.h"

namespace CoreDeck {
    struct AvdInfo {
        std::string Name;
        std::string DisplayName;
        std::string Device;
        std::string ApiLevel;
        std::string Abi;
        std::string SystemImagePath;
        std::string SystemImageVariant;
        std::string SystemImageTagId;
        std::string SystemImageTagDisplay;
        std::vector<std::string> SystemImageTagIds;
        std::vector<std::string> SystemImageTagDisplayNames;
        bool IsGoogleApisImage = false;
        bool IsGooglePlayImage = false;
        bool Supports16KbPageSize = false;
        std::string SdCard;
        std::string RamSize;
        std::string ScreenResolution;
        std::string GpuMode;
        std::string Arch;
        std::string Path;
        std::string SkinName;
    };

    struct AvdCreationData {
        std::string Name;
        std::string DisplayName;
        std::string SystemImagePackagePath;
        std::string DeviceId;
        std::string RamSize;
        std::string SdCardSize;
        std::string GpuMode;
        std::string SkinName;
        std::string SkinPath;
    };

    struct AvdSnapshotInfo {
        std::string Name;
        std::uintmax_t SizeBytes = 0;
        std::int64_t ModifiedEpochSeconds = 0;
    };

    std::vector<AvdInfo> LoadAvds(const std::vector<std::string> &avdNames);

    std::vector<std::string> ListAvdNames(const SdkInfo &sdk);

    bool CreateAvd(const SdkInfo &sdk, const AvdCreationData &data);

    bool DeleteAvd(const SdkInfo &sdk, const std::string &avdName);

    std::vector<AvdSnapshotInfo> ListAvdSnapshots(const std::string &avdPath, std::string *error = nullptr);

    bool DeleteAvdSnapshot(const std::string &avdPath, const std::string &snapshotName, std::string *error = nullptr);

    bool SetAvdDisplayName(const std::string &avdPath, const std::string &displayName);
}

#endif // EMU_LAUNCHER_AVD_INFO_H
