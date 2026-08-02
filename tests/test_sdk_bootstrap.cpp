#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>

#include "core/sdk_bootstrap.h"

using namespace CoreDeck;

namespace {
    std::filesystem::path MakeTempDir(const std::string &prefix) {
        const auto dir = std::filesystem::temp_directory_path() / (prefix + "_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directories(dir);
        return dir;
    }
}

TEST_CASE("Command-line tools package verification checks size and SHA-1", "[sdk_bootstrap][verify]") {
    const std::filesystem::path tempDir = MakeTempDir("coredeck_cmdline_tools_verify");
    const std::filesystem::path archive = tempDir / "package.zip";
    std::ofstream(archive, std::ios::binary) << "abc";

    CommandLineToolsPackage package;
    package.SizeBytes = 3;
    package.Sha1 = "a9993e364706816aba3e25717850c26c9cd0d89d";
    REQUIRE(detail::FileMatchesCommandLineToolsPackage(archive.string(), package));

    package.SizeBytes = 4;
    REQUIRE_FALSE(detail::FileMatchesCommandLineToolsPackage(archive.string(), package));
    package.SizeBytes = 3;
    package.Sha1 = "0000000000000000000000000000000000000000";
    REQUIRE_FALSE(detail::FileMatchesCommandLineToolsPackage(archive.string(), package));

    std::error_code ec;
    std::filesystem::remove_all(tempDir, ec);
}

TEST_CASE("Bundled command-line tools fallback has independent integrity metadata", "[sdk_bootstrap][fallback]") {
    const CommandLineToolsPackage package = BundledCommandLineToolsPackage();

    REQUIRE(package.DownloadUrl.starts_with("https://dl.google.com/android/repository/"));
    REQUIRE(package.DownloadUrl.ends_with("_latest.zip"));
    REQUIRE(package.SizeBytes > 0);
    REQUIRE(package.Sha1.empty());
    REQUIRE(package.Sha256.size() == 64);
}

TEST_CASE("SDK bootstrap filesystem errors include actionable path context", "[sdk_bootstrap][diagnostics]") {
    const std::filesystem::path source = std::filesystem::path("source") / "cmdline-tools" / "lib" / "tool.jar";
    const std::filesystem::path destination = std::filesystem::path("destination") / "cmdline-tools" / "latest" / "lib" / "tool.jar";
    const std::error_code ec = std::make_error_code(std::errc::no_such_file_or_directory);

    const std::string error = detail::FormatFilesystemError(
        "Copying command-line tools entry",
        source,
        destination,
        ec
    );

    CHECK(error.find("Copying command-line tools entry") != std::string::npos);
    CHECK(error.find(source.string()) != std::string::npos);
    CHECK(error.find(destination.string()) != std::string::npos);
    CHECK(error.find(ec.message()) != std::string::npos);
    CHECK(error.find("code " + std::to_string(ec.value())) != std::string::npos);
}

TEST_CASE("Command-line tools install directly into the SDK transaction root", "[sdk_bootstrap][install]") {
    const std::filesystem::path root = MakeTempDir("coredeck_cmdline_tools_layout");
    const std::filesystem::path extractedTools = root / "extracted" / "cmdline-tools";
    const std::filesystem::path sdkRoot = root / ".Sdk-coredeck-installing-123456789";
    std::filesystem::create_directories(extractedTools / "bin");
    std::filesystem::create_directories(extractedTools / "lib");
    std::ofstream(extractedTools / "bin" / "sdkmanager.bat") << "sdkmanager";
    std::ofstream(extractedTools / "lib" / "tool.jar") << "jar";

    std::string error;
    REQUIRE(detail::InstallExtractedCommandLineTools(extractedTools, sdkRoot, error));

    const std::filesystem::path cmdlineToolsRoot = sdkRoot / "cmdline-tools";
    CHECK(std::filesystem::exists(cmdlineToolsRoot / "latest" / "bin" / "sdkmanager.bat"));
    CHECK(std::filesystem::exists(cmdlineToolsRoot / "latest" / "lib" / "tool.jar"));
    for (const auto &entry: std::filesystem::directory_iterator(cmdlineToolsRoot)) {
        CHECK(entry.path().filename() == "latest");
    }

    std::filesystem::remove_all(root);
}

TEST_CASE("Command-line tools package verification checks size and SHA-256", "[sdk_bootstrap][fallback]") {
    const std::filesystem::path tempDir = MakeTempDir("coredeck_cmdline_tools_sha256_verify");
    const std::filesystem::path archive = tempDir / "package.zip";
    std::ofstream(archive, std::ios::binary) << "abc";

    CommandLineToolsPackage package;
    package.SizeBytes = 3;
    package.Sha256 = "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad";
    REQUIRE(detail::FileMatchesCommandLineToolsPackage(archive.string(), package));

    package.SizeBytes = 4;
    REQUIRE_FALSE(detail::FileMatchesCommandLineToolsPackage(archive.string(), package));
    package.SizeBytes = 3;
    package.Sha256 = std::string(64, '0');
    REQUIRE_FALSE(detail::FileMatchesCommandLineToolsPackage(archive.string(), package));

    std::error_code ec;
    std::filesystem::remove_all(tempDir, ec);
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
