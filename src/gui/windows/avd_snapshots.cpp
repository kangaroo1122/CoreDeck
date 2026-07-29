//
// Created by Codex on 29/07/2026.
//

#include <chrono>
#include <ctime>
#include <exception>
#include <future>
#include <string>
#include <utility>

#include "imgui.h"

#include "avd_snapshots.h"
#include "../localization.h"
#include "../theme.h"
#include "../widgets.h"
#include "../../core/avd.h"
#include "../../core/utilities.h"

namespace CoreDeck {
    namespace {
        constexpr const char *TITLE = "AVD Snapshots###AvdSnapshotsDialog";

        std::string FormatSnapshotModifiedTime(const std::int64_t epochSeconds) {
            if (epochSeconds <= 0) {
                return "-";
            }

            const auto rawTime = static_cast<std::time_t>(epochSeconds);
            std::tm tm{};
#if defined(_WIN32)
            localtime_s(&tm, &rawTime);
#else
            localtime_r(&rawTime, &tm);
#endif

            char buffer[32] = {};
            if (std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M", &tm) == 0) {
                return "-";
            }
            return buffer;
        }

        AvdSnapshotListResult LoadSnapshotsForPath(const std::string &avdPath) {
            AvdSnapshotListResult result;
            try {
                result.Snapshots = ListAvdSnapshots(avdPath, &result.Error);
            } catch (const std::exception &e) {
                result.Snapshots.clear();
                result.Error = e.what();
            } catch (...) {
                result.Snapshots.clear();
                result.Error = "Could not list snapshots.";
            }
            return result;
        }

        AvdSnapshotDeleteResult DeleteSnapshotForPath(const std::string &avdPath, const std::string &snapshotName) {
            AvdSnapshotDeleteResult result;
            try {
                result.Succeeded = DeleteAvdSnapshot(avdPath, snapshotName, &result.Error);
            } catch (const std::exception &e) {
                result.Succeeded = false;
                result.Error = e.what();
            } catch (...) {
                result.Succeeded = false;
                result.Error = "Could not delete snapshot.";
            }
            return result;
        }

        void StartSnapshotList(Context &context) {
            auto &work = context.AvdSnapshotWork;
            if (work.Loading.load() || work.Deleting.load() || work.ListFuture.valid()) {
                return;
            }

            work.Ready = false;
            work.Snapshots.clear();
            work.SelectedSnapshot = -1;
            work.Error.clear();
            work.Status.clear();
            work.Loading = true;

            const std::string targetPath = work.TargetPath;
            work.ListFuture = std::async(std::launch::async, [targetPath] {
                return LoadSnapshotsForPath(targetPath);
            });
        }

        void StartSnapshotDelete(Context &context, const std::string &snapshotName) {
            auto &work = context.AvdSnapshotWork;
            if (work.Loading.load() || work.Deleting.load() || work.DeleteFuture.valid()) {
                return;
            }

            work.Error.clear();
            work.Status.clear();
            work.Deleting = true;

            const std::string targetPath = work.TargetPath;
            work.DeleteFuture = std::async(std::launch::async, [targetPath, snapshotName] {
                return DeleteSnapshotForPath(targetPath, snapshotName);
            });
        }

        void PollSnapshotWork(Context &context) {
            auto &work = context.AvdSnapshotWork;

            if (work.ListFuture.valid() &&
                work.ListFuture.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
                AvdSnapshotListResult result = work.ListFuture.get();
                work.Snapshots = std::move(result.Snapshots);
                work.Error = std::move(result.Error);
                work.Loading = false;
                work.Ready = true;
            }

            if (work.DeleteFuture.valid() &&
                work.DeleteFuture.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
                AvdSnapshotDeleteResult result = work.DeleteFuture.get();
                work.Deleting = false;
                if (result.Succeeded) {
                    work.PendingDeleteName.clear();
                    StartSnapshotList(context);
                    work.Status = "Snapshot deleted.";
                } else {
                    work.Error = result.Error.empty() ? "Could not delete snapshot." : std::move(result.Error);
                    work.PendingDeleteName.clear();
                }
            }
        }

        void DrawSnapshotTable(Context &context, const bool isRunning) {
            auto &work = context.AvdSnapshotWork;

            PickerTableStyle tableStyle;
            constexpr ImGuiTableFlags flags =
                PICKER_TABLE_FLAGS |
                ImGuiTableFlags_ScrollY |
                ImGuiTableFlags_SizingStretchProp;

            if (!ImGui::BeginTable("##AvdSnapshotTable", 4, flags, ImVec2(-1.0F, Eh(14.0F)))) {
                return;
            }

            ImGui::TableSetupColumn(Tr("Snapshot"), ImGuiTableColumnFlags_WidthStretch, 3.0F);
            ImGui::TableSetupColumn(Tr("Size"), ImGuiTableColumnFlags_WidthFixed, Em(10.0F));
            ImGui::TableSetupColumn(Tr("Modified"), ImGuiTableColumnFlags_WidthFixed, Em(15.0F));
            ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, Em(4.0F));
            ImGui::TableHeadersRow();

            for (int i = 0; i < static_cast<int>(work.Snapshots.size()); i++) {
                const AvdSnapshotInfo &snapshot = work.Snapshots[i];
                ImGui::PushID(i);
                ImGui::TableNextRow();

                ImGui::TableSetColumnIndex(0);
                ImGui::Text("%s", snapshot.Name.c_str());

                ImGui::TableSetColumnIndex(1);
                const std::string size = FormatFileSize(snapshot.SizeBytes);
                ImGui::Text("%s", size.c_str());

                ImGui::TableSetColumnIndex(2);
                const std::string modified = FormatSnapshotModifiedTime(snapshot.ModifiedEpochSeconds);
                ImGui::Text("%s", modified.c_str());

                ImGui::TableSetColumnIndex(3);
                const bool canDelete =
                    !isRunning &&
                    !work.Loading.load() &&
                    !work.Deleting.load() &&
                    work.PendingDeleteName.empty();
                if (!canDelete) {
                    ImGui::BeginDisabled();
                }
                if (NegativeButton(Icons::TRASH, canDelete, ImVec2(ImGui::GetFrameHeight(), 0))) {
                    work.PendingDeleteName = snapshot.Name;
                }
                if (!canDelete) {
                    ImGui::EndDisabled();
                }
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                    ImGui::SetTooltip(
                        "%s",
                        Tr(isRunning ? "Stop the AVD before deleting snapshots." : "Delete snapshot")
                    );
                }

                ImGui::PopID();
            }

            ImGui::EndTable();
        }

        void DrawDeleteConfirmation(Context &context) {
            auto &work = context.AvdSnapshotWork;
            if (work.PendingDeleteName.empty()) {
                return;
            }

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            ImGui::TextWrapped(Tr("Delete snapshot \"%s\"?"), work.PendingDeleteName.c_str());
            ImGui::PushStyleColor(ImGuiCol_Text, HexColor(Colors::TEXT_MUTED));
            ImGui::TextWrapped("%s", Tr("This will permanently remove the selected snapshot. This action cannot be undone."));
            ImGui::PopStyleColor();

            const float spacing = ImGui::GetStyle().ItemSpacing.x;
            const float halfWidth = (ImGui::GetContentRegionAvail().x - spacing) * 0.5F;
            if (NegativeButton(work.Deleting.load() ? "Deleting..." : "Delete", !work.Deleting.load(), ImVec2(halfWidth, 0))) {
                StartSnapshotDelete(context, work.PendingDeleteName);
            }
            ImGui::SameLine();
            if (PrimaryButton("Cancel", !work.Deleting.load(), ImVec2(halfWidth, 0))) {
                work.PendingDeleteName.clear();
            }
        }
    }

    void OpenAvdSnapshotsDialog(Context &context, const AvdInfo &avd) {
        auto &work = context.AvdSnapshotWork;
        if (work.Loading.load() || work.Deleting.load()) {
            return;
        }

        work.TargetName = avd.Name;
        work.TargetDisplayName = avd.DisplayName;
        work.TargetPath = avd.Path;
        work.Snapshots.clear();
        work.SelectedSnapshot = -1;
        work.PendingDeleteName.clear();
        work.Error.clear();
        work.Status.clear();
        work.Ready = false;
        context.UI.ShowAvdSnapshotsDialog = true;
        StartSnapshotList(context);
    }

    void BuildAvdSnapshotsWindow(Context &context) {
        PollSnapshotWork(context);

        if (!context.UI.ShowAvdSnapshotsDialog) {
            return;
        }
        if (context.UI.ShowAvdSnapshotsDialog && !ImGui::IsPopupOpen(TITLE)) {
            ImGui::OpenPopup(TITLE);
        }

        const ImVec2 center = ImGui::GetMainViewport()->GetCenter();
        ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5F, 0.5F));
        ImGui::SetNextWindowSize(ImVec2(Em(74.0F), 0), ImGuiCond_Appearing);

        if (RoundedBeginPopupModal(TITLE, &context.UI.ShowAvdSnapshotsDialog, WINDOW_AUTO_RESIZE_FLAGS)) {
            auto &work = context.AvdSnapshotWork;
            const bool isRunning = context.Host.Manager.IsRunning(work.TargetName);
            const bool isLoading = work.Loading.load();
            const bool isDeleting = work.Deleting.load();
            const bool isBusy = isLoading || isDeleting || !work.PendingDeleteName.empty();

            ImGui::Text("%s", work.TargetDisplayName.empty() ? work.TargetName.c_str() : work.TargetDisplayName.c_str());
            if (!work.TargetName.empty() && work.TargetDisplayName != work.TargetName) {
                ImGui::SameLine();
                ImGui::TextDisabled("(%s)", work.TargetName.c_str());
            }
            if (isRunning) {
                ImGui::SameLine();
                StatusBadge("Running", true);
            }

            ImGui::Spacing();

            if (!work.Error.empty()) {
                ImGui::TextColored(HexColor(Colors::NEGATIVE), "%s", Tr(work.Error.c_str()));
                ImGui::Spacing();
            } else if (!work.Status.empty()) {
                ImGui::TextColored(HexColor(Colors::POSITIVE), "%s", Tr(work.Status.c_str()));
                ImGui::Spacing();
            }

            if (work.Loading.load() && !work.Ready) {
                ImGui::TextDisabled("%s", Tr("Loading snapshots..."));
            } else if (work.Ready && work.Snapshots.empty() && work.Error.empty()) {
                ImGui::TextDisabled("%s", Tr("No snapshots found."));
            } else if (!work.Snapshots.empty()) {
                DrawSnapshotTable(context, isRunning);
            }

            DrawDeleteConfirmation(context);

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            const float spacing = ImGui::GetStyle().ItemSpacing.x;
            const float halfWidth = (ImGui::GetContentRegionAvail().x - spacing) * 0.5F;
            if (PositiveButton(isLoading ? "Refreshing..." : "Refresh", !isBusy, ImVec2(halfWidth, 0))) {
                StartSnapshotList(context);
            }
            ImGui::SameLine();
            if (PrimaryButton("Close", !isDeleting, ImVec2(halfWidth, 0))) {
                context.UI.ShowAvdSnapshotsDialog = false;
                ImGui::CloseCurrentPopup();
            }

            ImGui::EndPopup();
        }
    }
}
