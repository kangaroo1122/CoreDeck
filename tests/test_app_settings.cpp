#include <catch2/catch_test_macros.hpp>
#include <rfl.hpp>
#include <rfl/json.hpp>

#include <string>

#include "core/app_settings_types.h"

using namespace CoreDeck;

TEST_CASE("AppSettings enables wipe-and-run confirmation by default", "[app_settings][defaults]") {
    const AppSettings settings;
    REQUIRE(settings.ConfirmBeforeWipeAndRun);
}

TEST_CASE("AppSettings keeps old settings and defaults missing new fields", "[app_settings][migration]") {
    const std::string oldSettingsJson = R"({
        "SchemaVersion": 1,
        "AutoScroll": false,
        "ConfirmBeforeDeleteAvd": false,
        "CrashReportingEnabled": false,
        "ShowAvdListPanel": false,
        "ShowOptionsPanel": true,
        "ShowDetailsPanel": false,
        "ShowLogPanel": true,
        "AvdSortMode": 2,
        "AvdSortAscending": false
    })";

    const auto settings = rfl::json::read<AppSettings, rfl::DefaultIfMissing>(oldSettingsJson).value();

    REQUIRE_FALSE(settings.AutoScroll);
    REQUIRE_FALSE(settings.ConfirmBeforeDeleteAvd);
    REQUIRE(settings.ConfirmBeforeWipeAndRun);
    REQUIRE_FALSE(settings.CrashReportingEnabled);
    REQUIRE_FALSE(settings.ShowAvdListPanel);
    REQUIRE(settings.ShowOptionsPanel);
    REQUIRE_FALSE(settings.ShowDetailsPanel);
    REQUIRE(settings.ShowLogPanel);
    REQUIRE(settings.AvdSortMode == 2);
    REQUIRE_FALSE(settings.AvdSortAscending);
}
