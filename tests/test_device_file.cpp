#include <catch2/catch_test_macros.hpp>

#include "core/device_file.h"

using namespace CoreDeck;

TEST_CASE("device file stat parser keeps names with spaces", "[device_file][parse]") {
    const std::string output =
        "drwxrwx---\tdirectory\t4096\t1710000000\tDownload\n"
        "-rw-rw----\tregular file\t1536\t1710000123\tMy File.txt\n"
        "lrwxrwxrwx\tsymbolic link\t11\t1710000456\tshortcut\n";

    const auto entries = ParseDeviceFileStatList(output, "/sdcard");

    REQUIRE(entries.size() == 3);
    CHECK(entries[0].Name == "Download");
    CHECK(entries[0].Path == "/sdcard/Download");
    CHECK(entries[0].Kind == DeviceFileKind::Directory);
    CHECK(entries[0].SizeBytes == 4096);
    CHECK(entries[1].Name == "My File.txt");
    CHECK(entries[1].Path == "/sdcard/My File.txt");
    CHECK(entries[1].Kind == DeviceFileKind::File);
    CHECK(entries[1].SizeBytes == 1536);
    CHECK(entries[2].Kind == DeviceFileKind::Symlink);
}

TEST_CASE("device path helpers normalize parent and child paths", "[device_file][path]") {
    CHECK(JoinDevicePath("/sdcard", "Download") == "/sdcard/Download");
    CHECK(JoinDevicePath("/", "sdcard") == "/sdcard");
    CHECK(ParentDevicePath("/sdcard/Download") == "/sdcard");
    CHECK(ParentDevicePath("/sdcard") == "/");
    CHECK(ParentDevicePath("/") == "/");
}

TEST_CASE("device path segment validation rejects nested or parent paths", "[device_file][path]") {
    CHECK(IsValidDevicePathSegment("Download"));
    CHECK(IsValidDevicePathSegment("My Folder"));
    CHECK_FALSE(IsValidDevicePathSegment(""));
    CHECK_FALSE(IsValidDevicePathSegment("   "));
    CHECK_FALSE(IsValidDevicePathSegment("."));
    CHECK_FALSE(IsValidDevicePathSegment(".."));
    CHECK_FALSE(IsValidDevicePathSegment("../data"));
    CHECK_FALSE(IsValidDevicePathSegment("nested/folder"));
}

TEST_CASE("device paths normalize without limiting the device root", "[device_file][path]") {
    CHECK(NormalizeDevicePath("", "/sdcard") == "/sdcard");
    CHECK(NormalizeDevicePath("Download", "/sdcard") == "/sdcard/Download");
    CHECK(NormalizeDevicePath("/sdcard//Download/.", "/sdcard") == "/sdcard/Download");
    CHECK(NormalizeDevicePath("/sdcard/Download/../Pictures", "/sdcard") == "/sdcard/Pictures");
    CHECK(NormalizeDevicePath("/", "/sdcard") == "/");
    CHECK(NormalizeDevicePath("/data", "/sdcard") == "/data");
    CHECK(NormalizeDevicePath("/sdcard/../data", "/sdcard") == "/data");
    CHECK(NormalizeDevicePath("/sdcard2", "/sdcard") == "/sdcard2");
    CHECK(NormalizeDevicePath("../data", "/sdcard") == "/data");
}

TEST_CASE("open device directory command launches Android folder viewer", "[device_file][open]") {
    const auto args = BuildOpenDeviceDirectoryInEmulatorArgs("emulator-5554", "/sdcard/CoreDeckShared");

    REQUIRE(args.size() == 11);
    CHECK(args[0] == "-s");
    CHECK(args[1] == "emulator-5554");
    CHECK(args[2] == "shell");
    CHECK(args[3] == "am");
    CHECK(args[4] == "start");
    CHECK(args[5] == "-a");
    CHECK(args[6] == "android.intent.action.VIEW");
    CHECK(args[7] == "-d");
    CHECK(args[8] == "content://com.android.externalstorage.documents/document/primary%3ACoreDeckShared");
    CHECK(args[9] == "-t");
    CHECK(args[10] == "vnd.android.document/directory");
}

TEST_CASE("open device directory command falls back to file uris outside shared storage", "[device_file][open]") {
    const auto args = BuildOpenDeviceDirectoryInEmulatorArgs("emulator-5554", "/data/local/tmp");

    REQUIRE(args.size() == 11);
    CHECK(args[8] == "file:///data/local/tmp");
    CHECK(args[10] == "resource/folder");
}
