#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

#include "core/paths.h"
#include "core/shared_folder.h"

using namespace CoreDeck;

namespace {
    namespace fs = std::filesystem;

    struct TempTree {
        fs::path Path;

        explicit TempTree(const std::string &name) {
            const auto tick = std::chrono::steady_clock::now().time_since_epoch().count();
            Path = fs::temp_directory_path() / ("coredeck_shared_folder_" + name + "_" + std::to_string(tick));
            fs::create_directories(Path);
        }

        ~TempTree() {
            std::error_code ec;
            fs::remove_all(Path, ec);
        }
    };

    void WriteTextFile(const fs::path &path, const std::string &content) {
        fs::create_directories(path.parent_path());
        std::ofstream file(path, std::ios::binary);
        REQUIRE(file.is_open());
        file << content;
        REQUIRE(file.good());
    }

    std::string ReadTextFile(const fs::path &path) {
        std::ifstream file(path, std::ios::binary);
        REQUIRE(file.is_open());
        return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
    }

    void SetModifiedTime(const fs::path &path, const fs::file_time_type time) {
        std::error_code ec;
        fs::last_write_time(path, time, ec);
        REQUIRE_FALSE(ec);
    }
}

TEST_CASE("shared folder uses a stable host and device location", "[shared_folder][path]") {
    const std::filesystem::path hostPath = GetSharedFolderHostPath();

    REQUIRE(!hostPath.empty());
    CHECK(hostPath.filename().string() == "shared");
    CHECK(hostPath.parent_path().string() == Paths::GetAppConfigPath());
    CHECK(GetSharedFolderDevicePath() == "/sdcard/CoreDeckShared");
}

TEST_CASE("shared folder separates host folders by AVD name", "[shared_folder][path]") {
    const std::filesystem::path rootPath = GetSharedFolderHostPath();
    const std::filesystem::path avdPath = GetSharedFolderHostPath("Pixel Tablet/2");

    CHECK(avdPath.parent_path() == rootPath);
    CHECK(avdPath.filename().string() == "Pixel_Tablet_2");
    CHECK(GetSharedFolderHostPath("   ") == rootPath.string());
}

TEST_CASE("shared folder host opener chooses the platform file manager", "[shared_folder][open]") {
    const std::string folder = "/tmp/CoreDeck Shared";
    const OpenFolderCommand command = BuildOpenFolderCommand(folder);

#if defined(_WIN32)
    CHECK(command.Executable == "explorer.exe");
    CHECK(std::string(GetOpenSharedFolderHostLabel()) == "Open Shared Folder in File Explorer");
#elif defined(__APPLE__)
    CHECK(command.Executable == "/usr/bin/open");
    CHECK(std::string(GetOpenSharedFolderHostLabel()) == "Open Shared Folder in Finder");
#else
    CHECK(command.Executable == "xdg-open");
    CHECK(std::string(GetOpenSharedFolderHostLabel()) == "Open Shared Folder in File Manager");
#endif

    REQUIRE(command.Args.size() == 1);
    CHECK(command.Args[0] == folder);
}

TEST_CASE("shared folder sync pushes host contents into the device folder", "[shared_folder][sync]") {
    const auto args = BuildPushSharedFolderToDeviceArgs(
        "emulator-5554",
        "/tmp/CoreDeck Shared",
        "/sdcard/CoreDeckShared"
    );

    REQUIRE(args.size() == 5);
    CHECK(args[0] == "-s");
    CHECK(args[1] == "emulator-5554");
    CHECK(args[2] == "push");
    CHECK(args[3] == BuildSharedFolderContentPath("/tmp/CoreDeck Shared"));
    CHECK(args[4] == "/sdcard/CoreDeckShared");
}

TEST_CASE("shared folder sync pulls device contents into the host folder", "[shared_folder][sync]") {
    const auto args = BuildPullSharedFolderFromDeviceArgs(
        "emulator-5554",
        "/sdcard/CoreDeckShared",
        "/tmp/CoreDeck Shared"
    );

    REQUIRE(args.size() == 6);
    CHECK(args[0] == "-s");
    CHECK(args[1] == "emulator-5554");
    CHECK(args[2] == "pull");
    CHECK(args[3] == "-a");
    CHECK(args[4] == "/sdcard/CoreDeckShared/.");
    CHECK(args[5] == "/tmp/CoreDeck Shared");
}

TEST_CASE("shared folder merge keeps the newest same-name files", "[shared_folder][sync]") {
    TempTree temp("merge_newest");
    const fs::path staging = temp.Path / "staging";
    const fs::path host = temp.Path / "host";

    WriteTextFile(host / "host-newer.txt", "host newer");
    WriteTextFile(staging / "host-newer.txt", "device older");
    WriteTextFile(host / "device-newer.txt", "host older");
    WriteTextFile(staging / "device-newer.txt", "device newer");
    WriteTextFile(staging / "device-only.txt", "device only");
    WriteTextFile(staging / "nested" / "device-only.txt", "nested device only");

    const auto now = fs::file_time_type::clock::now();
    SetModifiedTime(host / "host-newer.txt", now);
    SetModifiedTime(staging / "host-newer.txt", now - std::chrono::seconds(20));
    SetModifiedTime(host / "device-newer.txt", now - std::chrono::seconds(20));
    SetModifiedTime(staging / "device-newer.txt", now);
    SetModifiedTime(staging / "device-only.txt", now - std::chrono::seconds(10));
    SetModifiedTime(staging / "nested" / "device-only.txt", now - std::chrono::seconds(10));

    std::string error;
    REQUIRE(MergePulledSharedFolder(staging.string(), host.string(), &error));
    CHECK(error.empty());

    CHECK(ReadTextFile(host / "host-newer.txt") == "host newer");
    CHECK(ReadTextFile(host / "device-newer.txt") == "device newer");
    CHECK(ReadTextFile(host / "device-only.txt") == "device only");
    CHECK(ReadTextFile(host / "nested" / "device-only.txt") == "nested device only");
}

TEST_CASE("shared folder merge skips source directories that collide with host files", "[shared_folder][sync]") {
    TempTree temp("merge_directory_collision");
    const fs::path staging = temp.Path / "staging";
    const fs::path host = temp.Path / "host";

    WriteTextFile(host / "conflict", "host file");
    WriteTextFile(staging / "conflict" / "device.txt", "device nested");
    WriteTextFile(staging / "other.txt", "device other");

    std::string error;
    REQUIRE(MergePulledSharedFolder(staging.string(), host.string(), &error));
    CHECK(error.empty());

    CHECK(ReadTextFile(host / "conflict") == "host file");
    CHECK_FALSE(fs::exists(host / "conflict" / "device.txt"));
    CHECK(ReadTextFile(host / "other.txt") == "device other");
}

TEST_CASE("shared folder merge skips source files that collide with host directories", "[shared_folder][sync]") {
    TempTree temp("merge_file_collision");
    const fs::path staging = temp.Path / "staging";
    const fs::path host = temp.Path / "host";

    fs::create_directories(host / "conflict");
    WriteTextFile(host / "conflict" / "host.txt", "host nested");
    WriteTextFile(staging / "conflict", "device file");
    WriteTextFile(staging / "other.txt", "device other");

    std::string error;
    REQUIRE(MergePulledSharedFolder(staging.string(), host.string(), &error));
    CHECK(error.empty());

    CHECK(fs::is_directory(host / "conflict"));
    CHECK(ReadTextFile(host / "conflict" / "host.txt") == "host nested");
    CHECK(ReadTextFile(host / "other.txt") == "device other");
}

TEST_CASE("shared folder reconcile removes host files deleted from the device snapshot", "[shared_folder][sync]") {
    TempTree temp("reconcile_deletions");
    const fs::path staging = temp.Path / "staging";
    const fs::path host = temp.Path / "host";
    const fs::path snapshot = temp.Path / "snapshot.txt";

    WriteTextFile(host / "deleted-on-device.txt", "old host copy");
    WriteTextFile(host / "host-only.txt", "new host file");
    WriteTextFile(host / "kept.txt", "old host kept");
    WriteTextFile(staging / "kept.txt", "device kept");
    WriteTextFile(snapshot, "deleted-on-device.txt\nkept.txt\n");

    std::string error;
    REQUIRE(ReconcilePulledSharedFolder(staging.string(), host.string(), snapshot.string(), &error));
    CHECK(error.empty());

    CHECK_FALSE(fs::exists(host / "deleted-on-device.txt"));
    CHECK(ReadTextFile(host / "host-only.txt") == "new host file");
    CHECK(ReadTextFile(host / "kept.txt") == "device kept");
}
