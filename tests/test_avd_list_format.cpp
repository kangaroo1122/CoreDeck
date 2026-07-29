#include <catch2/catch_test_macros.hpp>

#include "gui/windows/avd_list_format.h"

using namespace CoreDeck;

TEST_CASE("AVD list metadata prefixes API level", "[avd][list]") {
    REQUIRE(FormatAvdListMetadata("35", "Google Play", "Ready") == "API 35 - Google Play - Ready");
}

TEST_CASE("AVD list metadata omits unavailable API level", "[avd][list]") {
    REQUIRE(FormatAvdListMetadata("", "Default", "Ready") == "Default - Ready");
}
