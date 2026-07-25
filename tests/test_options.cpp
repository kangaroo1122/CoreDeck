#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

#include "core/options.h"

using namespace CoreDeck;

TEST_CASE("BuildArgs ignores emulator port flags managed by CoreDeck", "[options][ports]") {
    EmulatorOption gpu;
    gpu.Flag = "-gpu";
    gpu.Enabled = true;
    gpu.Type = OptionType::Selection;
    gpu.Items = {"auto", "host"};
    gpu.SelectedItem = 1;

    EmulatorOption port;
    port.Flag = "-port";
    port.Enabled = true;
    port.Type = OptionType::TextInput;
    port.InputValue = "5554";

    EmulatorOption ports;
    ports.Flag = "-ports";
    ports.Enabled = true;
    ports.Type = OptionType::TextInput;
    ports.InputValue = "5554,5555";

    const std::vector<std::string> args = BuildArgs("Pixel_API_35", {gpu, port, ports});

    REQUIRE(args == std::vector<std::string>{"-avd", "Pixel_API_35", "-gpu", "host"});
}
