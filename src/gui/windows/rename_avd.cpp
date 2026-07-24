//
// Created by AbdulMuaz Aqeel on 24/07/2026.
//

#include <cstring>

#include "imgui.h"

#include "rename_avd.h"
#include "../application.h"
#include "../theme.h"
#include "../widgets.h"
#include "../../core/avd.h"

namespace CoreDeck {
    namespace {
        constexpr const char *TITLE = "Rename Display Name###RenameAvdDialog";

        void SelectAvdByName(Context &context, const std::string &avdName) {
            for (int i = 0; i < static_cast<int>(context.Catalog.Avds.size()); i++) {
                if (context.Catalog.Avds[i].Name == avdName) {
                    context.Catalog.SelectedAvd = i;
                    context.Catalog.PreviousSelectedAvd = -1;
                    return;
                }
            }
        }
    }

    void OpenRenameAvdDialog(Context &context, const AvdInfo &avd) {
        context.AvdRenameWork.TargetName = avd.Name;
        context.AvdRenameWork.TargetPath = avd.Path;
        context.AvdRenameWork.Error.clear();

        std::strncpy(
            context.AvdRenameWork.DisplayNameBuffer,
            avd.DisplayName.c_str(),
            sizeof(context.AvdRenameWork.DisplayNameBuffer) - 1
        );
        context.AvdRenameWork.DisplayNameBuffer[sizeof(context.AvdRenameWork.DisplayNameBuffer) - 1] = '\0';
        context.UI.ShowRenameAvdDialog = true;
    }

    void BuildRenameAvdWindow(Context &context) {
        if (context.UI.ShowRenameAvdDialog && !ImGui::IsPopupOpen(TITLE)) {
            ImGui::OpenPopup(TITLE);
        }

        const ImVec2 center = ImGui::GetMainViewport()->GetCenter();
        ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5F, 0.5F));
        ImGui::SetNextWindowSize(ImVec2(Em(54.0F), 0), ImGuiCond_Appearing);

        if (RoundedBeginPopupModal(TITLE, &context.UI.ShowRenameAvdDialog, WINDOW_AUTO_RESIZE_FLAGS)) {
            ImGui::Text("Display Name");
            ImGui::SetNextItemWidth(-1.0F);
            ImGui::InputTextWithHint(
                "##AvdDisplayName",
                "Leave empty to use the internal name",
                context.AvdRenameWork.DisplayNameBuffer,
                IM_ARRAYSIZE(context.AvdRenameWork.DisplayNameBuffer)
            );

            ImGui::PushStyleColor(ImGuiCol_Text, HexColor(Colors::TEXT_SUBTLE));
            ImGui::TextWrapped("Internal name remains unchanged: %s", context.AvdRenameWork.TargetName.c_str());
            ImGui::PopStyleColor();

            if (!context.AvdRenameWork.Error.empty()) {
                ImGui::TextColored(HexColor(Colors::NEGATIVE), "%s", context.AvdRenameWork.Error.c_str());
            }

            ImGui::Spacing();
            ImGui::Spacing();

            const float spacing = ImGui::GetStyle().ItemSpacing.x;
            const float halfWidth = (ImGui::GetContentRegionAvail().x - spacing) * 0.5F;

            if (PositiveButton("Save", true, ImVec2(halfWidth, 0))) {
                const std::string targetName = context.AvdRenameWork.TargetName;
                const std::string targetPath = context.AvdRenameWork.TargetPath;
                const std::string displayName = context.AvdRenameWork.DisplayNameBuffer;

                if (SetAvdDisplayName(targetPath, displayName)) {
                    RefreshAvds(context);
                    SelectAvdByName(context, targetName);
                    context.UI.ShowRenameAvdDialog = false;
                    ImGui::CloseCurrentPopup();
                } else {
                    context.AvdRenameWork.Error = "Failed to update the AVD config file.";
                }
            }
            ImGui::SameLine();
            if (PrimaryButton("Cancel", true, ImVec2(halfWidth, 0))) {
                context.UI.ShowRenameAvdDialog = false;
                ImGui::CloseCurrentPopup();
            }

            ImGui::EndPopup();
        }
    }
}
