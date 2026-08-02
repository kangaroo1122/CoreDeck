#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <memory>

#include "core/sdk_progress.h"

using namespace CoreDeck;

TEST_CASE("SDK progress maps local values into the active phase", "[sdk_progress]") {
    const auto progress = std::make_shared<SdkOperationProgress>();
    SetSdkProgressRange(progress, 0.42F, 0.48F);

    ReportSdkProgress(progress, 0.5F, "Fetching packages");

    std::lock_guard lock(progress->Mutex);
    CHECK(progress->Percent == Catch::Approx(0.45F));
    CHECK(progress->StatusText == "Fetching packages");
}

TEST_CASE("SDK progress is clamped and never moves backwards", "[sdk_progress]") {
    const auto progress = std::make_shared<SdkOperationProgress>();
    SetSdkProgressRange(progress, 0.48F, 0.97F);
    ReportSdkProgress(progress, 0.8F, "Installing");
    ReportSdkProgress(progress, 0.2F, "Installing another package");
    ReportSdkProgress(progress, 2.0F, "Installed");

    std::lock_guard lock(progress->Mutex);
    CHECK(progress->Percent == Catch::Approx(0.97F));
    CHECK(progress->StatusText == "Installed");
}

TEST_CASE("SDK progress defaults to the full range for standalone operations", "[sdk_progress]") {
    const auto progress = std::make_shared<SdkOperationProgress>();

    ReportSdkProgress(progress, 0.25F, "Working");

    std::lock_guard lock(progress->Mutex);
    CHECK(progress->Percent == Catch::Approx(0.25F));
}

TEST_CASE("SDK progress derives child phases from a captured parent range", "[sdk_progress]") {
    const auto progress = std::make_shared<SdkOperationProgress>();
    SetSdkProgressRange(progress, 0.42F, 0.97F);
    const SdkProgressRange parent = GetSdkProgressRange(progress);

    SetSdkProgressSubrange(progress, parent, 0.0F, 6.0F / 55.0F);
    ReportSdkProgress(progress, 1.0F, "Fetched");
    {
        std::lock_guard lock(progress->Mutex);
        CHECK(progress->Percent == Catch::Approx(0.48F));
    }

    SetSdkProgressSubrange(progress, parent, 6.0F / 55.0F, 1.0F);
    ReportSdkProgress(progress, 1.0F, "Installed");
    std::lock_guard lock(progress->Mutex);
    CHECK(progress->Percent == Catch::Approx(0.97F));
}

TEST_CASE("SDK progress maps sequential operation subranges without stalling", "[sdk_progress]") {
    const auto progress = std::make_shared<SdkOperationProgress>();
    SetSdkProgressRange(progress, 0.0F, 0.35F);

    ReportSdkProgressInSubrange(progress, {0.02F, 0.05F}, 1.0F, "Metadata fetched");
    ReportSdkProgressInSubrange(progress, {0.05F, 0.74F}, 0.5F, "Downloading tools");
    {
        std::lock_guard lock(progress->Mutex);
        CHECK(progress->Percent == Catch::Approx(0.13825F));
    }

    ReportSdkProgressInSubrange(progress, {0.74F, 0.88F}, 0.5F, "Extracting tools");
    std::lock_guard lock(progress->Mutex);
    CHECK(progress->Percent == Catch::Approx(0.2835F));
}
