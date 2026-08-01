#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>

#include "core/update_download.h"

using namespace CoreDeck::detail;

TEST_CASE("ParseSha256Checksum accepts sha256sum output", "[update_download][checksum]") {
    const auto checksum = ParseSha256Checksum(
        "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad  coredeck.msi\n",
        "coredeck.msi"
    );
    REQUIRE(checksum.has_value());
    REQUIRE(*checksum == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
}

TEST_CASE("ParseSha256Checksum rejects a different filename", "[update_download][checksum]") {
    REQUIRE_FALSE(ParseSha256Checksum(
        "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad  other.msi\n",
        "coredeck.msi"
    ).has_value());
}

TEST_CASE("Sha256File hashes deterministic local data", "[update_download][checksum]") {
    const auto path = std::filesystem::temp_directory_path() / "coredeck-sha256-test.txt";
    {
        std::ofstream file(path, std::ios::binary);
        file << "abc";
    }
    REQUIRE(Sha256File(path.string()) == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    std::error_code ec;
    std::filesystem::remove(path, ec);
}
