#pragma once

#include <functional>
#include <string>
#include <vector>
#include "sdk.h"

namespace CoreDeck {
    struct ApkReport {
        std::string Path;
        std::string PackageName;
        std::string Label;
        std::string VersionName;
        std::string VersionCode;
        std::string MinSdk;
        std::string TargetSdk;
        std::vector<std::string> Abis;
        std::vector<std::string> Permissions;
        std::string Error;
        std::string Details;
        bool Cancelled = false;
    };

    struct ApkInstallResult {
        bool Succeeded = false;
        bool Cancelled = false;
        std::string Error;
        std::string Details;
    };

    ApkReport ParseApkBadging(const std::string &output);
    ApkReport AnalyzeApk(const SdkInfo &sdk, const std::string &path, const std::function<bool()> &cancel);
    ApkInstallResult InstallApk(const SdkInfo &sdk, const std::string &serial, const std::string &path,
                                const std::function<bool()> &cancel);
}
