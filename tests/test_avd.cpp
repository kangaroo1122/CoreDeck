#include <catch2/catch_test_macros.hpp>

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
