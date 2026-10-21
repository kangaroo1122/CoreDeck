//
// Created by AbdulMuaz Aqeel on 09/04/2026.
//

#ifndef EMU_LAUNCHER_PATHS_H
#define EMU_LAUNCHER_PATHS_H

#include <string>
#include <filesystem>
#include <vector>

namespace CoreDeck::Paths {
    // Replace without deleting the destination first; both paths must be on the same filesystem.
    bool ReplaceFile(const std::filesystem::path &source, const std::filesystem::path &destination,
                     std::error_code &error);

    bool WriteTextFile(const std::filesystem::path &path, const std::string &text, std::error_code &error);

    enum class Platform : uint8_t {
        Windows,
        MacOS,
        Linux,
        Unknown
    };

    Platform GetCurrentPlatform();

    const char *GetPlatformName();

    std::string GetHomeDirectory();

    std::string GetConfigDirectory();

    std::string GetAndroidSdkDefaultPath();

    std::string GetAvdDirectory();

    std::string GetAppConfigPath(const std::string &subPath = "");

    std::string GetNullDevice();

    std::string GetExecutableExtension();

    std::string GetExecutableDirectory();

    std::string GetResourcesDirectory();
    std::string GetAssetsDirectory();

    std::string JoinPaths(const std::vector<std::string> &components);

    std::string NormalizePath(const std::string &path);

    std::string GetOptionsConfigPath(const std::string &avdName);

    std::string EnsureOptionsConfigDirectoryExists();

    namespace Onboarding {
        bool IsFirstRunComplete();

        void MarkFirstRunComplete();

        bool ValidateSdkPath(const std::string &path);

        std::string LoadSdkPathOverride();

        void SaveSdkPathOverride(const std::string &path);

        void ClearSdkPathOverride();
    }
}

#endif // EMU_LAUNCHER_PATHS_H
