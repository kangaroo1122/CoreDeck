#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <miniz.h>

#include "core/archive.h"

using namespace CoreDeck;

namespace {
    std::filesystem::path MakeTempDir(const std::string &prefix) {
        const auto dir = std::filesystem::temp_directory_path() /
                         (prefix + "_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directories(dir);
        return dir;
    }

    bool MakeZip(const std::filesystem::path &path, const std::vector<std::pair<std::string, std::string>> &files) {
        mz_zip_archive zip = {};
        if (!mz_zip_writer_init_file(&zip, path.string().c_str(), 0)) {
            return false;
        }
        for (const auto &[name, contents]: files) {
            if (!mz_zip_writer_add_mem(&zip, name.c_str(), contents.data(), contents.size(), MZ_DEFAULT_COMPRESSION)) {
                mz_zip_writer_end(&zip);
                return false;
            }
        }
        const bool ok = mz_zip_writer_finalize_archive(&zip) && mz_zip_writer_end(&zip);
        return ok;
    }
}

TEST_CASE("ExtractZip strips Google's top-level directory and restores bin executability", "[archive]") {
    const std::filesystem::path root = MakeTempDir("coredeck_archive");
    const std::filesystem::path zip = root / "tools.zip";
    const std::filesystem::path destination = root / "extracted";
    REQUIRE(MakeZip(zip, {{"cmdline-tools/bin/sdkmanager", "#!/bin/sh\n"}, {"cmdline-tools/lib/tool.jar", "jar"}}));

    std::string error;
    REQUIRE(ExtractZip(zip.string(), destination.string(), ExtractOptions{.StripTopLevelDir = true}, nullptr, error));
    REQUIRE(std::filesystem::exists(destination / "bin" / "sdkmanager"));
    REQUIRE(std::filesystem::exists(destination / "lib" / "tool.jar"));

#if !defined(_WIN32)
    const auto permissions = std::filesystem::status(destination / "bin" / "sdkmanager").permissions();
    REQUIRE((permissions & std::filesystem::perms::owner_exec) != std::filesystem::perms::none);
#endif

    std::filesystem::remove_all(root);
}

TEST_CASE("ExtractZip rejects unsafe archive paths", "[archive][security]") {
    const std::filesystem::path root = MakeTempDir("coredeck_archive_unsafe");
    const std::filesystem::path zip = root / "unsafe.zip";
    const std::filesystem::path destination = root / "extracted";
    REQUIRE(MakeZip(zip, {{"cmdline-tools/../escape.txt", "no"}}));

    std::string error;
    REQUIRE_FALSE(ExtractZip(zip.string(), destination.string(), {}, nullptr, error));
    REQUIRE(error.find("unsafe") != std::string::npos);
    REQUIRE_FALSE(std::filesystem::exists(root / "escape.txt"));

    std::filesystem::remove_all(root);
}

TEST_CASE("ExtractZip revalidates paths after stripping the top-level directory", "[archive][security]") {
    const std::filesystem::path root = MakeTempDir("coredeck_archive_stripped_unsafe");
    const std::filesystem::path zip = root / "unsafe.zip";
    const std::filesystem::path destination = root / "extracted";
    const std::filesystem::path escaped = root / "escaped.txt";
    const std::string unsafeName = "top/" + escaped.generic_string();
    REQUIRE(MakeZip(zip, {{unsafeName, "no"}}));

    std::string error;
    CHECK_FALSE(ExtractZip(
        zip.string(),
        destination.string(),
        ExtractOptions{.StripTopLevelDir = true},
        nullptr,
        error
    ));
    CHECK(error.find("unsafe") != std::string::npos);
    CHECK_FALSE(std::filesystem::exists(escaped));

    std::filesystem::remove_all(root);
}

TEST_CASE("ExtractZip reports corrupt archives", "[archive]") {
    const std::filesystem::path root = MakeTempDir("coredeck_archive_corrupt");
    const std::filesystem::path zip = root / "corrupt.zip";
    std::ofstream(zip, std::ios::binary) << "not a zip";

    std::string error;
    REQUIRE_FALSE(ExtractZip(zip.string(), (root / "extracted").string(), {}, nullptr, error));
    REQUIRE_FALSE(error.empty());

    std::filesystem::remove_all(root);
}

TEST_CASE("ExtractZip supports cancellation during entry processing", "[archive][cancel]") {
    const std::filesystem::path root = MakeTempDir("coredeck_archive_cancel");
    const std::filesystem::path zip = root / "tools.zip";
    REQUIRE(MakeZip(zip, {{"cmdline-tools/a", "a"}, {"cmdline-tools/b", "b"}}));

    std::string error;
    REQUIRE_FALSE(ExtractZip(
        zip.string(),
        (root / "extracted").string(),
        ExtractOptions{.StripTopLevelDir = true},
        [](const float) { return false; },
        error
    ));
    REQUIRE(error.find("cancel") != std::string::npos);

    std::filesystem::remove_all(root);
}
