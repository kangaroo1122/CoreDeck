#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>

#include "core/avd.h"

namespace fs = std::filesystem;
using namespace CoreDeck;

namespace {
    fs::path UniqueTempDir(const std::string &prefix) {
        std::random_device rd;
        const fs::path base = fs::temp_directory_path() / (prefix + "_" + std::to_string(rd()));
        fs::create_directories(base);
        return base;
    }

    std::string ReadFile(const fs::path &path) {
        std::ifstream file(path);
        return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
    }
}

TEST_CASE("SetAvdDisplayName updates an existing display name", "[avd][display_name]") {
    const fs::path avdDir = UniqueTempDir("coredeck_avd_rename_update");
    const fs::path config = avdDir / "config.ini";
    std::ofstream(config) << "hw.device.name=pixel_8\navd.ini.displayname=Old Name\nhw.ramSize=2048\n";

    REQUIRE(SetAvdDisplayName(avdDir.string(), "New Name"));

    const std::string saved = ReadFile(config);
    REQUIRE(saved.find("hw.device.name=pixel_8\n") != std::string::npos);
    REQUIRE(saved.find("avd.ini.displayname=New Name\n") != std::string::npos);
    REQUIRE(saved.find("hw.ramSize=2048\n") != std::string::npos);

    fs::remove_all(avdDir);
}

TEST_CASE("SetAvdDisplayName adds a missing display name", "[avd][display_name]") {
    const fs::path avdDir = UniqueTempDir("coredeck_avd_rename_add");
    const fs::path config = avdDir / "config.ini";
    std::ofstream(config) << "hw.device.name=pixel_8\nhw.ramSize=2048\n";

    REQUIRE(SetAvdDisplayName(avdDir.string(), "Friendly Name"));

    const std::string saved = ReadFile(config);
    REQUIRE(saved.find("hw.device.name=pixel_8\n") != std::string::npos);
    REQUIRE(saved.find("hw.ramSize=2048\n") != std::string::npos);
    REQUIRE(saved.find("avd.ini.displayname=Friendly Name\n") != std::string::npos);

    fs::remove_all(avdDir);
}

TEST_CASE("SetAvdDisplayName removes the display name when empty", "[avd][display_name]") {
    const fs::path avdDir = UniqueTempDir("coredeck_avd_rename_remove");
    const fs::path config = avdDir / "config.ini";
    std::ofstream(config) << "hw.device.name=pixel_8\navd.ini.displayname=Old Name\nhw.ramSize=2048\n";

    REQUIRE(SetAvdDisplayName(avdDir.string(), ""));

    const std::string saved = ReadFile(config);
    REQUIRE(saved.find("hw.device.name=pixel_8\n") != std::string::npos);
    REQUIRE(saved.find("avd.ini.displayname=") == std::string::npos);
    REQUIRE(saved.find("hw.ramSize=2048\n") != std::string::npos);

    fs::remove_all(avdDir);
}

TEST_CASE("ListAvdSnapshots returns snapshot directories with size and newest-first ordering", "[avd][snapshots]") {
    const fs::path avdDir = UniqueTempDir("coredeck_avd_snapshots_list");
    const fs::path snapshotsDir = avdDir / "snapshots";
    const fs::path olderSnapshot = snapshotsDir / "older";
    const fs::path newerSnapshot = snapshotsDir / "newer";
    fs::create_directories(olderSnapshot);
    fs::create_directories(newerSnapshot);
    std::ofstream(olderSnapshot / "state.bin") << "12345";
    std::ofstream(newerSnapshot / "state.bin") << "123456789";
    std::ofstream(snapshotsDir / "not-a-snapshot.bin") << "ignored";

    const auto now = fs::file_time_type::clock::now();
    fs::last_write_time(olderSnapshot / "state.bin", now - std::chrono::hours(2));
    fs::last_write_time(newerSnapshot / "state.bin", now - std::chrono::hours(1));

    std::string error;
    const auto snapshots = ListAvdSnapshots(avdDir.string(), &error);

    REQUIRE(error.empty());
    REQUIRE(snapshots.size() == 2);
    REQUIRE(snapshots[0].Name == "newer");
    REQUIRE(snapshots[0].SizeBytes == 9);
    REQUIRE(snapshots[0].ModifiedEpochSeconds > 0);
    REQUIRE(snapshots[1].Name == "older");
    REQUIRE(snapshots[1].SizeBytes == 5);

    fs::remove_all(avdDir);
}

#if !defined(_WIN32)
TEST_CASE("ListAvdSnapshots reports an unreadable snapshots directory", "[avd][snapshots]") {
    const fs::path avdDir = UniqueTempDir("coredeck_avd_snapshots_unreadable");
    const fs::path snapshotsDir = avdDir / "snapshots";
    fs::create_directories(snapshotsDir);
    fs::permissions(snapshotsDir, fs::perms::none);

    std::string error;
    const auto snapshots = ListAvdSnapshots(avdDir.string(), &error);

    fs::permissions(snapshotsDir, fs::perms::owner_all);
    fs::remove_all(avdDir);

    REQUIRE(snapshots.empty());
    REQUIRE_FALSE(error.empty());
}
#endif

TEST_CASE("DeleteAvdSnapshot removes only a safe snapshot directory", "[avd][snapshots]") {
    const fs::path avdDir = UniqueTempDir("coredeck_avd_snapshots_delete");
    const fs::path snapshotsDir = avdDir / "snapshots";
    const fs::path keepSnapshot = snapshotsDir / "keep";
    const fs::path deleteSnapshot = snapshotsDir / "delete-me";
    const fs::path outside = avdDir / "outside";
    fs::create_directories(keepSnapshot);
    fs::create_directories(deleteSnapshot);
    fs::create_directories(outside);
    std::ofstream(keepSnapshot / "state.bin") << "keep";
    std::ofstream(deleteSnapshot / "state.bin") << "delete";
    std::ofstream(outside / "state.bin") << "outside";

    std::string error;
    REQUIRE(DeleteAvdSnapshot(avdDir.string(), "delete-me", &error));
    REQUIRE(error.empty());
    REQUIRE_FALSE(fs::exists(deleteSnapshot));
    REQUIRE(fs::exists(keepSnapshot));

    REQUIRE_FALSE(DeleteAvdSnapshot(avdDir.string(), "../outside", &error));
    REQUIRE_FALSE(error.empty());
    REQUIRE(fs::exists(outside));

    fs::remove_all(avdDir);
}

#if !defined(_WIN32)
TEST_CASE("DeleteAvdSnapshot rejects a snapshots directory symlink", "[avd][snapshots]") {
    const fs::path avdDir = UniqueTempDir("coredeck_avd_snapshots_symlink");
    const fs::path outsideDir = UniqueTempDir("coredeck_avd_snapshots_outside");
    const fs::path outsideSnapshot = outsideDir / "victim";
    fs::create_directories(outsideSnapshot);
    std::ofstream(outsideSnapshot / "state.bin") << "keep";
    fs::create_directory_symlink(outsideDir, avdDir / "snapshots");

    std::string error;
    const bool deleted = DeleteAvdSnapshot(avdDir.string(), "victim", &error);
    const bool outsideSnapshotExists = fs::exists(outsideSnapshot);

    fs::remove_all(avdDir);
    fs::remove_all(outsideDir);

    REQUIRE_FALSE(deleted);
    REQUIRE_FALSE(error.empty());
    REQUIRE(outsideSnapshotExists);
}
#endif
