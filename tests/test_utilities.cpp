#include <catch2/catch_test_macros.hpp>

#include "core/utilities.h"
#include "core/process.h"
#include <filesystem>
#include <fstream>
#include <chrono>

using namespace CoreDeck;

TEST_CASE("Directory sizes ignore missing files and count nested files", "[utilities][storage]") {
    const auto path = std::filesystem::temp_directory_path() /
        ("coredeck-storage-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    CHECK(GetDirectorySize(path.string()) == 0);
    std::filesystem::create_directories(path / "nested");
    { std::ofstream file(path / "one"); file << "abc"; }
    { std::ofstream file(path / "nested" / "two"); file << "12345"; }
    CHECK(GetDirectorySize(path.string()) == 8);
    std::filesystem::remove_all(path);
}

#if !defined(_WIN32)
TEST_CASE("Closed child stdin does not terminate the application", "[utilities][process]") {
    // Larger than the pipe buffer: the write must observe the child closing stdin.
    CHECK(StreamCommandArgs("/bin/sh", {"-c", "exec 0<&-; exit 0"}, std::string(1024 * 1024, 'x'), {}));
    CHECK_FALSE(StreamCommandArgs("/nonexistent/coredeck-command", {}, "no\n", {}));
}
#else
TEST_CASE("Batch commands preserve shell metacharacters in paths and arguments", "[utilities][process]") {
    const auto directory = std::filesystem::temp_directory_path() /
        ("coredeck&batch-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(directory);
    const auto script = directory / "echo.cmd";
    { std::ofstream file(script); file << "@echo off\r\necho %1\r\n"; }
    CHECK(RunCommandArgs(script.string(), {"a&b%PATH%!value^()"}) == "\"a&b%PATH%!value^()\"\n");
    std::filesystem::remove_all(directory);
}
#endif

TEST_CASE("FormatFileSize renders bytes under 1 KB as raw B", "[utilities][format]") {
    REQUIRE(FormatFileSize(0) == "0 B");
    REQUIRE(FormatFileSize(1) == "1 B");
    REQUIRE(FormatFileSize(1023) == "1023 B");
}

TEST_CASE("FormatFileSize renders KB/MB/GB with the right precision", "[utilities][format]") {
    REQUIRE(FormatFileSize(1024) == "1.0 KB");
    REQUIRE(FormatFileSize(1536) == "1.5 KB");
    REQUIRE(FormatFileSize(1024ULL * 1024) == "1.0 MB");
    REQUIRE(FormatFileSize(1024ULL * 1024 * 5) == "5.0 MB");
    REQUIRE(FormatFileSize(1024ULL * 1024 * 1024) == "1.00 GB");
    REQUIRE(FormatFileSize((1024ULL * 1024 * 1024 * 3) + (1024ULL * 1024 * 512)) == "3.50 GB");
}

TEST_CASE("FormatFileSize picks the largest fitting unit at boundaries", "[utilities][format]") {
    REQUIRE(FormatFileSize((1024 * 1024) - 1) == "1024.0 KB");
    REQUIRE(FormatFileSize((1024ULL * 1024 * 1024) - 1) == "1024.0 MB");
}

TEST_CASE("StrConcat joins heterogeneous string-like inputs", "[utilities][strconcat]") {
    REQUIRE(StrConcat("hello", " ", "world") == "hello world");
    REQUIRE(StrConcat(std::string("foo"), "-", std::string("bar")) == "foo-bar");
}

TEST_CASE("StrConcat with a single argument passes it through", "[utilities][strconcat]") {
    REQUIRE(StrConcat("only") == "only");
}
