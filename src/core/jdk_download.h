//
// Created by kangaroo. on 26/07/2026.
//

#ifndef COREDECK_JDK_DOWNLOAD_H
#define COREDECK_JDK_DOWNLOAD_H

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "sdk_packages.h"

namespace CoreDeck {
    enum class JdkVendor : uint8_t {
        EclipseTemurin,
        AzulZulu,
        AmazonCorretto,
    };

    struct JdkPackage {
        JdkVendor Vendor = JdkVendor::EclipseTemurin;
        int FeatureVersion = 0;
        std::string JavaVersion;
        std::string ArchiveName;
        std::string DownloadUrl;
        std::uintmax_t SizeBytes = 0;
    };

    struct JdkPackageListResult {
        std::vector<JdkPackage> Packages;
        std::string Error;
    };

    struct JdkInstallResult {
        bool Succeeded = false;
        bool Cancelled = false;
        std::string JavaHomePath;
        std::string Error;
    };

    const char *JdkVendorDisplayName(JdkVendor vendor);

    const char *JdkVendorId(JdkVendor vendor);

    std::string DefaultJdkInstallRoot();

    bool CanInstallJdkIntoRequestedDirectory(const std::string &requestedJavaHomePath);

    JdkPackageListResult ListLatestLtsJdkPackages(JdkVendor vendor);

    JdkInstallResult InstallJdkPackage(
        const JdkPackage &package,
        const std::shared_ptr<SdkOperationProgress> &progress = nullptr,
        const std::string &requestedJavaHomePath = ""
    );

    namespace detail { // NOLINT(readability-identifier-naming)
        std::vector<int> ParseJdkLtsReleases(const std::string &body);

        std::optional<JdkPackage> ParseTemurinJdkPackage(const std::string &body, int featureVersion);

        std::optional<JdkPackage> ParseAzulJdkPackage(const std::string &body, int featureVersion);

        std::string ResolveJdkInstallDirectory(const JdkPackage &package, const std::string &requestedJavaHomePath);

        bool CanInstallJdkIntoRequestedDirectory(const std::string &requestedJavaHomePath);

        std::string FindJavaHomeInDirectory(const std::string &directory);
    }
}

#endif // COREDECK_JDK_DOWNLOAD_H
