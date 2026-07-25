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
        bool ShowAvdListPanel = true;
        bool ShowOptionsPanel = true;
        bool ShowDetailsPanel = true;
        bool ShowLogPanel = true;
        int AvdSortMode = 0;
        bool AvdSortAscending = true;
        std::string JavaHomePath;
    };
}

#endif // COREDECK_APP_SETTINGS_TYPES_H
