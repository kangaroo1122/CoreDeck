//
// Created by AbdulMuaz Aqeel on 19/04/2026.
//

#ifndef COREDECK_SDK_BOOTSTRAP_H
#define COREDECK_SDK_BOOTSTRAP_H

#include <cstdint>
#include <memory>
#include <optional>
#include <string>

#include "sdk_packages.h"

namespace CoreDeck {
    struct CommandLineToolsPackage {
        std::string DownloadUrl;
        std::string Sha1;
        std::uintmax_t SizeBytes = 0;
    };

    struct SdkBootstrapResult {
        bool Succeeded = false;
        bool Cancelled = false;
        std::string Error;
    };

    std::string CommandLineToolsDownloadUrl();

    namespace detail { // NOLINT(readability-identifier-naming)
        std::optional<CommandLineToolsPackage> ParseCommandLineToolsPackage(
            const std::string &body,
            const std::string &hostOs,
            const std::string &hostArch = ""
        );

        bool FileMatchesCommandLineToolsPackage(
            const std::string &path,
            const CommandLineToolsPackage &package
        );
    }

    bool CanInstallAndroidSdkIntoDirectory(const std::string &sdkRoot);

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
