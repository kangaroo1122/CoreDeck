#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <random>

#include "core/adb.h"

using namespace CoreDeck;

namespace {
    std::filesystem::path MakeTempDir(const std::string &prefix) {
        std::random_device rd;
        const std::filesystem::path base = std::filesystem::temp_directory_path() / (prefix + "_" + std::to_string(rd()));
        std::filesystem::create_directories(base);
        return base;
    }
}

TEST_CASE("ADB availability checks the detected executable path", "[adb][sdk]") {
    SdkInfo sdk;
    CHECK_FALSE(HasAdb(sdk));

    const auto tempDir = MakeTempDir("coredeck_adb");
    sdk.AdbPath = (tempDir / "adb").string();
    CHECK_FALSE(HasAdb(sdk));

    std::ofstream(sdk.AdbPath) << "fake adb";
    CHECK_FALSE(HasAdb(sdk));
#ifndef _WIN32
    std::filesystem::permissions(
        sdk.AdbPath,
        std::filesystem::perms::owner_exec | std::filesystem::perms::owner_read | std::filesystem::perms::owner_write,
        std::filesystem::perm_options::add
    );
#endif
    CHECK(HasAdb(sdk));

    sdk.AdbPath = tempDir.string();
    CHECK_FALSE(HasAdb(sdk));
}

TEST_CASE("ADB device list parser keeps online devices and metadata", "[adb][parse]") {
    const std::string output =
        "List of devices attached\n"
        "emulator-5554          device product:sdk_gphone64_arm64 model:sdk_gphone64_arm64 device:emu64a transport_id:1\n"
        "0123456789ABCDEF       offline usb:336592896X product:oriole model:Pixel_6 device:oriole transport_id:2\n"
        "\n";

    const auto devices = ParseAdbDevices(output);

    REQUIRE(devices.size() == 2);
    CHECK(devices[0].Serial == "emulator-5554");
    CHECK(devices[0].State == "device");
    CHECK(devices[0].Product == "sdk_gphone64_arm64");
    CHECK(devices[0].Model == "sdk_gphone64_arm64");
    CHECK(devices[0].Device == "emu64a");
    CHECK(devices[0].TransportId == "1");
    CHECK(devices[0].IsOnline());
    CHECK(devices[1].Serial == "0123456789ABCDEF");
    CHECK(devices[1].State == "offline");
    CHECK_FALSE(devices[1].IsOnline());
}

TEST_CASE("ADB device list parser ignores adb error lines", "[adb][parse]") {
    const std::string output =
        "List of devices attached\n"
        "error: protocol fault\n"
        "adb: failed to read command\n";

    CHECK(ParseAdbDevices(output).empty());
}

TEST_CASE("ADB emulator AVD name parser ignores protocol trailers", "[adb][parse]") {
    const std::string output =
        "Pixel_8_API_35\n"
        "OK\n";

    CHECK(ParseAdbEmuAvdName(output) == "Pixel_8_API_35");
    CHECK(ParseAdbEmuAvdName("KO: not an emulator\n").empty());
    CHECK(ParseAdbEmuAvdName("error: device offline\n").empty());
}

TEST_CASE("ADB emulator serial uses the console port", "[adb][serial]") {
    CHECK(EmulatorSerialForConsolePort(5554) == "emulator-5554");
    CHECK(EmulatorSerialForConsolePort(0).empty());
}
