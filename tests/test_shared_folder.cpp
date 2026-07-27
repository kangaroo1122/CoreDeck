#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

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

    std::uint64_t HashFileForSnapshot(const fs::path &path) {
        std::ifstream file(path, std::ios::binary);
        REQUIRE(file.is_open());

        std::uint64_t hash = 14695981039346656037ULL;
        char buffer[4096] = {};
        while (file.read(buffer, sizeof(buffer)) || file.gcount() > 0) {
            for (std::streamsize i = 0; i < file.gcount(); i++) {
                hash ^= static_cast<unsigned char>(buffer[i]);
                hash *= 1099511628211ULL;
            }
        }
        REQUIRE_FALSE(file.bad());
        return hash;
    }

    void WriteSnapshot(
        const fs::path &snapshot,
        const fs::path &root,
        const std::vector<std::string> &relativePaths
    ) {
        fs::create_directories(snapshot.parent_path());
        std::ofstream file(snapshot, std::ios::binary);
        REQUIRE(file.is_open());

        for (const auto &relativePath: relativePaths) {
            const fs::path path = root / relativePath;
            file << "v2\t"
                 << fs::file_size(path) << '\t'
                 << fs::last_write_time(path).time_since_epoch().count() << '\t'
                 << HashFileForSnapshot(path) << '\t'
                 << relativePath << '\n';
        }
        REQUIRE(file.good());
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

TEST_CASE("shared folder reconcile keeps host changes when the device copy is unchanged", "[shared_folder][sync]") {
    TempTree temp("reconcile_host_changed");
    const fs::path base = temp.Path / "base";
    const fs::path staging = temp.Path / "staging";
    const fs::path host = temp.Path / "host";
    const fs::path snapshot = temp.Path / "snapshot.txt";

    WriteTextFile(base / "changed.txt", "base");
    WriteTextFile(host / "changed.txt", "host changed");
    WriteTextFile(staging / "changed.txt", "base");

    const auto now = fs::file_time_type::clock::now();
    SetModifiedTime(base / "changed.txt", now);
    SetModifiedTime(staging / "changed.txt", now);
    SetModifiedTime(host / "changed.txt", now - std::chrono::seconds(20));
    WriteSnapshot(snapshot, base, {"changed.txt"});

    SharedFolderReconcileChanges changes;
    std::string error;
    REQUIRE(ReconcilePulledSharedFolder(staging.string(), host.string(), snapshot.string(), &error, &changes));
    CHECK(error.empty());

    CHECK(ReadTextFile(host / "changed.txt") == "host changed");
    CHECK(changes.DeviceDeletedPaths.empty());
    CHECK(changes.ConflictPaths.empty());
}

TEST_CASE("shared folder reconcile pulls device changes when the host copy is unchanged", "[shared_folder][sync]") {
    TempTree temp("reconcile_device_changed");
    const fs::path base = temp.Path / "base";
    const fs::path staging = temp.Path / "staging";
    const fs::path host = temp.Path / "host";
    const fs::path snapshot = temp.Path / "snapshot.txt";

    WriteTextFile(base / "changed.txt", "base");
    WriteTextFile(host / "changed.txt", "base");
    WriteTextFile(staging / "changed.txt", "device changed");

    const auto now = fs::file_time_type::clock::now();
    SetModifiedTime(base / "changed.txt", now);
    SetModifiedTime(host / "changed.txt", now);
    SetModifiedTime(staging / "changed.txt", now - std::chrono::seconds(20));
    WriteSnapshot(snapshot, base, {"changed.txt"});

    SharedFolderReconcileChanges changes;
    std::string error;
    REQUIRE(ReconcilePulledSharedFolder(staging.string(), host.string(), snapshot.string(), &error, &changes));
    CHECK(error.empty());

    CHECK(ReadTextFile(host / "changed.txt") == "device changed");
    CHECK(changes.DeviceDeletedPaths.empty());
    CHECK(changes.ConflictPaths.empty());
}

TEST_CASE("shared folder reconcile schedules device deletes for files removed on host", "[shared_folder][sync]") {
    TempTree temp("reconcile_host_deleted");
    const fs::path base = temp.Path / "base";
    const fs::path staging = temp.Path / "staging";
    const fs::path host = temp.Path / "host";
    const fs::path snapshot = temp.Path / "snapshot.txt";

    WriteTextFile(base / "deleted-on-host.txt", "base");
    WriteTextFile(staging / "deleted-on-host.txt", "base");
    WriteSnapshot(snapshot, base, {"deleted-on-host.txt"});

    SharedFolderReconcileChanges changes;
    std::string error;
    REQUIRE(ReconcilePulledSharedFolder(staging.string(), host.string(), snapshot.string(), &error, &changes));
    CHECK(error.empty());

    REQUIRE(changes.DeviceDeletedPaths.size() == 1);
    CHECK(changes.DeviceDeletedPaths[0] == "deleted-on-host.txt");
    CHECK(changes.ConflictPaths.empty());
    CHECK_FALSE(fs::exists(host / "deleted-on-host.txt"));
}

TEST_CASE("shared folder reconcile deletes only the file and keeps parent directories inside the share", "[shared_folder][sync]") {
    TempTree temp("reconcile_host_deleted_parent_kept");
    const fs::path base = temp.Path / "base";
    const fs::path staging = temp.Path / "staging";
    const fs::path host = temp.Path / "host";
    const fs::path snapshot = temp.Path / "snapshot.txt";

    WriteTextFile(base / "nested" / "deleted.txt", "base");
    WriteTextFile(host / "nested" / "deleted.txt", "base");
    WriteSnapshot(snapshot, base, {"nested/deleted.txt"});

    SharedFolderReconcileChanges changes;
    std::string error;
    REQUIRE(ReconcilePulledSharedFolder(staging.string(), host.string(), snapshot.string(), &error, &changes));
    CHECK(error.empty());

    CHECK_FALSE(fs::exists(host / "nested" / "deleted.txt"));
    CHECK(fs::exists(host / "nested"));
    REQUIRE(changes.DeviceDeletedPaths.size() == 1);
    CHECK(changes.DeviceDeletedPaths[0] == "nested/deleted.txt");
}

TEST_CASE("shared folder reconcile keeps both copies when host and device both change", "[shared_folder][sync]") {
    TempTree temp("reconcile_conflict");
    const fs::path base = temp.Path / "base";
    const fs::path staging = temp.Path / "staging";
    const fs::path host = temp.Path / "host";
    const fs::path snapshot = temp.Path / "snapshot.txt";

    WriteTextFile(base / "both.txt", "base");
    WriteTextFile(host / "both.txt", "host changed");
    WriteTextFile(staging / "both.txt", "device changed");
    WriteSnapshot(snapshot, base, {"both.txt"});

    SharedFolderReconcileChanges changes;
    std::string error;
    REQUIRE(ReconcilePulledSharedFolder(staging.string(), host.string(), snapshot.string(), &error, &changes));
    CHECK(error.empty());

    CHECK(ReadTextFile(host / "both.txt") == "host changed");
    REQUIRE(changes.ConflictPaths.size() == 1);
    CHECK(changes.ConflictPaths[0] == "both.conflict-device.txt");
    CHECK(ReadTextFile(host / "both.conflict-device.txt") == "device changed");
}

TEST_CASE("shared folder reconcile saves a host edit as a conflict when the device deletes it", "[shared_folder][sync]") {
    TempTree temp("reconcile_host_edit_device_delete");
    const fs::path base = temp.Path / "base";
    const fs::path staging = temp.Path / "staging";
    const fs::path host = temp.Path / "host";
    const fs::path snapshot = temp.Path / "snapshot.txt";

    WriteTextFile(base / "deleted-on-device.txt", "base");
    WriteTextFile(host / "deleted-on-device.txt", "host changed");
    WriteSnapshot(snapshot, base, {"deleted-on-device.txt"});

    SharedFolderReconcileChanges changes;
    std::string error;
    REQUIRE(ReconcilePulledSharedFolder(staging.string(), host.string(), snapshot.string(), &error, &changes));
    CHECK(error.empty());

    CHECK_FALSE(fs::exists(host / "deleted-on-device.txt"));
    REQUIRE(changes.ConflictPaths.size() == 1);
    CHECK(changes.ConflictPaths[0] == "deleted-on-device.conflict-host.txt");
    CHECK(ReadTextFile(host / "deleted-on-device.conflict-host.txt") == "host changed");
    CHECK(changes.DeviceDeletedPaths.empty());
}

TEST_CASE("shared folder reconcile saves a device edit as a conflict when the host deletes it", "[shared_folder][sync]") {
    TempTree temp("reconcile_device_edit_host_delete");
    const fs::path base = temp.Path / "base";
    const fs::path staging = temp.Path / "staging";
    const fs::path host = temp.Path / "host";
    const fs::path snapshot = temp.Path / "snapshot.txt";

    WriteTextFile(base / "deleted-on-host.txt", "base");
    WriteTextFile(staging / "deleted-on-host.txt", "device changed");
    WriteSnapshot(snapshot, base, {"deleted-on-host.txt"});

    SharedFolderReconcileChanges changes;
    std::string error;
    REQUIRE(ReconcilePulledSharedFolder(staging.string(), host.string(), snapshot.string(), &error, &changes));
    CHECK(error.empty());

    CHECK_FALSE(fs::exists(host / "deleted-on-host.txt"));
    REQUIRE(changes.ConflictPaths.size() == 1);
    CHECK(changes.ConflictPaths[0] == "deleted-on-host.conflict-device.txt");
    CHECK(ReadTextFile(host / "deleted-on-host.conflict-device.txt") == "device changed");
    REQUIRE(changes.DeviceDeletedPaths.size() == 1);
    CHECK(changes.DeviceDeletedPaths[0] == "deleted-on-host.txt");
}
