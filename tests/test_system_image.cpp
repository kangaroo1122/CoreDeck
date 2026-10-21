#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdlib>
#include <miniz.h>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <vector>

#include "core/system_image.h"
#include "core/health_check.h"
#include "core/apk_analyzer.h"

using namespace CoreDeck;

TEST_CASE("APK badging parses versions permissions and architectures", "[apk_analyzer]") {
    const auto report = ParseApkBadging(
        "package: name='com.example.demo' versionCode='42' versionName='1.2'\n"
        "application-label:'Demo app'\nminSdkVersion:'23'\ntargetSdkVersion:'36'\n"
        "uses-permission: name='android.permission.INTERNET'\n"
        "uses-permission-sdk-23: name='android.permission.INTERNET'\n"
        "native-code: 'arm64-v8a' 'x86_64'\n");
    CHECK(report.Error.empty());
    CHECK(report.PackageName == "com.example.demo");
    CHECK(report.Label == "Demo app");
    CHECK(report.VersionCode == "42");
    CHECK(report.VersionName == "1.2");
    CHECK(report.MinSdk == "23");
    CHECK(report.TargetSdk == "36");
    CHECK(report.Permissions.size() == 1);
    CHECK(report.Abis == std::vector<std::string>{"arm64-v8a", "x86_64"});
    CHECK(ParseApkBadging("package: name='com.example.app'\nsdkVersion:'21'\n").MinSdk == "21");
    CHECK_FALSE(ParseApkBadging("error: invalid archive").Error.empty());
}

TEST_CASE("AVD manager health check recognizes help rather than diagnostic mentions", "[system_image]") {
    CHECK(IsAvdManagerHelpOutput("Usage:\n avdmanager [global options] [action]\n- list avd\n- create avd\n"));
    CHECK_FALSE(IsAvdManagerHelpOutput("Error: avdmanager could not start"));
    CHECK_FALSE(IsAvdManagerHelpOutput("Usage:\n avdmanager [global options]"));
    CHECK_FALSE(IsAvdManagerHelpOutput(""));
}

TEST_CASE("environment check honours cancellation before launching tools", "[system_image]") {
    const auto cancel = std::make_shared<std::atomic<bool>>(true);
    CHECK(RunEnvironmentHealthCheck({}, cancel).empty());
}

TEST_CASE("SDK license interpretation supports old and new tools", "[system_image][licenses]") {
    CHECK(InterpretSdkLicenseOutput("All SDK package licenses accepted.") == LicenseStatus::AllAccepted);
    CHECK(InterpretSdkLicenseOutput("1 license not accepted\nAll SDK package licenses accepted.") == LicenseStatus::AllAccepted);
    CHECK(InterpretSdkLicenseOutput("The --licenses option is no longer needed.") == LicenseStatus::AllAccepted);
    CHECK(InterpretSdkLicenseOutput("1 of 1 SDK package license not accepted") == LicenseStatus::SomeUnaccepted);
    CHECK(InterpretSdkLicenseOutput("2 of 7 SDK package licenses not accepted") == LicenseStatus::SomeUnaccepted);
    CHECK(InterpretSdkLicenseOutput("Error: Java could not start") == LicenseStatus::CheckFailed);
    CHECK(InterpretSdkLicenseOutput("") == LicenseStatus::CheckFailed);
}

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
        out << "echo   system-images/android-35/google_apis_playstore_tablet/" << abi << " ^| 9 ^| Google Play Tablet " << abiLabel << " System Image\n";
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
        out << "  system-images/android-35/google_apis_playstore_tablet/" << abi << " | 9 | Google Play Tablet " << abiLabel << " System Image\n";
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

TEST_CASE("failed image installation does not succeed because an old image exists", "[system_image][install]") {
    const auto root = MakeTempDir("coredeck_failed_image_install");
    struct Cleanup {
        std::filesystem::path Path;
        ~Cleanup() { std::error_code ignored; std::filesystem::remove_all(Path, ignored); }
    } cleanup{root};
    CreateInstalledSystemImage(root, "35", "google_apis");
    const auto manager = WriteFakeSdkManager(root);
    {
        std::ofstream script(manager);
#ifdef _WIN32
        script << "@echo off\nexit /b 1\n";
#else
        script << "#!/bin/sh\nexit 1\n";
#endif
    }
    SdkInfo sdk;
    sdk.SdkPath = root.string();
    sdk.SdkManagerPath = manager.string();
    const auto progress = std::make_shared<InstallProgressData>();
    CHECK_FALSE(InstallSystemImage(sdk, "system-images;android-35;google_apis;" + TestAbi(), progress));
    CHECK(progress->Finished);
    CHECK_FALSE(progress->Succeeded);
    // A failed uninstall is not successful merely because the target was already absent.
    CHECK_FALSE(UninstallSystemImage(sdk, "system-images;android-99;google_apis;" + TestAbi()));

    progress->CancelRequested.store(true);
    CHECK_FALSE(InstallSystemImage(sdk, "system-images;android-35;google_apis;" + TestAbi(), progress));
    CHECK(progress->Finished);
    CHECK_FALSE(progress->Succeeded);
    CHECK(progress->StatusText == "Cancelled.");
    const auto cancelled = [] { return true; };
    CHECK(CheckSdkLicenses(sdk, cancelled) == LicenseStatus::CheckFailed);
    CHECK_FALSE(AcceptSdkLicenses(sdk, cancelled));
    CHECK_FALSE(UninstallSystemImage(sdk, "system-images;android-35;google_apis;" + TestAbi(), cancelled));
    CHECK(ListRemoteSystemImages(sdk, {}, cancelled).empty());
    sdk.AvdManagerPath = manager.string();
    CHECK(ListDeviceProfiles(sdk, cancelled).empty());
    CHECK_THROWS_AS(ListRemoteSystemImages(sdk, {}), std::runtime_error);
    CHECK_THROWS_AS(ListDeviceProfiles(sdk), std::runtime_error);
}

TEST_CASE("ParseApkSummary reads the tab-separated summary row", "[apk-analyzer]") {
    const auto summary = ParseApkSummary("com.example.helloworld\t1\t1.0\n");

    REQUIRE(summary.has_value());
    REQUIRE(summary->ApplicationId == "com.example.helloworld");
    REQUIRE(summary->VersionCode == "1");
    REQUIRE(summary->VersionName == "1.0");
}

TEST_CASE("ParseApkSummary keeps a version name that contains spaces", "[apk-analyzer]") {
    const auto summary = ParseApkSummary("Warning: package.xml parsing problem\ncom.example.app\t42\t1.2 beta\r\n");

    REQUIRE(summary.has_value());
    REQUIRE(summary->VersionCode == "42");
    REQUIRE(summary->VersionName == "1.2 beta");
}

TEST_CASE("ParseApkSummary rejects a row that is not three fields", "[apk-analyzer]") {
    REQUIRE_FALSE(ParseApkSummary("com.example.helloworld\n").has_value());
    REQUIRE_FALSE(ParseApkSummary("").has_value());
}

TEST_CASE("ParseApkByteSize reads a byte count", "[apk-analyzer]") {
    REQUIRE(ParseApkByteSize("5047\n") == 5047);
    REQUIRE(ParseApkByteSize("3952\n") == 3952);
    REQUIRE_FALSE(ParseApkByteSize("12.3MB\n").has_value());
}

TEST_CASE("ParseApkDebuggable reads true and false", "[apk-analyzer]") {
    REQUIRE(ParseApkDebuggable("true\n") == true);
    REQUIRE(ParseApkDebuggable("false\n") == false);
    REQUIRE_FALSE(ParseApkDebuggable("yes\n").has_value());
}

TEST_CASE("ParseApkFileList reads raw size, download size, and path", "[apk-analyzer]") {
    const auto files = ParseApkFileList(
        "4157\t3476\t/\n"
        "703\t703\t/classes.dex\n"
        "312\t312\t/res/layout/main.xml\r\n"
    );

    REQUIRE(files.has_value());
    REQUIRE(files->size() == 3);
    REQUIRE(files->at(0).Path == "/");
    REQUIRE(files->at(0).RawSize == 4157);
    REQUIRE(files->at(0).DownloadSize == 3476);
    REQUIRE(files->at(1).Path == "/classes.dex");
    REQUIRE(files->at(2).Path == "/res/layout/main.xml");
    REQUIRE(files->at(2).RawSize == 312);
}

TEST_CASE("ParseApkFileList rejects a row without both size columns", "[apk-analyzer]") {
    REQUIRE_FALSE(ParseApkFileList("703\t/classes.dex\n").has_value());
}

TEST_CASE("ParseApkCompare reads old, new, and signed difference", "[apk-analyzer]") {
    const auto rows = ParseApkCompare(
        "960\t649\t-311\t/\n"
        "352\t0\t-352\t/instant-run.zip\n"
        "6\t6\t0\t/res/anim/fade.xml\n"
    );

    REQUIRE(rows.has_value());
    REQUIRE(rows->size() == 3);
    REQUIRE(rows->at(0).Path == "/");
    REQUIRE(rows->at(0).OldSize == 960);
    REQUIRE(rows->at(0).NewSize == 649);
    REQUIRE(rows->at(0).Difference == -311);
    REQUIRE(rows->at(1).Path == "/instant-run.zip");
    REQUIRE(rows->at(2).Difference == 0);
}

TEST_CASE("RunApkAnalyzer refuses to run without an apkanalyzer binary", "[apk-analyzer]") {
    const SdkInfo sdk;
    REQUIRE_FALSE(RunApkAnalyzer(sdk, {"apk", "summary", "app.apk"}).has_value());

    const auto summary = QueryApkSummary(sdk, "app.apk");
    REQUIRE(summary.ToolMissing);
    REQUIRE_FALSE(summary.Ok);
}

TEST_CASE("QueryApkSummary does not launch when the apk path is empty", "[apk-analyzer]") {
    SdkInfo sdk;
    sdk.ApkAnalyzerPath = "/sdk/cmdline-tools/latest/bin/apkanalyzer";

    const auto summary = QueryApkSummary(sdk, "");
    REQUIRE_FALSE(summary.ToolMissing);
    REQUIRE_FALSE(summary.Ok);
}

TEST_CASE("LoadApkReport reports a missing apkanalyzer without running it", "[apk-analyzer]") {
    const auto report = LoadApkReport(SdkInfo{}, "app.apk");

    REQUIRE(report.ToolMissing);
    REQUIRE(report.Path == "app.apk");
    REQUIRE_FALSE(report.Summary.Ok);
    REQUIRE_FALSE(report.Debuggable.Ok);
    REQUIRE_FALSE(report.Files.Ok);
}

TEST_CASE("ZipDataAlignment reports the page or byte boundary", "[apk-analyzer]") {
    REQUIRE(ZipDataAlignment(0) == 0);
    REQUIRE(ZipDataAlignment(1) == 1);
    REQUIRE(ZipDataAlignment(4) == 4);
    REQUIRE(ZipDataAlignment(4096) == 4096);
    REQUIRE(ZipDataAlignment(16384) == 16384);
    REQUIRE(ZipDataAlignment(16384 + 4) == 4);
    REQUIRE(ZipDataAlignment(32768) == 16384);
}

TEST_CASE("APK analyzer reports missing tools and honors cancellation", "[apk_analyzer]") {
    const auto root = MakeTempDir("coredeck_apk_analyzer");
    struct Cleanup {
        std::filesystem::path Path;
        ~Cleanup() { std::error_code ignored; std::filesystem::remove_all(Path, ignored); }
    } cleanup{root};
    const auto apk = root / "demo app.apk";
    std::ofstream(apk) << "test fixture";
    SdkInfo sdk;
    sdk.SdkPath = root.string();
    CHECK_FALSE(AnalyzeApk(sdk, apk.string(), {}).Error.empty());
    CHECK(AnalyzeApk(sdk, apk.string(), [] { return true; }).Cancelled);

#ifndef _WIN32
    // Native aapt2 is an executable rather than a batch file on Windows.
    // Exercise orchestration with a local executable fixture on POSIX hosts.
    const auto tools = root / "build-tools" / "37.0.0";
    std::filesystem::create_directories(tools);
    const auto tool = tools / "aapt2";
    std::ofstream(tool) << "#!/bin/sh\necho \"package: name='com.example.demo' versionCode='1' versionName='1.0'\"\necho \"minSdkVersion:'23'\"\n";
    std::filesystem::permissions(tool, std::filesystem::perms::owner_exec, std::filesystem::perm_options::add);
    const auto analyzer = root / "apkanalyzer";
    std::ofstream(analyzer) << "#!/bin/sh\ncase \"$1 $2\" in\n"
        "'apk summary') printf 'com.example.demo\\t1\\t1.0\\n';;\n"
        "'apk file-size'|'apk download-size') echo 100;;\n"
        "'manifest debuggable') echo false;;\n"
        "'files list') printf '100\\t80\\t/\\n100\\t80\\t/classes.dex\\n';;\n"
        "'apk compare') printf '100\\t120\\t20\\t/classes.dex\\n';;\nesac\n";
    std::filesystem::permissions(analyzer, std::filesystem::perms::owner_exec, std::filesystem::perm_options::add);
    sdk.ApkAnalyzerPath = analyzer.string();
    const auto report = AnalyzeApk(sdk, apk.string(), {});
    CHECK(report.Error.empty());
    CHECK(report.PackageName == "com.example.demo");
    CHECK(report.MinSdk == "23");
    CHECK(report.Path == apk.string());
    CHECK(report.Files.Ok);
    CHECK(report.Files.Value.size() == 2);
    const auto baseline = root / "baseline.apk";
    std::ofstream(baseline) << "baseline fixture";
    const auto comparison = LoadApkCompare(sdk, baseline.string(), apk.string());
    REQUIRE(comparison.Ok);
    CHECK(comparison.Value.front().Difference == 20);
    std::ofstream(analyzer) << "#!/bin/sh\nexit 1\n";
    CHECK_FALSE(QueryApkSummary(sdk, apk.string()).Ok);
    CHECK(AnalyzeApk(sdk, apk.string(), [] { return true; }).Cancelled);
    sdk.AdbPath = tool.string();
    CHECK_FALSE(InstallApk(sdk, "emulator-5554", apk.string(), {}).Succeeded);
    std::ofstream(tool) << "#!/bin/sh\necho Success\n";
    CHECK(InstallApk(sdk, "emulator-5554", apk.string(), {}).Succeeded);
    CHECK(InstallApk(sdk, "emulator-5554", apk.string(), [] { return true; }).Cancelled);
#endif
}

TEST_CASE("APK file listing falls back to ZIP headers", "[apk-analyzer]") {
    const auto root = MakeTempDir("coredeck_apk_zip_fallback");
    const auto path = root / "nested.apk";
    mz_zip_archive zip{};
    REQUIRE(mz_zip_writer_init_file(&zip, path.string().c_str(), 0));
    REQUIRE(mz_zip_writer_add_mem(&zip, "assets/inner.jar", "not a jar", 9, MZ_NO_COMPRESSION));
    REQUIRE(mz_zip_writer_finalize_archive(&zip));
    mz_zip_writer_end(&zip);
    const auto files = QueryApkFiles(SdkInfo{}, path.string());
    REQUIRE(files.Ok);
    CHECK(files.FromZip);
    REQUIRE(files.Value.size() == 3);
    CHECK(files.Value.front().Path == "/");
    CHECK(files.Value.front().RawSize == 9);
    CHECK(files.Value.front().DownloadSize == 9);
    CHECK_FALSE(QueryApkFiles(SdkInfo{}, path.string(), [] { return true; }).Ok);
    std::filesystem::remove_all(root);
}

TEST_CASE("APK analyzer integrates with a local Android SDK", "[.apk-sdk]") {
    const char *sdkPath = std::getenv("COREDECK_TEST_SDK");
    const char *apkPath = std::getenv("COREDECK_TEST_APK");
    REQUIRE(sdkPath != nullptr);
    REQUIRE(apkPath != nullptr);
    SdkInfo sdk;
    sdk.SdkPath = sdkPath;
    sdk.ApkAnalyzerPath = (std::filesystem::path(sdkPath) / "cmdline-tools/latest/bin/apkanalyzer").string();
    float progress = 0;
    const auto report = AnalyzeApk(sdk, apkPath, {}, [&](float value) { progress = value; });
    INFO(report.Details);
    REQUIRE(report.Error.empty());
    REQUIRE(report.Summary.Ok);
    REQUIRE(report.Files.Ok);
    CHECK_FALSE(report.PackageName.empty());
    CHECK_FALSE(report.Files.Value.empty());
    CHECK(progress == 1.0F);
    CHECK(std::ranges::any_of(report.Files.Value, [](const auto &file) { return file.Alignment != 0; }));
    const auto comparison = LoadApkCompare(sdk, apkPath, apkPath);
    INFO(comparison.Output);
    REQUIRE(comparison.Ok);
    CHECK(std::ranges::all_of(comparison.Value, [](const auto &row) { return row.Difference == 0; }));
    if (const char *baseline = std::getenv("COREDECK_TEST_BASELINE")) {
        const auto different = LoadApkCompare(sdk, baseline, apkPath);
        INFO(different.Output);
        REQUIRE(different.Ok);
        CHECK_FALSE(different.Value.empty());
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
    installed.PackagePath = "system-images/android-35/google_apis_playstore_tablet/" + abi;

    const auto images = ListRemoteSystemImages(sdk, {installed});
    REQUIRE(images.size() == 3);

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
