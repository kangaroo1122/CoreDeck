//
// Created by AbdulMuaz Aqeel on 18/04/2026.
//

#include "imgui.h"
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <GLFW/glfw3.h>
#include <future>

#include "update.h"
#include "../../core/utilities.h"
#include "../../core/update_download.h"
#include "../localization.h"
#include "../theme.h"
#include "../widgets.h"

namespace CoreDeck {
    namespace {
        bool OpenDownloadedPackage(const std::string &path) {
#if defined(__linux__)
            return OpenPath(std::filesystem::path(path).parent_path().string());
#else
            return OpenPath(path);
#endif
        }

        void PollUpdateDownload(Context &context) {
            auto &updates = context.Updates;
            if (!updates.DownloadFuture.valid() || updates.DownloadFuture.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
                return;
            }
            const UpdateDownloadResult result = updates.DownloadFuture.get();
            updates.DownloadInFlight = false;
            updates.DownloadProgress.reset();
            if (!result.Succeeded) {
                updates.DownloadError = result.Error;
                return;
            }
            updates.DownloadError.clear();
            updates.DownloadedPackagePath = result.PackagePath;
#if defined(_WIN32)
            updates.ShowInstallConfirmation = true;
#else
            if (!OpenDownloadedPackage(result.PackagePath)) {
                updates.DownloadError = "Could not open the downloaded update.";
            }
#endif
        }

        void StartUpdateDownload(Context &context) {
            if (context.Updates.DownloadInFlight || !context.Updates.LatestPackage || !context.Updates.LatestChecksum) {
                return;
            }
            context.Updates.DownloadError.clear();
            context.Updates.DownloadProgress = std::make_shared<UpdateDownloadProgress>();
            context.Updates.DownloadInFlight = true;
            const ReleaseAsset package = *context.Updates.LatestPackage;
            const ReleaseAsset checksum = *context.Updates.LatestChecksum;
            const auto progress = context.Updates.DownloadProgress;
            context.Updates.DownloadFuture = std::async(
                std::launch::async,
                [package, checksum, progress]() {
                    return DownloadAndVerifyUpdate(package, checksum, progress);
                }
            );
        }

        void RenderDownloadState(Context &context) {
            auto &updates = context.Updates;
            if (updates.DownloadInFlight && updates.DownloadProgress) {
                std::uint64_t downloaded = 0;
                std::uint64_t total = 0;
                std::string status;
                {
                    std::lock_guard lock(updates.DownloadProgress->Mutex);
                    downloaded = updates.DownloadProgress->DownloadedBytes;
                    total = updates.DownloadProgress->TotalBytes;
                    status = updates.DownloadProgress->Status;
                }
                ImGui::TextWrapped("%s", Tr(status.c_str()));
                const float fraction = total > 0 ? static_cast<float>(downloaded) / static_cast<float>(total) : 0.0F;
                ImGui::ProgressBar(fraction, ImVec2(-1, 0), nullptr);
                if (PrimaryButton("Cancel", true)) {
                    updates.DownloadProgress->CancelRequested.store(true);
                }
                return;
            }
            if (!updates.DownloadError.empty()) {
                ImGui::TextColored(HexColor(Colors::NEGATIVE), "%s", Tr(updates.DownloadError.c_str()));
            }
        }

        void RenderReleaseNotes(const std::string &notes) {
            if (notes.empty()) {
                return;
            }

            const float lineHeight = ImGui::GetTextLineHeightWithSpacing();
            ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, ImGui::GetStyle().FrameRounding);
            ImGui::BeginChild("##ReleaseNotes", ImVec2(0, lineHeight * 12.0F), 1, ImGuiWindowFlags_HorizontalScrollbar);

            size_t start = 0;
            while (start <= notes.size()) {
                const size_t end = notes.find('\n', start);
                std::string line = notes.substr(start, end == std::string::npos ? std::string::npos : end - start);
                if (!line.empty() && line.back() == '\r') {
                    line.pop_back();
                }

                std::string trimmed = line;
                size_t leading = 0;
                while (leading < trimmed.size() && (trimmed[leading] == ' ' || trimmed[leading] == '\t')) {
                    ++leading;
                }
                trimmed.erase(0, leading);

                if (trimmed.empty()) {
                    ImGui::Spacing();
                } else if (trimmed.starts_with("### ")) {
                    ImGui::TextColored(HexColor(Colors::POSITIVE), "%s", trimmed.substr(4).c_str());
                } else if (trimmed.starts_with("## ")) {
                    ImGui::TextColored(HexColor(Colors::POSITIVE), "%s", trimmed.substr(3).c_str());
                } else if (trimmed.starts_with("# ")) {
                    ImGui::TextColored(HexColor(Colors::POSITIVE), "%s", trimmed.substr(2).c_str());
                } else if (trimmed.starts_with("- ") || trimmed.starts_with("* ")) {
                    ImGui::Bullet();
                    ImGui::SameLine();
                    ImGui::TextWrapped(" %s", trimmed.substr(2).c_str());
                } else {
                    ImGui::TextWrapped("%s", trimmed.c_str());
                }

                if (end == std::string::npos) {
                    break;
                }
                start = end + 1;
            }

            ImGui::EndChild();
            ImGui::PopStyleVar();
        }

        void BuildUpToDateModal(Context &context) {
            if (!context.Updates.ShowUpToDateModal) {
                return;
            }

            if (!ImGui::IsPopupOpen("Up to date###CoreDeckUpdateOk")) {
                ImGui::OpenPopup("Up to date###CoreDeckUpdateOk");
            }

            const ImVec2 center = ImGui::GetMainViewport()->GetCenter();
            ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5F, 0.5F));
            ImGui::SetNextWindowSize(ImVec2(Em(32.0F), 0), ImGuiCond_Appearing);

            if (RoundedBeginPopupModal("Up to date###CoreDeckUpdateOk", &context.Updates.ShowUpToDateModal, WINDOW_NO_RESIZE_FLAGS)) {
                ImGui::TextWrapped("%s", Tr("You're running the latest CoreDeck release."));
                ImGui::Spacing();
                ImGui::Text("%s", Tr("Current: "));
                ImGui::SameLine(0, 0.0F);
                ImGui::TextColored(HexColor(Colors::POSITIVE), "v%s", COREDECK_VERSION);
                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Spacing();
                if (PrimaryButton("OK", true, ImVec2(ImGui::GetContentRegionAvail().x, 0))) {
                    context.Updates.ShowUpToDateModal = false;
                    ImGui::CloseCurrentPopup();
                }
                ImGui::EndPopup();
            }
        }
    }

    void BuildUpdateNoticeWindow(Context &context) {
        PollUpdateDownload(context);

        if (context.Updates.ShowInstallConfirmation) {
            const DialogResult result = SimpleDialog({
                .Id = "InstallUpdateConfirmDialog",
                .IsOpen = context.Updates.ShowInstallConfirmation,
                .Title = "Install Update Now?",
                .Message = "The installer will start and CoreDeck will close. Continue?",
                .ConfirmButtonTitle = "Install Now",
                .CancelButtonTitle = "Later",
                .BusyButtonTitle = "Installing...",
                .Type = DialogType::Default,
                .IsBusy = false,
            });
            if (result == DialogResult::Confirmed) {
                context.Updates.ShowInstallConfirmation = false;
                if (!OpenDownloadedPackage(context.Updates.DownloadedPackagePath)) {
                    context.Updates.DownloadError = "Could not start the update installer.";
                } else if (context.UI.MainWindow != nullptr) {
                    context.UI.QuitConfirmed = true;
                    glfwSetWindowShouldClose(context.UI.MainWindow, GLFW_TRUE);
                }
            } else if (result == DialogResult::Cancelled) {
                context.Updates.ShowInstallConfirmation = false;
            }
        }

        BuildUpToDateModal(context);

        if (!context.Updates.ShowNewVersionModal) {
            return;
        }

        if (!ImGui::IsPopupOpen("Update Available###CoreDeckUpdate")) {
            ImGui::OpenPopup("Update Available###CoreDeckUpdate");
        }

        const ImVec2 center = ImGui::GetMainViewport()->GetCenter();
        ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5F, 0.5F));
        ImGui::SetNextWindowSize(ImVec2(Em(80.0F), 0), ImGuiCond_Appearing);

        if (RoundedBeginPopupModal("Update Available###CoreDeckUpdate", &context.Updates.ShowNewVersionModal, WINDOW_NO_RESIZE_FLAGS)) {
            ImGui::Spacing();
            ImGui::TextUnformatted(Tr("You're currently running on"));
            ImGui::SameLine();
            ImGui::TextColored(HexColor(Colors::WARNING), "v%s", COREDECK_VERSION);
            ImGui::Spacing();

            RenderReleaseNotes(context.Updates.LatestNotes);

            RenderDownloadState(context);

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            const float spacing = ImGui::GetStyle().ItemSpacing.x;
            const float half = (ImGui::GetContentRegionAvail().x - spacing) * 0.5F;

            const bool canDownload = context.Updates.LatestPackage.has_value() && context.Updates.LatestChecksum.has_value();
            const char *downloadLabel = context.Updates.DownloadedPackagePath.empty() ? "Download" : "Open Download";
            if (PositiveButton(downloadLabel, !context.Updates.DownloadInFlight, ImVec2(half, 0))) {
                if (!canDownload) {
                    OpenUrl(COREDECK_GITHUB_RELEASES);
                } else if (!context.Updates.DownloadedPackagePath.empty()) {
                    if (!OpenDownloadedPackage(context.Updates.DownloadedPackagePath)) {
                        context.Updates.DownloadError = "Could not open the downloaded update.";
                    }
                } else {
                    StartUpdateDownload(context);
                }
            }
            ImGui::SameLine();
            if (PrimaryButton("Later", true, ImVec2(half, 0))) {
                context.Updates.ShowNewVersionModal = false;
                ImGui::CloseCurrentPopup();
            }

            ImGui::EndPopup();
        }
    }
}
