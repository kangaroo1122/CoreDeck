#include <catch2/catch_test_macros.hpp>

#include <string>

#include "core/sdk_repository.h"

using namespace CoreDeck;

TEST_CASE("SDK repository parser handles namespace prefixes and reordered nodes", "[sdk_repository]") {
    const std::string xml = R"xml(
        <sdk:sdk-repository xmlns:sdk="urn:test">
            <sdk:remotePackage path="cmdline-tools;latest">
                <sdk:archives>
                    <sdk:archive>
                        <sdk:host-arch>aarch64</sdk:host-arch>
                        <sdk:complete>
                            <sdk:url>commandlinetools-mac.zip</sdk:url>
                            <sdk:checksum type="sha1">
                                0123456789abcdef0123456789abcdef01234567
                            </sdk:checksum>
                            <sdk:size>
                                42
                            </sdk:size>
                        </sdk:complete>
                        <sdk:host-os>macosx</sdk:host-os>
                    </sdk:archive>
                </sdk:archives>
                <sdk:revision><sdk:minor>2</sdk:minor><sdk:major>20</sdk:major></sdk:revision>
            </sdk:remotePackage>
        </sdk:sdk-repository>
    )xml";

    const auto package = ParseCommandLineToolsRepository(xml, "macosx", "aarch64");

    REQUIRE(package.has_value());
    CHECK(package->DownloadUrl == "https://dl.google.com/android/repository/commandlinetools-mac.zip");
    CHECK(package->Sha1 == "0123456789abcdef0123456789abcdef01234567");
    CHECK(package->SizeBytes == 42);
}

TEST_CASE("SDK repository parser selects the newest non-obsolete matching package", "[sdk_repository]") {
    const std::string xml = R"xml(
        <repository>
            <remotePackage path="cmdline-tools;latest">
                <revision><major>19</major><minor>9</minor></revision>
                <archives><archive><host-os>linux</host-os><complete><url>old.zip</url><size>10</size><checksum type="sha1">1111111111111111111111111111111111111111</checksum></complete></archive></archives>
            </remotePackage>
            <remotePackage obsolete="true" path="cmdline-tools;latest">
                <revision><major>99</major></revision>
                <archives><archive><host-os>linux</host-os><complete><url>obsolete.zip</url><size>99</size><checksum type="sha1">9999999999999999999999999999999999999999</checksum></complete></archive></archives>
            </remotePackage>
            <remotePackage path="cmdline-tools;latest">
                <revision><major>20</major></revision>
                <archives><archive><host-os>linux</host-os><complete><url>new.zip</url><size>20</size><checksum type="sha1">2222222222222222222222222222222222222222</checksum></complete></archive></archives>
            </remotePackage>
        </repository>
    )xml";

    const auto package = ParseCommandLineToolsRepository(xml, "linux");

    REQUIRE(package.has_value());
    CHECK(package->DownloadUrl.ends_with("/new.zip"));
}

TEST_CASE("SDK repository parser rejects malformed or incomplete metadata", "[sdk_repository]") {
    SECTION("malformed XML") {
        CHECK_FALSE(ParseCommandLineToolsRepository("<repository><remotePackage>", "linux").has_value());
    }
    SECTION("wrong checksum type") {
        const std::string xml = R"xml(
            <repository><remotePackage path="cmdline-tools;latest"><revision><major>1</major></revision>
            <archives><archive><host-os>linux</host-os><complete><url>x.zip</url><size>1</size><checksum type="sha256">hash</checksum></complete></archive></archives>
            </remotePackage></repository>)xml";
        CHECK_FALSE(ParseCommandLineToolsRepository(xml, "linux").has_value());
    }
    SECTION("invalid size") {
        const std::string xml = R"xml(
            <repository><remotePackage path="cmdline-tools;latest"><revision><major>1</major></revision>
            <archives><archive><host-os>linux</host-os><complete><url>x.zip</url><size>not-a-number</size><checksum type="sha1">hash</checksum></complete></archive></archives>
            </remotePackage></repository>)xml";
        CHECK_FALSE(ParseCommandLineToolsRepository(xml, "linux").has_value());
    }
    SECTION("invalid SHA-1") {
        const std::string xml = R"xml(
            <repository><remotePackage path="cmdline-tools;latest"><revision><major>1</major></revision>
            <archives><archive><host-os>linux</host-os><complete><url>x.zip</url><size>1</size><checksum type="sha1">not-a-sha1</checksum></complete></archive></archives>
            </remotePackage></repository>)xml";
        CHECK_FALSE(ParseCommandLineToolsRepository(xml, "linux").has_value());
    }
    SECTION("host architecture mismatch") {
        const std::string xml = R"xml(
            <repository><remotePackage path="cmdline-tools;latest"><revision><major>1</major></revision>
            <archives><archive><host-os>macosx</host-os><host-arch>x64</host-arch><complete><url>x.zip</url><size>1</size><checksum type="sha1">hash</checksum></complete></archive></archives>
            </remotePackage></repository>)xml";
        CHECK_FALSE(ParseCommandLineToolsRepository(xml, "macosx", "aarch64").has_value());
    }
}
