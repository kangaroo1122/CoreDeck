#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <vector>

#include "core/system_image.h"

using namespace CoreDeck;

namespace {
    std::string TestAbi() {
        const auto supportedAbis = SupportedSystemImageAbis();
        return supportedAbis.empty() ? "arm64-v8a" : supportedAbis.front();
    }

    std::string UnsupportedTestAbi() {
        const std::string abi = TestAbi();
        return abi == "arm64-v8a" || abi == "armeabi-v7a" ? "x86_64" : "arm64-v8a";
    }

    std::string AbiDisplayName(const std::string &abi) {
        if (abi == "arm64-v8a") {
            return "ARM 64 v8a";
        }
        if (abi == "x86_64") {
            return "Intel x86_64 Atom";
        }
        if (abi == "x86") {
            return "Intel x86 Atom";
        }
        if (abi == "armeabi-v7a") {
            return "ARM EABI v7a";
        }
        return abi;
    }

    std::filesystem::path MakeTempDir(const std::string &prefix) {
        std::random_device rd;
        const std::filesystem::path base = std::filesystem::temp_directory_path() / (prefix + "_" + std::to_string(rd()));
        std::filesystem::create_directories(base);
        return base;
    }

    const RemoteSystemImage *FindRemoteImage(
        const std::vector<RemoteSystemImage> &images,
        const std::string &packagePath
    ) {
        const auto it = std::ranges::find_if(images, [&](const RemoteSystemImage &img) {
            return img.PackagePath == packagePath;
        });
        return it == images.end() ? nullptr : &*it;
    }

    const SystemImage *FindLocalImage(
        const std::vector<SystemImage> &images,
        const std::string &packagePath
    ) {
        const auto it = std::ranges::find_if(images, [&](const SystemImage &img) {
            return img.PackagePath == packagePath;
        });
        return it == images.end() ? nullptr : &*it;
    }

    std::filesystem::path WriteFakeSdkManager(const std::filesystem::path &dir) {
        const std::string abi = TestAbi();
        const std::string abiLabel = AbiDisplayName(abi);
        const std::string unsupportedAbi = UnsupportedTestAbi();
        const std::string unsupportedAbiLabel = AbiDisplayName(unsupportedAbi);
#ifdef _WIN32
        const std::filesystem::path script = dir / "sdkmanager.bat";
        std::ofstream out(script);
        out << "@echo off\n";
        out << "echo Available Packages:\n";
        out << "echo   Path ^| Version ^| Description\n";
        out << "echo   ------- ^| ------- ^| -------\n";
        out << "echo   system-images;android-37.0;google_apis_playstore;" << abi << " ^| 5 ^| Google Play " << abiLabel << " System Image\n";
        out << "echo   system-images;android-37.0;google_apis_playstore_ps16k;" << abi << " ^| 6 ^| 16 KB Page Size Google Play " << abiLabel << " System Image\n";
        out << "echo   system-images;android-35;google_apis_playstore_tablet;" << abi << " ^| 9 ^| Google Play Tablet " << abiLabel << " System Image\n";
        out << "echo   system-images;android-35;google_apis_playstore;" << unsupportedAbi << " ^| 9 ^| Google Play " << unsupportedAbiLabel << " System Image\n";
#else
        const std::filesystem::path script = dir / "sdkmanager";
        std::ofstream out(script);
        out << "#!/bin/sh\n";
        out << "cat <<'SDKMANAGER_OUTPUT'\n";
        out << "Available Packages:\n";
        out << "  Path                                                            | Version | Description\n";
        out << "  -------                                                         | ------- | -------\n";
        out << "  system-images;android-37.0;google_apis_playstore;" << abi << "      | 5       | Google Play " << abiLabel << " System Image\n";
        out << "  system-images;android-37.0;google_apis_playstore_ps16k;" << abi << " | 6       | 16 KB Page Size Google Play " << abiLabel << " System Image\n";
        out << "  system-images;android-35;google_apis_playstore_tablet;" << abi << " | 9       | Google Play Tablet " << abiLabel << " System Image\n";
        out << "  system-images;android-35;google_apis_playstore;" << unsupportedAbi << " | 9       | Google Play " << unsupportedAbiLabel << " System Image\n";
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

    void CreateInstalledSystemImage(
        const std::filesystem::path &sdkRoot,
        const std::string &api,
        const std::string &variant,
        const std::string &abi = TestAbi()
    ) {
        const auto imageDir = sdkRoot / "system-images" / ("android-" + api) / variant / abi;
        std::filesystem::create_directories(imageDir);
        std::ofstream(imageDir / "system.img") << "system image";
    }
}

TEST_CASE("remote system image names use sdkmanager descriptions", "[system_image][list]") {
    const std::filesystem::path tempDir = MakeTempDir("coredeck_system_images_remote");
    const std::filesystem::path sdkManager = WriteFakeSdkManager(tempDir);
    const std::string abi = TestAbi();
    const std::string abiLabel = AbiDisplayName(abi);
    const std::string unsupportedAbi = UnsupportedTestAbi();

    SdkInfo sdk;
    sdk.SdkPath = tempDir.string();
    sdk.SdkManagerPath = sdkManager.string();

    SystemImage installed;
    installed.PackagePath = "system-images;android-35;google_apis_playstore_tablet;" + abi;

    const auto images = ListRemoteSystemImages(sdk, {installed});

    const auto *play = FindRemoteImage(images, "system-images;android-37.0;google_apis_playstore;" + abi);
    const auto *pageSize16k = FindRemoteImage(images, "system-images;android-37.0;google_apis_playstore_ps16k;" + abi);
    const auto *tablet = FindRemoteImage(images, "system-images;android-35;google_apis_playstore_tablet;" + abi);

    REQUIRE(play != nullptr);
    REQUIRE(pageSize16k != nullptr);
    REQUIRE(tablet != nullptr);
    REQUIRE(play->DisplayName == "Google Play " + abiLabel + " System Image");
    REQUIRE(pageSize16k->DisplayName == "16 KB Page Size Google Play " + abiLabel + " System Image");
    REQUIRE(tablet->DisplayName == "Google Play Tablet " + abiLabel + " System Image");
    REQUIRE(tablet->IsInstalled);
    REQUIRE(FindRemoteImage(images, "system-images;android-35;google_apis_playstore;" + unsupportedAbi) == nullptr);

    std::error_code ec;
    std::filesystem::remove_all(tempDir, ec);
}

TEST_CASE("installed system image names preserve variant details", "[system_image][display]") {
    const std::filesystem::path tempDir = MakeTempDir("coredeck_system_images_local");
    const std::string abi = TestAbi();
    const std::string abiLabel = AbiDisplayName(abi);
    const std::string unsupportedAbi = UnsupportedTestAbi();
    CreateInstalledSystemImage(tempDir, "35", "google_apis_playstore_ps16k");
    CreateInstalledSystemImage(tempDir, "35", "google_apis_playstore_tablet");
    CreateInstalledSystemImage(tempDir, "35", "google_apis_playstore", unsupportedAbi);

    SdkInfo sdk;
    sdk.SdkPath = tempDir.string();

    const auto images = ListSystemImages(sdk);
    const auto *pageSize16k = FindLocalImage(images, "system-images;android-35;google_apis_playstore_ps16k;" + abi);
    const auto *tablet = FindLocalImage(images, "system-images;android-35;google_apis_playstore_tablet;" + abi);

    REQUIRE(pageSize16k != nullptr);
    REQUIRE(tablet != nullptr);
    REQUIRE(pageSize16k->DisplayName == "16 KB Page Size Google Play " + abiLabel + " System Image");
    REQUIRE(tablet->DisplayName == "Google Play Tablet " + abiLabel + " System Image");
    REQUIRE(FindLocalImage(images, "system-images;android-35;google_apis_playstore;" + unsupportedAbi) == nullptr);

    std::error_code ec;
    std::filesystem::remove_all(tempDir, ec);
}
