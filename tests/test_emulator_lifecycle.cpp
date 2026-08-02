#include <catch2/catch_test_macros.hpp>

#include "core/emulator.h"

using namespace CoreDeck;

TEST_CASE("stopping emulator instances cannot be relaunched", "[emulator][lifecycle]") {
    EmulatorInstance instance;
    instance.IsRunning = false;
    instance.Stopping = true;

    CHECK_FALSE(detail::CanLaunchEmulatorInstance(instance));
}

TEST_CASE("stop completion only applies to the same emulator instance", "[emulator][lifecycle]") {
    EmulatorInstance instance;
    instance.Pid = 200;
    instance.Generation = 8;

    CHECK(detail::IsCurrentEmulatorInstance(instance, 200, 8));
    CHECK_FALSE(detail::IsCurrentEmulatorInstance(instance, 201, 8));
    CHECK_FALSE(detail::IsCurrentEmulatorInstance(instance, 200, 9));
}
