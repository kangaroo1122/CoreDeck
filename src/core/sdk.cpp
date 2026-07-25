//
// Created by AbdulMuaz Aqeel on 02/04/2026.
//

#include <filesystem>
#include <cstdlib>

#include "sdk.h"
#include "paths.h"

namespace CoreDeck {
    namespace {
        std::string FindCmdlineTool(const std::string &binDir, const std::string &name) {
#if defined(_WIN32)
            for (const auto *ext: {".bat", ".exe"}) {
                const std::string candidate = Paths::JoinPaths({binDir, name + ext});
                if (std::filesystem::exists(candidate)) return candidate;
            }
            return "";
#else
            const std::string candidate = Paths::JoinPaths({binDir, name});
            return std::filesystem::exists(candidate) ? candidate : "";
#endif
        }
    }

    SdkInfo DetectAndroidSdk() {
        SdkInfo sdk;

        const std::string savedPath = Paths::Onboarding::LoadSdkPathOverride();
        if (!savedPath.empty() && std::filesystem::exists(savedPath)) {
            sdk.SdkPath = savedPath;
        } else {
            const char *sdkEnv = std::getenv("ANDROID_HOME"); // NOLINT(concurrency-mt-unsafe)
            if (!sdkEnv) {
                sdkEnv = std::getenv("ANDROID_SDK_ROOT"); // NOLINT(concurrency-mt-unsafe)
            }

            if (sdkEnv) {
                sdk.SdkPath = sdkEnv;
            } else {
                const std::string defaultPath = Paths::GetAndroidSdkDefaultPath();
                if (std::filesystem::exists(defaultPath)) {
                    sdk.SdkPath = defaultPath;
                }
            }
        }

        if (sdk.SdkPath.empty()) {
            return sdk;
        }

        sdk.EmulatorPath = Paths::JoinPaths({sdk.SdkPath, "emulator", "emulator" + Paths::GetExecutableExtension()});
        sdk.AdbPath = Paths::JoinPaths({sdk.SdkPath, "platform-tools", "adb" + Paths::GetExecutableExtension()});
        if (!std::filesystem::exists(sdk.AdbPath)) {
            sdk.AdbPath.clear();
        }

        const std::string latestBin = Paths::JoinPaths({sdk.SdkPath, "cmdline-tools", "latest", "bin"});
        sdk.AvdManagerPath = FindCmdlineTool(latestBin, "avdmanager");

        if (sdk.AvdManagerPath.empty()) {
            const std::string cmdlineRoot = Paths::JoinPaths({sdk.SdkPath, "cmdline-tools"});
            if (std::filesystem::exists(cmdlineRoot) && std::filesystem::is_directory(cmdlineRoot)) {
                for (const auto &entry: std::filesystem::directory_iterator(cmdlineRoot)) {
                    if (!entry.is_directory()) {
                        continue;
                    }
                    const std::string candidate = FindCmdlineTool(
                        Paths::JoinPaths({entry.path().string(), "bin"}),
                        "avdmanager"
                    );
                    if (!candidate.empty()) {
                        sdk.AvdManagerPath = candidate;
                        break;
                    }
                }
            }
        }

        if (!sdk.AvdManagerPath.empty()) {
            const std::filesystem::path avdMgrPath(sdk.AvdManagerPath);
            sdk.SdkManagerPath = FindCmdlineTool(avdMgrPath.parent_path().string(), "sdkmanager");
        }

        if (std::filesystem::exists(sdk.EmulatorPath)) {
            sdk.IsFound = true;
        }
        return sdk;
    }

    ProcessEnvironment BuildAndroidToolEnvironment(const SdkInfo &sdk) {
        ProcessEnvironment environment;
        if (sdk.JavaHomePath.empty()) {
            return environment;
        }

        const std::string javaBin = Paths::JoinPaths({sdk.JavaHomePath, "bin"});
        std::string pathValue = javaBin;
        const char *currentPath = std::getenv("PATH"); // NOLINT(concurrency-mt-unsafe)
#if defined(_WIN32)
        if (currentPath == nullptr) {
            currentPath = std::getenv("Path"); // NOLINT(concurrency-mt-unsafe)
        }
#endif
        if (currentPath != nullptr) {
#if defined(_WIN32)
            pathValue += ";";
#else
            pathValue += ":";
#endif
            pathValue += currentPath;
        }

        environment.emplace_back("JAVA_HOME", sdk.JavaHomePath);
        environment.emplace_back("PATH", pathValue);
        return environment;
    }
}
