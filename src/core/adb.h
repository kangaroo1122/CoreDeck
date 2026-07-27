#ifndef COREDECK_ADB_H
#define COREDECK_ADB_H

#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "sdk.h"

namespace CoreDeck {
    struct AdbDevice {
        std::string Serial;
        std::string State;
        std::string Product;
        std::string Model;
        std::string Device;
        std::string TransportId;
        std::string AvdName;

        [[nodiscard]] bool IsOnline() const {
            return State == "device";
        }
    };

    std::vector<AdbDevice> ParseAdbDevices(const std::string &output);

    bool HasAdb(const SdkInfo &sdk);

    std::string ParseAdbEmuAvdName(const std::string &output);

    std::vector<AdbDevice> ListAdbDevices(
        const SdkInfo &sdk,
        const std::function<bool()> &shouldCancel = {}
    );

    std::optional<std::string> QueryAdbEmuAvdName(
        const SdkInfo &sdk,
        const std::string &serial,
        const std::function<bool()> &shouldCancel = {}
    );

    std::string EmulatorSerialForConsolePort(int consolePort);
}

#endif // COREDECK_ADB_H
