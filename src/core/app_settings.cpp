//
// Created by AbdulMuaz Aqeel on 18/04/2026.
//

#include <filesystem>
#include <fstream>
#include <rfl.hpp>
#include <rfl/json.hpp>

#include "app_settings.h"
#include "paths.h"
#include "log.h"

namespace CoreDeck {
    namespace {
        std::string GetAppSettingsFilePath() {
            return Paths::GetAppConfigPath("settings.json");
        }
    }

    AppSettings LoadAppSettings() {
        try {
            const std::string path = GetAppSettingsFilePath();
            if (path.empty() || !std::filesystem::exists(path)) {
                return AppSettings{};
            }

            std::ifstream file(path);
            if (!file.is_open()) {
                return AppSettings{};
            }

            const std::string json((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
            file.close();
            if (json.empty()) {
                return AppSettings{};
            }

            return rfl::json::read<AppSettings, rfl::DefaultIfMissing>(json).value();
        } catch (const std::exception &e) {
            Log::Error("Failed to load app settings: ", e.what());
            return AppSettings{};
        }
    }

    void SaveAppSettings(const AppSettings &settings) {
        try {
            const std::string path = GetAppSettingsFilePath();
            if (path.empty()) {
                return;
            }

            std::error_code ec;
            const auto json = rfl::json::write(settings);
            if (!Paths::WriteTextFile(path, json, ec)) {
                Log::Error("Failed to save app settings to: ", path, ": ", ec.message());
            }
        } catch (const std::exception &e) {
            Log::Error("Failed to save app settings: ", e.what());
        }
    }
}
