//
// Created by AbdulMuaz Aqeel on 15/04/2026.
//

#include <chrono>

#include "delete_avd.h"
#include "../application.h"
#include "../localization.h"
#include "../widgets.h"
#include "../../core/avd.h"

namespace CoreDeck {
    void StartDeleteAvdAsync(Context &context, const std::string &avdName) {
        if (context.Jobs.AvdDeletion.Busy.load()) {
            return;
        }

        context.Jobs.AvdDeletion.Error.clear();
        context.Jobs.AvdDeletion.TargetName = avdName;
        context.Jobs.AvdDeletion.Busy = true;
        const SdkInfo sdk = context.Host.Sdk;
        context.Jobs.AvdDeletion.Future = std::async(std::launch::async, [sdk, avdName]() {
            return DeleteAvd(sdk, avdName);
        });
    }

    void BuildDeleteAvdWindow(Context &context) {
        if (context.Jobs.AvdDeletion.Future.valid() &&
            context.Jobs.AvdDeletion.Future.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            bool deleted = false;
            try {
                deleted = context.Jobs.AvdDeletion.Future.get();
            } catch (...) {
                deleted = false;
            }
            context.Jobs.AvdDeletion.Busy = false;
            if (deleted) {
                context.Jobs.AvdDeletion.TargetName.clear();
                RefreshAvds(context);
                context.UI.ShowDeleteAvdDialog = false;
                return;
            }
            context.Jobs.AvdDeletion.Error = "Could not delete AVD.";
            for (int i = 0; i < static_cast<int>(context.Catalog.Avds.size()); i++) {
                if (context.Catalog.Avds[i].Name == context.Jobs.AvdDeletion.TargetName) {
                    context.Catalog.SelectedAvd = i;
                    break;
                }
            }
            context.UI.ShowDeleteAvdDialog = true;
        }

        if (context.Catalog.SelectedAvd < 0 || context.Catalog.SelectedAvd >= static_cast<int>(context.Catalog.Avds.size())) {
            return;
        }
        if (!context.UI.ShowDeleteAvdDialog) {
            return;
        }

        const auto &avd = context.Catalog.Avds[context.Catalog.SelectedAvd];
        const std::string title = StrConcat(Tr("Delete"), " \"", avd.DisplayName, "\"?");
        const bool isDeleting = context.Jobs.AvdDeletion.Busy.load();
        const DialogResult result = SimpleDialog(
            {.Id = "Delete###DeleteAvdDialog",
             .IsOpen = context.UI.ShowDeleteAvdDialog,
             .Title = title.c_str(),
             .Message = "This will permanently remove the AVD and all its data. This action cannot be undone.",
             .ConfirmButtonTitle = "Delete",
             .CancelButtonTitle = "Cancel",
             .BusyButtonTitle = "Deleting...",
             .ErrorMessage = context.Jobs.AvdDeletion.Error.empty()
                                 ? nullptr
                                 : Tr(context.Jobs.AvdDeletion.Error.c_str()),
             .Type = DialogType::Negative,
             .IsBusy = isDeleting}
        );

        if (result == DialogResult::Confirmed) {
            StartDeleteAvdAsync(context, avd.Name);
        }
    }
}
