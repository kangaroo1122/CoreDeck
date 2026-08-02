//
// Created by AbdulMuaz Aqeel on 19/04/2026.
//

#ifndef COREDECK_SDK_BOOTSTRAP_H
#define COREDECK_SDK_BOOTSTRAP_H

#include <filesystem>
#include <memory>
#include <string>
#include <system_error>

#include "sdk_packages.h"
#include "sdk_repository.h"

namespace CoreDeck {
    struct SdkBootstrapResult {
        bool Succeeded = false;
        bool Cancelled = false;
        std::string Error;
    };

    std::string CommandLineToolsDownloadUrl();

    CommandLineToolsPackage BundledCommandLineToolsPackage();

    namespace detail { // NOLINT(readability-identifier-naming)
        std::string FormatFilesystemError(
            const std::string &operation,
            const std::filesystem::path &source,
            const std::filesystem::path &destination,
            const std::error_code &error
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
