#ifndef COREDECK_EMULATOR_CONSOLE_H
#define COREDECK_EMULATOR_CONSOLE_H

#include <string>
#include <vector>

namespace CoreDeck::EmulatorConsole {
    int FindFreePort(int startPort = 5554, int endPort = 5584, const std::vector<int> &reservedConsolePorts = {});

    bool IsAvailable(int port, int timeoutMs = 250);

    std::string QueryAvdName(int port, int timeoutMs = 500);

    int FindAvdConsolePort(const std::string &avdName, int startPort = 5554, int endPort = 5584);

    bool SendKill(int port, int timeoutMs = 2000);
}

#endif
