//
// Created by kangaroo. on 26/07/2026.
//

#ifndef COREDECK_MACOS_MENU_H
#define COREDECK_MACOS_MENU_H

#include <cstdint>
#include <optional>

namespace CoreDeck {
    enum class NativeMenuAction : uint8_t {
        Preferences,
        Quit,
        ToggleAvdList,
        ToggleOptions,
        ToggleDetails,
        ToggleOutputLog,
        ToggleDeviceExplorer,
        StorageOverview,
        DeviceExplorer,
        OpenSharedFolderHost,
        OpenSharedFolderEmulator,
        About,
        CheckForUpdates,
    };

    struct NativeMenuState {
        bool Interactive = true;
        bool ShowAvdListPanel = true;
        bool ShowOptionsPanel = true;
        bool ShowDetailsPanel = true;
        bool ShowLogPanel = true;
        bool ShowDeviceExplorerPanel = false;
        bool ShowToolsMenu = false;
        bool UpdateCheckInFlight = false;
    };

    namespace MacosMenu {
        void Install();

        void Shutdown();

        void Update(const NativeMenuState &state);

        std::optional<NativeMenuAction> PollAction();
    }
}

#endif // COREDECK_MACOS_MENU_H
