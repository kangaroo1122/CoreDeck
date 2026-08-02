#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <string>

#include "core/sha256.h"

using namespace CoreDeck;

TEST_CASE("SHA-256 matches standard test vectors", "[sha256]") {
    REQUIRE(Sha256Hex("") == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    REQUIRE(Sha256Hex("abc") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    REQUIRE(
        Sha256Hex("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq") ==
        "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1"
    );
}

TEST_CASE("SHA-256 handles input spanning many blocks", "[sha256]") {
    REQUIRE(
        Sha256Hex(std::string(1000000, 'a')) ==
        "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0"
    );
}

TEST_CASE("SHA-256 hashes file contents", "[sha256][file]") {
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / "coredeck_sha256_test.bin";
    {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        out << "abc";
    }

    REQUIRE(Sha256File(path.string()) == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");

    std::filesystem::remove(path);
}

TEST_CASE("SHA-256 returns empty for a missing file", "[sha256][file]") {
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / "coredeck_sha256_missing.bin";
    std::filesystem::remove(path);

    REQUIRE(Sha256File(path.string()).empty());
}

TEST_CASE("SHA-256 hexadecimal comparison is case insensitive", "[sha256]") {
    REQUIRE(EqualsIgnoreCaseHex("ABCDEF", "abcdef"));
    REQUIRE_FALSE(EqualsIgnoreCaseHex("abcdef", "abcde"));
    REQUIRE_FALSE(EqualsIgnoreCaseHex("abcdef", "abcdee"));
}
