#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

#include "core/sdk_install_transaction.h"

using namespace CoreDeck;

namespace {
    std::filesystem::path TransactionTempDir() {
        const auto suffix = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
        const auto path = std::filesystem::temp_directory_path() / ("coredeck_sdk_transaction_" + suffix);
        std::filesystem::create_directories(path);
        return path;
    }
}

TEST_CASE("fresh SDK transaction installs through a sibling staging root", "[sdk_transaction]") {
    const auto parent = TransactionTempDir();
    const auto target = parent / "sdk";
    std::string error;

    auto transaction = SdkInstallTransaction::Begin(target, error);

    REQUIRE(transaction != nullptr);
    CHECK(transaction->StagingRoot().parent_path() == parent);
    CHECK(transaction->StagingRoot() != target);
    std::filesystem::create_directories(transaction->StagingRoot() / "platform-tools");
    std::ofstream(transaction->StagingRoot() / "platform-tools" / "adb") << "adb";
    REQUIRE(transaction->Commit(error));
    CHECK(std::filesystem::exists(target / "platform-tools" / "adb"));
    CHECK_FALSE(std::filesystem::exists(transaction->StagingRoot()));

    std::filesystem::remove_all(parent);
}

TEST_CASE("fresh SDK transaction rollback preserves an empty target", "[sdk_transaction]") {
    const auto parent = TransactionTempDir();
    const auto target = parent / "sdk";
    std::filesystem::create_directories(target);
    std::filesystem::path staging;
    std::string error;
    {
        auto transaction = SdkInstallTransaction::Begin(target, error);
        REQUIRE(transaction != nullptr);
        staging = transaction->StagingRoot();
        std::ofstream(staging / "partial") << "partial";
    }

    CHECK(std::filesystem::is_directory(target));
    CHECK(std::filesystem::is_empty(target));
    CHECK_FALSE(std::filesystem::exists(staging));
    std::filesystem::remove_all(parent);
}

TEST_CASE("fresh SDK transaction rejects a non-empty target", "[sdk_transaction]") {
    const auto parent = TransactionTempDir();
    const auto target = parent / "sdk";
    std::filesystem::create_directories(target);
    std::ofstream(target / "keep.txt") << "keep";
    std::string error;

    CHECK(SdkInstallTransaction::Begin(target, error) == nullptr);
    CHECK_FALSE(error.empty());
    CHECK(std::filesystem::exists(target / "keep.txt"));
    std::filesystem::remove_all(parent);
}
