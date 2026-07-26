#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

#include "core/sdk_bootstrap.h"

using namespace CoreDeck;

namespace {
    std::filesystem::path MakeTempDir(const std::string &prefix) {
        const auto dir = std::filesystem::temp_directory_path() / (prefix + "_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directories(dir);
        return dir;
    }
}

TEST_CASE("Android SDK bootstrap directory must be empty missing or contain sdkmanager", "[sdk_bootstrap][install]") {
    const std::filesystem::path nonEmptyDir = MakeTempDir("coredeck_non_empty_sdk_target");
    std::ofstream((nonEmptyDir / "note.txt").string()) << "keep me";

    const std::filesystem::path emptyDir = MakeTempDir("coredeck_empty_sdk_target");
    const std::filesystem::path platformToolsDir = MakeTempDir("coredeck_platform_tools_target");
    std::filesystem::create_directories(platformToolsDir / "platform-tools");
    const std::filesystem::path fileMarkerDir = MakeTempDir("coredeck_file_marker_sdk_target");
    std::ofstream((fileMarkerDir / "platform-tools").string()) << "not a directory";
    const std::filesystem::path sdkPlatformsDir = MakeTempDir("coredeck_sdk_platforms_target");
    std::filesystem::create_directories(sdkPlatformsDir / "platforms" / "android-35");
    const std::filesystem::path genericPlatformsDir = MakeTempDir("coredeck_generic_platforms_target");
    std::filesystem::create_directories(genericPlatformsDir / "platforms" / "ios");
    const std::filesystem::path genericToolsDir = MakeTempDir("coredeck_generic_tools_target");
    std::filesystem::create_directories(genericToolsDir / "tools");
    const std::filesystem::path sdkManagerDir = MakeTempDir("coredeck_sdkmanager_target");
    std::filesystem::create_directories(sdkManagerDir / "cmdline-tools" / "latest" / "bin");
#if defined(_WIN32)
    std::ofstream((sdkManagerDir / "cmdline-tools" / "latest" / "bin" / "sdkmanager.bat").string()) << "";
#else
    std::ofstream((sdkManagerDir / "cmdline-tools" / "latest" / "bin" / "sdkmanager").string()) << "";
#endif

    const std::filesystem::path filePath = nonEmptyDir / "sdk-file";
    std::ofstream(filePath.string()) << "not a directory";

    REQUIRE_FALSE(CanInstallAndroidSdkIntoDirectory(""));
    REQUIRE_FALSE(CanInstallAndroidSdkIntoDirectory(nonEmptyDir.string()));
    REQUIRE(CanInstallAndroidSdkIntoDirectory(emptyDir.string()));
    REQUIRE(CanInstallAndroidSdkIntoDirectory((nonEmptyDir / "new-sdk").string()));
    REQUIRE_FALSE(CanInstallAndroidSdkIntoDirectory(platformToolsDir.string()));
    REQUIRE_FALSE(CanInstallAndroidSdkIntoDirectory(fileMarkerDir.string()));
    REQUIRE_FALSE(CanInstallAndroidSdkIntoDirectory(sdkPlatformsDir.string()));
    REQUIRE_FALSE(CanInstallAndroidSdkIntoDirectory(genericPlatformsDir.string()));
    REQUIRE_FALSE(CanInstallAndroidSdkIntoDirectory(genericToolsDir.string()));
    REQUIRE(CanInstallAndroidSdkIntoDirectory(sdkManagerDir.string()));
    REQUIRE_FALSE(CanInstallAndroidSdkIntoDirectory(filePath.string()));

    std::error_code ec;
    std::filesystem::remove_all(nonEmptyDir, ec);
    std::filesystem::remove_all(emptyDir, ec);
    std::filesystem::remove_all(platformToolsDir, ec);
    std::filesystem::remove_all(fileMarkerDir, ec);
    std::filesystem::remove_all(sdkPlatformsDir, ec);
    std::filesystem::remove_all(genericPlatformsDir, ec);
    std::filesystem::remove_all(genericToolsDir, ec);
    std::filesystem::remove_all(sdkManagerDir, ec);
}
