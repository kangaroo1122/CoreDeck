#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

#include "core/sdk_install_session.h"

using namespace CoreDeck;

namespace {
    std::filesystem::path SessionTempDir() {
        const auto suffix = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
        const auto path = std::filesystem::temp_directory_path() / ("coredeck_sdk_session_" + suffix);
        std::filesystem::create_directories(path);
        return path;
    }
}

TEST_CASE("fresh SDK install session uses staging for every operation", "[sdk_session]") {
    const auto parent = SessionTempDir();
    const auto target = parent / "sdk";
    SdkInstallSession session;
    std::string error;

    REQUIRE(session.BeginFresh(target, error));
    CHECK(session.TargetRoot() == target);
    CHECK(session.ActiveRoot() != target);
    CHECK(std::filesystem::is_directory(session.ActiveRoot()));

    std::filesystem::remove_all(parent);
}

TEST_CASE("cancelling a fresh SDK install session removes staging state", "[sdk_session]") {
    const auto parent = SessionTempDir();
    const auto target = parent / "sdk";
    SdkInstallSession session;
    std::string error;
    REQUIRE(session.BeginFresh(target, error));
    const auto staging = session.ActiveRoot();

    session.Reset();

    CHECK(session.ActiveRoot().empty());
    CHECK_FALSE(std::filesystem::exists(staging));
    std::filesystem::remove_all(parent);
}

TEST_CASE("switching to an existing SDK clears a previous fresh transaction", "[sdk_session]") {
    const auto parent = SessionTempDir();
    const auto target = parent / "new-sdk";
    const auto existing = parent / "existing-sdk";
    std::filesystem::create_directories(existing);
    SdkInstallSession session;
    std::string error;
    REQUIRE(session.BeginFresh(target, error));
    const auto staging = session.ActiveRoot();

    session.UseExisting(existing);

    CHECK(session.TargetRoot() == existing);
    CHECK(session.ActiveRoot() == existing);
    CHECK_FALSE(std::filesystem::exists(staging));
    std::filesystem::remove_all(parent);
}
