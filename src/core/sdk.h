//
// Created by AbdulMuaz Aqeel on 02/04/2026.
//

#ifndef EMU_LAUNCHER_SDK_H
#define EMU_LAUNCHER_SDK_H
#include <string>

#include "process.h"

namespace CoreDeck {
    struct SdkInfo {
        std::string SdkPath;
        std::string EmulatorPath;
        std::string AdbPath;
        std::string AvdManagerPath;
        std::string SdkManagerPath;
        std::string JavaHomePath;
        bool IsFound = false;
    };

    SdkInfo DetectAndroidSdk();

    ProcessEnvironment BuildAndroidToolEnvironment(const SdkInfo &sdk);
}

#endif // EMU_LAUNCHER_SDK_H
