#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "core/jdk_download.h"
#include "core/jdk.h"
#include "core/paths.h"

using namespace CoreDeck;

namespace {
    std::filesystem::path MakeTempDir(const std::string &prefix) {
        const auto dir = std::filesystem::temp_directory_path() / (prefix + "_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directories(dir);
        return dir;
    }
}

TEST_CASE("JDK LTS releases keep modern supported versions", "[jdk_download][parse]") {
    const std::string json = R"({
        "available_lts_releases": [8, 11, 17, 21, 25],
        "available_releases": [8, 11, 17, 21, 25, 26],
        "most_recent_lts": 25
    })";

    REQUIRE(detail::ParseJdkLtsReleases(json) == std::vector<int>{17, 21, 25});
}

TEST_CASE("Java version parser handles legacy and modern version strings", "[jdk][parse]") {
    REQUIRE(JavaMajorVersionFromText("java version \"1.8.0_491\"") == 8);
    REQUIRE(JavaMajorVersionFromText("openjdk version \"17.0.12\" 2024-07-16") == 17);
    REQUIRE(JavaMajorVersionFromText("openjdk version \"21.0.11+10-LTS\"") == 21);
    REQUIRE(JavaMajorVersionFromText("Unable to read Java version.") == 0);
}

TEST_CASE("Temurin package parser reads the latest archive package", "[jdk_download][parse]") {
    const std::string json = R"([
        {
            "release_name": "jdk-21.0.11+10",
            "vendor": "eclipse",
            "version_data": {
                "major": 21,
                "openjdk_version": "21.0.11+10-LTS"
            },
            "binaries": [
                {
                    "architecture": "aarch64",
                    "package": {
                        "link": "https://github.com/adoptium/temurin21-binaries/releases/download/jdk-21.0.11%2B10/OpenJDK21U-jdk_aarch64_mac_hotspot_21.0.11_10.tar.gz",
                        "name": "OpenJDK21U-jdk_aarch64_mac_hotspot_21.0.11_10.tar.gz",
                        "size": 200030394
                    }
                }
            ]
        }
    ])";

    const auto package = detail::ParseTemurinJdkPackage(json, 21);

    REQUIRE(package.has_value());
    REQUIRE(package->Vendor == JdkVendor::EclipseTemurin);
    REQUIRE(package->FeatureVersion == 21);
    REQUIRE(package->JavaVersion == "21.0.11+10-LTS");
    REQUIRE(package->ArchiveName == "OpenJDK21U-jdk_aarch64_mac_hotspot_21.0.11_10.tar.gz");
    REQUIRE(package->DownloadUrl.find("OpenJDK21U-jdk_aarch64_mac_hotspot_21.0.11_10.tar.gz") != std::string::npos);
    REQUIRE(package->SizeBytes == 200030394);
}

TEST_CASE("Azul package parser prefers regular CA JDK packages", "[jdk_download][parse]") {
    const std::string json = R"([
        {
            "download_url": "https://cdn.azul.com/zulu/bin/zulu21.52.15-ca-jdk21.0.12-linux_musl_aarch64.tar.gz",
            "java_version": [21, 0, 12],
            "name": "zulu21.52.15-ca-jdk21.0.12-linux_musl_aarch64.tar.gz",
            "openjdk_build_number": 8
        },
        {
            "download_url": "https://cdn.azul.com/zulu/bin/zulu21.50.19-ca-crac-jdk21.0.11-linux_aarch64.tar.gz",
            "java_version": [21, 0, 11],
            "name": "zulu21.50.19-ca-crac-jdk21.0.11-linux_aarch64.tar.gz",
            "openjdk_build_number": 10
        },
        {
            "download_url": "https://cdn.azul.com/zulu/bin/zulu21.52.15-ca-jdk21.0.12-linux_aarch64.tar.gz",
            "java_version": [21, 0, 12],
            "name": "zulu21.52.15-ca-jdk21.0.12-linux_aarch64.tar.gz",
            "openjdk_build_number": 8
        }
    ])";

    const auto package = detail::ParseAzulJdkPackage(json, 21);

    REQUIRE(package.has_value());
    REQUIRE(package->Vendor == JdkVendor::AzulZulu);
    REQUIRE(package->FeatureVersion == 21);
    REQUIRE(package->JavaVersion == "21.0.12+8-LTS");
    REQUIRE(package->ArchiveName == "zulu21.52.15-ca-jdk21.0.12-linux_aarch64.tar.gz");
    REQUIRE(package->DownloadUrl.find("linux_aarch64") != std::string::npos);
}

TEST_CASE("Corretto package list uses official latest LTS URLs", "[jdk_download][list]") {
    const JdkPackageListResult result = ListLatestLtsJdkPackages(JdkVendor::AmazonCorretto);

    REQUIRE(result.Error.empty());
    REQUIRE(result.Packages.size() == 3);

    const auto jdk21 = std::ranges::find_if(result.Packages, [](const JdkPackage &package) {
        return package.FeatureVersion == 21;
    });
    REQUIRE(jdk21 != result.Packages.end());
    REQUIRE(jdk21->Vendor == JdkVendor::AmazonCorretto);
    REQUIRE(jdk21->DownloadUrl.find("https://corretto.aws/downloads/latest/amazon-corretto-21-") == 0);
    REQUIRE(jdk21->DownloadUrl.find("-jdk.") != std::string::npos);
}

TEST_CASE("JDK install directory uses the selected invalid JDK path", "[jdk_download][install]") {
    JdkPackage package;
    package.Vendor = JdkVendor::EclipseTemurin;
    package.FeatureVersion = 21;
    package.JavaVersion = "21.0.11+10-LTS";

    const std::filesystem::path tempDir = MakeTempDir("coredeck_requested_jdk_parent");
    const std::filesystem::path requested = tempDir / "coredeck_user_jdk";

    REQUIRE(detail::ResolveJdkInstallDirectory(package, requested.string()) == requested.string());
    REQUIRE(detail::ResolveJdkInstallDirectory(package, "").find("temurin-21.0.11-10-LTS") != std::string::npos);

    std::error_code ec;
    std::filesystem::remove_all(tempDir, ec);
}

TEST_CASE("JDK install directory does not overwrite an existing Java home", "[jdk_download][install]") {
    JdkPackage package;
    package.Vendor = JdkVendor::EclipseTemurin;
    package.FeatureVersion = 21;
    package.JavaVersion = "21.0.11+10-LTS";

    const std::filesystem::path tempDir = MakeTempDir("coredeck_existing_jdk");
    std::filesystem::create_directories(tempDir / "bin");
#if defined(_WIN32)
    const std::filesystem::path javaExecutable = tempDir / "bin" / "java.exe";
#else
    const std::filesystem::path javaExecutable = tempDir / "bin" / "java";
#endif
    std::ofstream(javaExecutable.string()) << "";

    const std::string resolved = detail::ResolveJdkInstallDirectory(package, tempDir.string());
    REQUIRE(resolved != tempDir.string());
    REQUIRE(resolved.find("temurin-21.0.11-10-LTS") != std::string::npos);

    std::error_code ec;
    std::filesystem::remove_all(tempDir, ec);
}

TEST_CASE("JDK requested install directory must be empty or missing", "[jdk_download][install]") {
    const std::filesystem::path tempDir = MakeTempDir("coredeck_non_empty_jdk_target");
    std::ofstream((tempDir / "note.txt").string()) << "keep me";
    const std::filesystem::path emptyDir = MakeTempDir("coredeck_empty_jdk_target");
    const std::filesystem::path filePath = tempDir / "jdk-file";
    std::ofstream(filePath.string()) << "not a directory";

    REQUIRE_FALSE(detail::CanInstallJdkIntoRequestedDirectory(tempDir.string()));
    REQUIRE(detail::CanInstallJdkIntoRequestedDirectory(emptyDir.string()));
    REQUIRE(detail::CanInstallJdkIntoRequestedDirectory((tempDir / "new-jdk").string()));
    REQUIRE_FALSE(detail::CanInstallJdkIntoRequestedDirectory(filePath.string()));

    std::error_code ec;
    std::filesystem::remove_all(tempDir, ec);
    std::filesystem::remove_all(emptyDir, ec);
}

TEST_CASE("JDK extraction lookup finds Java home inside platform bundles", "[jdk_download][install]") {
    const std::filesystem::path tempDir = MakeTempDir("coredeck_jdk_extract");
    const std::filesystem::path javaHome = tempDir / "jdk-21.jdk" / "Contents" / "Home";
    std::filesystem::create_directories(javaHome / "bin");

#if defined(_WIN32)
    const std::filesystem::path javaExecutable = javaHome / "bin" / "java.exe";
#else
    const std::filesystem::path javaExecutable = javaHome / "bin" / "java";
#endif
    std::ofstream(javaExecutable.string()) << "";

    REQUIRE(detail::FindJavaHomeInDirectory(tempDir.string()) == javaHome.string());

    std::error_code ec;
    std::filesystem::remove_all(tempDir, ec);
}
