//
// Created by AbdulMuaz Aqeel on 15/04/2026.
//

#include "imgui.h"
#include <GLFW/glfw3.h>

#include "main_menu_bar.h"
#include "device_explorer.h"
#include "../../core/shared_folder.h"
#include "../widgets.h"
#include "../theme.h"
#include "../application.h"

namespace CoreDeck {
    void BuildMainMenuBar(Context &context) {
        MenuStyle ms;

        if (ImGui::BeginMainMenuBar()) {
            if (RoundedBeginMenu("File")) {
                if (RoundedMenuItem("Preferences...")) {
                    context.UI.ShowPreferences = true;
                }
                ImGui::Separator();
                if (RoundedMenuItem("Quit", nullptr, false, context.UI.MainWindow != nullptr)) {
                    glfwSetWindowShouldClose(context.UI.MainWindow, GLFW_TRUE);
                }
                ImGui::EndMenu();
            }

            if (RoundedBeginMenu("View")) {
                if (RoundedMenuItem(context.UI.ShowAvdListPanel ? "Hide AVD List" : "Show AVD List")) {
                    context.UI.ShowAvdListPanel = !context.UI.ShowAvdListPanel;
                    PersistAppSettings(context);
                }
                if (RoundedMenuItem(context.UI.ShowOptionsPanel ? "Hide Options" : "Show Options")) {
                    context.UI.ShowOptionsPanel = !context.UI.ShowOptionsPanel;
                    PersistAppSettings(context);
                }
                if (RoundedMenuItem(context.UI.ShowDetailsPanel ? "Hide Details" : "Show Details")) {
                    context.UI.ShowDetailsPanel = !context.UI.ShowDetailsPanel;
                    PersistAppSettings(context);
                }
                if (RoundedMenuItem(context.UI.ShowLogPanel ? "Hide Output Log" : "Show Output Log")) {
                    context.UI.ShowLogPanel = !context.UI.ShowLogPanel;
                    PersistAppSettings(context);
                }
                if (RoundedMenuItem(context.UI.ShowDeviceExplorerPanel ? "Hide Device Explorer" : "Show Device Explorer")) {
                    context.UI.ShowDeviceExplorerPanel = !context.UI.ShowDeviceExplorerPanel;
                    context.DeviceExplorer.Open = context.UI.ShowDeviceExplorerPanel;
                    if (context.UI.ShowDeviceExplorerPanel) {
                        context.DeviceExplorer.DockRequested = true;
                    }
                    PersistAppSettings(context);
                }
                ImGui::Separator();
                if (RoundedMenuItem("Storage Overview")) {
                    context.UI.ShowStorageDialog = true;
                }
                ImGui::EndMenu();
            }

            if (HasSelectedRunningAvd(context) && RoundedBeginMenu("Tools")) {
                if (RoundedMenuItem("Device Explorer")) {
                    OpenDeviceExplorer(context);
                }
                if (RoundedBeginMenu("Shared Folder")) {
                    if (RoundedMenuItem(GetOpenSharedFolderHostLabel())) {
                        OpenSharedFolderOnHost(context);
                    }
                    if (RoundedMenuItem("Open Shared Folder in Emulator")) {
                        OpenSharedFolderInEmulator(context);
                    }
                    ImGui::EndMenu();
                }
                ImGui::EndMenu();
            }

            if (RoundedBeginMenu("Help")) {
                if (RoundedMenuItem(IconWithLabel(Icons::INFO, "About CoreDeck").c_str())) {
                    context.UI.ShowAboutDialog = true;
                }
                if (RoundedMenuItem("Check for Updates...", nullptr, false, !context.Updates.UpdateCheckInFlight)) {
                    context.Updates.RequestManualUpdateCheck = true;
                }
                ImGui::EndMenu();
            }

            ImGui::EndMainMenuBar();
        }
    }
}
