//
// Created by kangaroo. on 28/07/2026.
//

#include "quit_confirm.h"

#include <algorithm>
#include <GLFW/glfw3.h>

#include "../widgets.h"

namespace CoreDeck {
    void RequestQuitConfirmation(Context &context) {
        const bool hasRunningAvd = std::ranges::any_of(
            context.Catalog.Avds,
            [&context](const AvdInfo &avd) {
                return context.Host.Manager.IsRunning(avd.Name);
            }
        );
        if (!hasRunningAvd) {
            context.UI.QuitConfirmed = true;
            if (context.UI.MainWindow != nullptr) {
                glfwSetWindowShouldClose(context.UI.MainWindow, GLFW_TRUE);
            }
            return;
        }
        context.UI.ShowQuitDialog = true;
    }

    void BuildQuitConfirmWindow(Context &context) {
        if (!context.UI.ShowQuitDialog) {
            return;
        }

        const DialogResult result = SimpleDialog(
            {.Id = "QuitConfirmDialog",
             .IsOpen = context.UI.ShowQuitDialog,
             .Title = "Quit CoreDeck?",
             .Message = "CoreDeck will close. Running emulator sessions and background tasks will be stopped before exit.",
             .ConfirmButtonTitle = "Quit",
             .CancelButtonTitle = "Cancel",
             .BusyButtonTitle = "Quitting...",
             .Type = DialogType::Negative,
             .IsBusy = false}
        );

        if (result == DialogResult::Confirmed && context.UI.MainWindow != nullptr) {
            context.UI.ShowQuitDialog = false;
            context.UI.QuitConfirmed = true;
            glfwSetWindowShouldClose(context.UI.MainWindow, GLFW_TRUE);
        }
    }
}
