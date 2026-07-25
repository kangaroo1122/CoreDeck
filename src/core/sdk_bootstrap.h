//
// Created by AbdulMuaz Aqeel on 19/04/2026.
//

#ifndef COREDECK_SDK_BOOTSTRAP_H
#define COREDECK_SDK_BOOTSTRAP_H

#include <memory>
#include <string>

#include "sdk_packages.h"

namespace CoreDeck {
    struct SdkBootstrapResult {
        bool Succeeded = false;
        std::string Error;
    };

    std::string CommandLineToolsDownloadUrl();

    SdkInfo BuildSdkInfoFromSdkRoot(
        const std::string &sdkRoot,
        const std::string &javaHomePath = ""
    );

    SdkBootstrapResult BootstrapCommandLineTools(
        const std::string &sdkRoot,
        const std::shared_ptr<SdkOperationProgress> &progress = nullptr
    );

    SdkBootstrapResult BootstrapBaseAndroidSdk(
        const std::string &sdkRoot,
        const std::string &javaHomePath,
        const std::shared_ptr<SdkOperationProgress> &progress = nullptr
    );

    SdkBootstrapResult InstallBaseSdkPackages(
        const SdkInfo &sdk,
        const std::shared_ptr<SdkOperationProgress> &progress = nullptr
    );
}

#endif // COREDECK_SDK_BOOTSTRAP_H
