#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <random>

#include "core/sdk_packages.h"

using namespace CoreDeck;

namespace {
    SdkPackage Package(
        std::string path,
        std::string version,
        std::string description,
        const bool installed = false,
        const bool updateAvailable = false
    ) {
        SdkPackage package;
        package.Path = std::move(path);
        package.Version = version;
        package.Description = std::move(description);
        package.Installed = installed;
        package.Available = !installed || updateAvailable;
        package.InstalledVersion = installed ? version : "";
        package.AvailableVersion = version;
        package.UpdateAvailable = updateAvailable;
        return package;
    }

    std::filesystem::path MakeTempDir(const std::string &prefix) {
        std::random_device rd;
        const std::filesystem::path base = std::filesystem::temp_directory_path() / (prefix + "_" + std::to_string(rd()));
        std::filesystem::create_directories(base);
        return base;
    }

    std::filesystem::path WriteFakeSdkManager(const std::filesystem::path &dir) {
#ifdef _WIN32
        const std::filesystem::path script = dir / "sdkmanager.bat";
        std::ofstream out(script);
        out << "@echo off\n";
        out << "echo WARNING: The SDK Manager CLI tool (sdkmanager) is deprecated. Use Android CLI instead.\n";
        out << "echo The 'android' binary can also be found in the cmdline-tools directory, and 'android sdk' is the replacement for 'sdkmanager'.\n";
        out << "echo To learn more about the Android CLI and how to use it, see the documentation (https://d.android.com/tools/agents/android-cli)\n";
        out << "echo.\n";
        out << "echo Installed packages:\n";
        out << "echo   Path ^| Version ^| Description ^| Location\n";
        out << "echo   ------- ^| ------- ^| ------- ^| -------\n";
        out << "echo   platform-tools ^| 37.0.0 ^| Android SDK Platform-Tools ^| platform-tools\n";
        out << "echo Available Packages:\n";
        out << "echo Loading package metadata from remote repository...\n";
        out << "echo   Path ^| Version ^| Description\n";
        out << "echo   ------- ^| ------- ^| -------\n";
        out << "echo   platforms;android-36 ^| 1 ^| Android SDK Platform 36\n";
        out << "echo   platforms;android-36.1 ^| 1 ^| Android SDK Platform 36.1\n";
#else
        const std::filesystem::path script = dir / "sdkmanager";
        std::ofstream out(script);
        out << "#!/bin/sh\n";
        out << "cat <<'SDKMANAGER_OUTPUT'\n";
        out << "WARNING: The SDK Manager CLI tool (sdkmanager) is deprecated. Use Android CLI instead.\n";
        out << "The 'android' binary can also be found in the cmdline-tools directory, and 'android sdk' is the replacement for 'sdkmanager'.\n";
        out << "To learn more about the Android CLI and how to use it, see the documentation (https://d.android.com/tools/agents/android-cli)\n";
        out << "\n";
        out << "Installed packages:\n";
        out << "  Path           | Version | Description                | Location\n";
        out << "  -------        | ------- | -------                    | -------\n";
        out << "  platform-tools | 37.0.0  | Android SDK Platform-Tools | platform-tools\n";
        out << "Available Packages:\n";
        out << "Loading package metadata from remote repository...\n";
        out << "  Path                   | Version | Description\n";
        out << "  -------                | ------- | -------\n";
        out << "  platforms;android-36   | 1       | Android SDK Platform 36\n";
        out << "  platforms;android-36.1 | 1       | Android SDK Platform 36.1\n";
        out << "SDKMANAGER_OUTPUT\n";
        out.close();
        std::filesystem::permissions(
            script,
            std::filesystem::perms::owner_exec | std::filesystem::perms::owner_read | std::filesystem::perms::owner_write,
            std::filesystem::perm_options::add
        );
#endif
        return script;
    }
}

TEST_CASE("SDK platform summary installs only the platform package", "[sdk_packages][display]") {
    std::vector<SdkPackage> packages = {
        Package("platforms;android-35", "2", "Android SDK Platform 35"),
        Package("sources;android-35", "1", "Sources for Android 35"),
        Package("system-images;android-35;google_apis;arm64-v8a", "7", "Google APIs ARM 64 v8a System Image"),
    };

    const auto rows = BuildSdkPackageDisplayRows(packages, SdkPackageViewTab::Platforms, SdkPackageViewMode::Summary);

    REQUIRE(rows.size() == 1);
    REQUIRE(rows[0].ApiLevel == "35");
    REQUIRE(rows[0].InstallPackagePaths == std::vector<std::string>{"platforms;android-35"});
    REQUIRE(rows[0].Status == SdkPackageDisplayStatus::NotInstalled);
}

TEST_CASE("SDK platform details include platform sources and system images", "[sdk_packages][display]") {
    std::vector<SdkPackage> packages = {
        Package("platforms;android-35", "2", "Android SDK Platform 35"),
        Package("sources;android-35", "1", "Sources for Android 35"),
        Package("system-images;android-35;google_apis;arm64-v8a", "7", "Google APIs ARM 64 v8a System Image"),
        Package("platform-tools", "37.0.0", "Android SDK Platform-Tools"),
    };

    const auto rows = BuildSdkPackageDisplayRows(packages, SdkPackageViewTab::Platforms, SdkPackageViewMode::Details);

    REQUIRE(rows.size() == 3);
    REQUIRE(rows[0].PackagePath == "platforms;android-35");
    REQUIRE(rows[1].PackagePath == "sources;android-35");
    REQUIRE(rows[2].PackagePath == "system-images;android-35;google_apis;arm64-v8a");
}

TEST_CASE("SDK tools summary groups build tools and reports updates", "[sdk_packages][display]") {
    std::vector<SdkPackage> packages = {
        Package("build-tools;35.0.0", "35.0.0", "Android SDK Build-Tools 35", true),
        Package("build-tools;37.0.0", "37.0.0", "Android SDK Build-Tools 37"),
        Package("platform-tools", "37.0.0", "Android SDK Platform-Tools", true),
    };

    const auto rows = BuildSdkPackageDisplayRows(packages, SdkPackageViewTab::Tools, SdkPackageViewMode::Summary);

    REQUIRE(rows.size() == 2);
    REQUIRE(rows[0].Name == "Android SDK Build-Tools");
    REQUIRE(rows[0].Status == SdkPackageDisplayStatus::UpdateAvailable);
    REQUIRE(rows[0].InstallPackagePaths == std::vector<std::string>{"build-tools;37.0.0"});
    REQUIRE(rows[0].RemovePackagePaths == std::vector<std::string>{"build-tools;35.0.0"});
}

TEST_CASE("SDK tools details include concrete tool versions", "[sdk_packages][display]") {
    std::vector<SdkPackage> packages = {
        Package("build-tools;35.0.0", "35.0.0", "Android SDK Build-Tools 35", true),
        Package("build-tools;37.0.0", "37.0.0", "Android SDK Build-Tools 37"),
        Package("platforms;android-35", "2", "Android SDK Platform 35"),
    };

    const auto rows = BuildSdkPackageDisplayRows(packages, SdkPackageViewTab::Tools, SdkPackageViewMode::Details);

    REQUIRE(rows.size() == 2);
    REQUIRE(rows[0].PackagePath == "build-tools;37.0.0");
    REQUIRE(rows[1].PackagePath == "build-tools;35.0.0");
}

TEST_CASE("SDK package list ignores sdkmanager diagnostics before package sections", "[sdk_packages][list]") {
    const std::filesystem::path tempDir = MakeTempDir("coredeck_sdkmanager");
    const std::filesystem::path sdkManager = WriteFakeSdkManager(tempDir);

    SdkInfo sdk;
    sdk.SdkPath = tempDir.string();
    sdk.SdkManagerPath = sdkManager.string();

    const SdkPackageListResult result = ListSdkPackages(sdk, false);

    REQUIRE(result.Error.empty());
    REQUIRE(result.Packages.size() == 3);
    REQUIRE(std::ranges::none_of(result.Packages, [](const SdkPackage &package) {
        return package.Path.find("android binary") != std::string::npos ||
               package.Path.find("documentation") != std::string::npos ||
               package.Path.find("WARNING") != std::string::npos;
    }));
    REQUIRE(SelectLatestSdkPlatformPackage(result.Packages) == "platforms;android-36.1");

    std::error_code ec;
    std::filesystem::remove_all(tempDir, ec);
}
