#include "adb.h"

#include <filesystem>
#include <sstream>

#include "process.h"

namespace CoreDeck {
    namespace {
        std::string TrimCopy(const std::string &value) {
            const auto start = value.find_first_not_of(" \t\r\n");
            if (start == std::string::npos) {
                return "";
            }
            const auto end = value.find_last_not_of(" \t\r\n");
            return value.substr(start, end - start + 1);
        }

        bool StartsWith(const std::string &value, const std::string &prefix) {
            return value.size() >= prefix.size() && value.compare(0, prefix.size(), prefix) == 0;
        }
    }

    std::vector<AdbDevice> ParseAdbDevices(const std::string &output) {
        std::vector<AdbDevice> devices;
        std::istringstream lines(output);
        std::string line;
        while (std::getline(lines, line)) {
            line = TrimCopy(line);
            if (line.empty() ||
                line == "List of devices attached" ||
                StartsWith(line, "* ") ||
                StartsWith(line, "adb:") ||
                StartsWith(line, "error:")) {
                continue;
            }

            std::istringstream parts(line);
            AdbDevice device;
            parts >> device.Serial;
            parts >> device.State;
            if (device.Serial.empty() || device.State.empty()) {
                continue;
            }

            std::string token;
            while (parts >> token) {
                const auto colon = token.find(':');
                if (colon == std::string::npos) {
                    continue;
                }
                const std::string key = token.substr(0, colon);
                const std::string value = token.substr(colon + 1);
                if (key == "product") {
                    device.Product = value;
                } else if (key == "model") {
                    device.Model = value;
                } else if (key == "device") {
                    device.Device = value;
                } else if (key == "transport_id") {
                    device.TransportId = value;
                }
            }

            devices.push_back(std::move(device));
        }
        return devices;
    }

    bool HasAdb(const SdkInfo &sdk) {
        if (sdk.AdbPath.empty()) {
            return false;
        }
        std::error_code ec;
        if (!std::filesystem::is_regular_file(sdk.AdbPath, ec) || ec) {
            return false;
        }
#if defined(_WIN32)
        return true;
#else
        const auto perms = std::filesystem::status(sdk.AdbPath, ec).permissions();
        if (ec) {
            return false;
        }
        constexpr auto execBits =
            std::filesystem::perms::owner_exec |
            std::filesystem::perms::group_exec |
            std::filesystem::perms::others_exec;
        return (perms & execBits) != std::filesystem::perms::none;
#endif
    }

    std::string ParseAdbEmuAvdName(const std::string &output) {
        std::istringstream lines(output);
        std::string line;
        while (std::getline(lines, line)) {
            line = TrimCopy(line);
            if (line.empty() || line == "OK") {
                continue;
            }
            if (StartsWith(line, "KO") || StartsWith(line, "error:")) {
                return "";
            }
            return line;
        }
        return "";
    }

    std::vector<AdbDevice> ListAdbDevices(const SdkInfo &sdk, const std::function<bool()> &shouldCancel) {
        if (!HasAdb(sdk)) {
            return {};
        }

        std::string output;
        const bool ok = StreamCommandArgsWithEnvCancelable(
            sdk.AdbPath,
            {"devices", "-l"},
            "",
            BuildAndroidToolEnvironment(sdk),
            [&output](const std::string &line) {
                output += line;
                output.push_back('\n');
            },
            shouldCancel
        );
        if (!ok) {
            return {};
        }

        auto devices = ParseAdbDevices(output);

        for (auto &device: devices) {
            if (shouldCancel && shouldCancel()) {
                break;
            }
            if (!device.IsOnline() || !StartsWith(device.Serial, "emulator-")) {
                continue;
            }
            if (const auto avdName = QueryAdbEmuAvdName(sdk, device.Serial, shouldCancel)) {
                device.AvdName = *avdName;
            }
        }
        return devices;
    }

    std::optional<std::string> QueryAdbEmuAvdName(
        const SdkInfo &sdk,
        const std::string &serial,
        const std::function<bool()> &shouldCancel
    ) {
        if (!HasAdb(sdk) || serial.empty()) {
            return std::nullopt;
        }

        std::string output;
        const bool ok = StreamCommandArgsWithEnvCancelable(
            sdk.AdbPath,
            {"-s", serial, "emu", "avd", "name"},
            "",
            BuildAndroidToolEnvironment(sdk),
            [&output](const std::string &line) {
                output += line;
                output.push_back('\n');
            },
            shouldCancel
        );
        if (!ok) {
            return std::nullopt;
        }
        const std::string avdName = ParseAdbEmuAvdName(output);
        if (avdName.empty()) {
            return std::nullopt;
        }
        return avdName;
    }

    std::string EmulatorSerialForConsolePort(const int consolePort) {
        if (consolePort <= 0) {
            return "";
        }
        return "emulator-" + std::to_string(consolePort);
    }
}
