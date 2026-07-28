//
// Created by AbdulMuaz Aqeel on 18/04/2026.
//

#ifndef COREDECK_APP_SETTINGS_TYPES_H
#define COREDECK_APP_SETTINGS_TYPES_H

#include <string>

namespace CoreDeck {
    struct AppSettings {
        int SchemaVersion = 1;
        bool AutoScroll = true;
        bool ConfirmBeforeDeleteAvd = true;
        bool ConfirmBeforeWipeAndRun = true;
        bool CrashReportingEnabled = true;
        int ThemeMode = 0;
        int Language = 0;
        std::string CustomCjkFontPath;
        float UiFontSize = 16.0F;
        int WindowWidth = 1200;
        int WindowHeight = 900;
        bool WindowMaximized = false;
        bool ShowAvdListPanel = true;
        bool ShowOptionsPanel = true;
        bool ShowDetailsPanel = true;
        bool ShowLogPanel = true;
        bool ShowDeviceExplorerPanel = false;
        float DockBottomGroupRatio = 1.0F / 3.0F;
        float DockTopOptionsRatio = 0.25F;
        float DockTopDetailsRatio = 0.2625F;
        float DockTopSideOnlyOptionsRatio = 0.50F;
        float DockBottomExplorerRatio = 1.0F / 3.0F;
        int AvdSortMode = 0;
        bool AvdSortAscending = true;
        std::string JavaHomePath;
    };
}

#endif // COREDECK_APP_SETTINGS_TYPES_H
