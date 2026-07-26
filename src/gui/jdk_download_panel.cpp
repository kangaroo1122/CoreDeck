//
// Created by kangaroo. on 26/07/2026.
//

#include "jdk_download_panel.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <exception>
#include <future>
#include <string>

#include "imgui.h"

#include "application.h"
#include "localization.h"
#include "theme.h"
#include "widgets.h"
#include "../core/jdk.h"
#include "../core/jdk_download.h"
#include "../core/utilities.h"

namespace CoreDeck {
    namespace {
        constexpr JdkVendor JDK_VENDORS[] = {
            JdkVendor::EclipseTemurin,
            JdkVendor::AzulZulu,
            JdkVendor::AmazonCorretto,
        };

        void ResetJdkPackageList(Context::JdkDownloadState &work) {
            work.Packages.clear();
            work.SelectedPackage = -1;
            work.Error.clear();
            work.List.Ready = false;
        }

        void StartJdkPackageList(Context::JdkDownloadState &work) {
            ResetJdkPackageList(work);
            work.List.Loading = true;
            work.List.Vendor = work.SelectedVendor;
            const JdkVendor vendor = work.SelectedVendor;
            work.List.Future = std::async(std::launch::async, [vendor] {
                return ListLatestLtsJdkPackages(vendor);
            });
        }

        void RequestJdkOperationCancel(const std::shared_ptr<SdkOperationProgress> &progress) {
            if (!progress) {
                return;
            }

            progress->CancelRequested.store(true);
            std::lock_guard lock(progress->Mutex);
            progress->StatusText = "Cancelling...";
            progress->DetailText.clear();
        }

        void ApplyInstalledJdk(
            Context &context,
            char *javaHomeBuffer,
            const std::size_t javaHomeBufferSize,
            JavaHomeStatus &versionState,
            const std::string &javaHomePath
        ) {
            context.Prefs.JavaHomePath = javaHomePath;
            context.Host.Sdk.JavaHomePath = context.Prefs.JavaHomePath;
            context.Host.Manager.SetSdk(context.Host.Sdk);
            strncpy(javaHomeBuffer, javaHomePath.c_str(), javaHomeBufferSize - 1);
            javaHomeBuffer[javaHomeBufferSize - 1] = '\0';
            versionState.Path.clear();
            RefreshJavaHomeStatus(versionState, javaHomePath);
            context.SdkManagerWork.List.Ready = false;
            PersistAppSettings(context);
        }

        void PollJdkDownloadWork(
            Context &context,
            char *javaHomeBuffer,
            const std::size_t javaHomeBufferSize,
            JavaHomeStatus &versionState
        ) {
            auto &work = context.JdkDownloadWork;

            if (work.List.Future.valid() &&
                work.List.Future.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
                work.List.Loading = false;
                work.List.Ready = true;
                try {
                    JdkPackageListResult result = work.List.Future.get();
                    if (work.List.Vendor == work.SelectedVendor) {
                        work.Packages = std::move(result.Packages);
                        work.Error = std::move(result.Error);
                        std::ranges::sort(work.Packages, [](const JdkPackage &a, const JdkPackage &b) {
                            return a.FeatureVersion > b.FeatureVersion;
                        });
                        work.SelectedPackage = work.Packages.empty() ? -1 : 0;
                    }
                } catch (const std::exception &ex) {
                    work.Packages.clear();
                    work.SelectedPackage = -1;
                    work.Error = StrConcat("Could not fetch JDK packages: ", ex.what());
                } catch (...) {
                    work.Packages.clear();
                    work.SelectedPackage = -1;
                    work.Error = "Could not fetch JDK packages.";
                }
            }

            if (work.InstallFuture.valid() &&
                work.InstallFuture.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
                work.Installing = false;
                try {
                    const JdkInstallResult result = work.InstallFuture.get();
                    if (result.Succeeded) {
                        work.Error.clear();
                        ApplyInstalledJdk(
                            context,
                            javaHomeBuffer,
                            javaHomeBufferSize,
                            versionState,
                            result.JavaHomePath
                        );
                    } else if (!result.Cancelled) {
                        work.Error = result.Error.empty() ? "JDK installation failed." : result.Error;
                    }
                } catch (const std::exception &ex) {
                    work.Error = StrConcat("JDK installation failed: ", ex.what());
                } catch (...) {
                    work.Error = "JDK installation failed.";
                }
            }
        }

        std::string JdkPackageVersionLabel(const JdkPackage &package) {
            if (package.JavaVersion.empty()) {
                return StrConcat("JDK ", std::to_string(package.FeatureVersion), " LTS");
            }
            return StrConcat("JDK ", package.JavaVersion);
        }

        void DrawJdkDownloadProgress(const Context::JdkDownloadState &work, const float width) {
            if (!work.Progress) {
                return;
            }

            bool finished = false;
            bool succeeded = false;
            float percent = 0.0F;
            std::string statusText;
            std::string detailText;
            {
                std::lock_guard lock(work.Progress->Mutex);
                finished = work.Progress->Finished;
                succeeded = work.Progress->Succeeded;
                percent = work.Progress->Percent;
                statusText = work.Progress->StatusText;
                detailText = work.Progress->DetailText;
            }

            ImGui::Spacing();
            ImGui::TextColored(
                finished ? (succeeded ? HexColor(Colors::POSITIVE) : HexColor(Colors::NEGATIVE)) : HexColor(Colors::TEXT_SUBTLE),
                "%s",
                Tr(statusText.c_str())
            );
            if (!detailText.empty()) {
                ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + width);
                ImGui::TextDisabled("%s", detailText.c_str());
                ImGui::PopTextWrapPos();
            }
            ImGui::PushStyleColor(ImGuiCol_PlotHistogram, HexColor(Colors::POSITIVE));
            ImGui::ProgressBar(percent, ImVec2(width, 0.0F));
            ImGui::PopStyleColor();
        }
    }

    bool IsJdkDownloadBusy(const Context &context) {
        const auto &work = context.JdkDownloadWork;
        return work.Installing.load();
    }

    bool ShouldOfferManagedJdkDownload(const JavaHomeStatus &versionState) {
        return !versionState.HasJava ||
               versionState.MajorVersion == 0 ||
               versionState.MajorVersion < 17;
    }

    void DrawJdkDownloadPanel(
        Context &context,
        char *javaHomeBuffer,
        const std::size_t javaHomeBufferSize,
        JavaHomeStatus &versionState,
        const float width,
        const bool enabled
    ) {
        auto &work = context.JdkDownloadWork;
        PollJdkDownloadWork(context, javaHomeBuffer, javaHomeBufferSize, versionState);

        if (!work.List.Ready && !work.List.Loading.load()) {
            StartJdkPackageList(work);
        }

        const bool installing = work.Installing.load();
        const bool listLoading = work.List.Loading.load();
        const bool busy = listLoading || installing;

        ImGui::PushID("JdkDownloadPanel");
        if (!enabled) {
            ImGui::BeginDisabled();
        }

        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Text, HexColor(Colors::TEXT_PRIMARY));
        ImGui::TextUnformatted(Tr("Download managed JDK"));
        ImGui::PopStyleColor();
        ImGui::PushStyleColor(ImGuiCol_Text, HexColor(Colors::TEXT_SUBTLE));
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + width);
        ImGui::TextWrapped("%s", Tr("CoreDeck can download a user-local JDK and use it without changing your system Java."));
        ImGui::PopTextWrapPos();
        ImGui::PopStyleColor();

        ImGui::SetNextItemWidth(width);
        {
            ComboStyle cs;
            if (ImGui::BeginCombo("##JdkVendor", JdkVendorDisplayName(work.SelectedVendor))) {
                for (const JdkVendor vendor: JDK_VENDORS) {
                    const bool selected = work.SelectedVendor == vendor;
                    if (RoundedSelectable(JdkVendorDisplayName(vendor), selected) && !busy) {
                        work.SelectedVendor = vendor;
                        ResetJdkPackageList(work);
                    }
                    if (selected) {
                        ImGui::SetItemDefaultFocus();
                    }
                }
                ImGui::EndCombo();
            }
        }

        if (listLoading) {
            ImGui::TextDisabled("%s", Tr("Fetching JDK packages from official sources..."));
        }
        if (!work.Error.empty()) {
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + width);
            ImGui::TextColored(HexColor(Colors::NEGATIVE), "%s", Tr(work.Error.c_str()));
            ImGui::PopTextWrapPos();
        }

        const std::string requestedJavaHomePath = NormalizeJavaHomePath(javaHomeBuffer);
        const bool usesRequestedInstallDirectory =
            !requestedJavaHomePath.empty() &&
            !LooksLikeJavaHome(requestedJavaHomePath);
        const bool canUseRequestedInstallDirectory =
            !usesRequestedInstallDirectory ||
            CanInstallJdkIntoRequestedDirectory(requestedJavaHomePath);
        if (usesRequestedInstallDirectory && !canUseRequestedInstallDirectory) {
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + width);
            ImGui::TextColored(
                HexColor(Colors::WARNING),
                "%s",
                Tr("Choose an empty folder for the JDK download.")
            );
            ImGui::PopTextWrapPos();
        }

        const float tableHeight = Eh(6.0F);
        PickerTableStyle pts;
        if (ImGui::BeginTable("##JdkPackages", 3, PICKER_TABLE_FLAGS, ImVec2(width, tableHeight))) {
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableSetupColumn(Tr("Version"), ImGuiTableColumnFlags_WidthStretch, 1.4F);
            ImGui::TableSetupColumn(Tr("Package"), ImGuiTableColumnFlags_WidthStretch, 1.7F);
            ImGui::TableSetupColumn(Tr("Size"), ImGuiTableColumnFlags_WidthStretch, 0.7F);
            ImGui::TableHeadersRow();

            for (int i = 0; i < static_cast<int>(work.Packages.size()); i++) {
                const JdkPackage &package = work.Packages[i];
                const bool selected = work.SelectedPackage == i;
                ImGui::TableNextRow();
                if (selected) {
                    ImGui::TableSetBgColor(
                        ImGuiTableBgTarget_RowBg0,
                        ImGui::GetColorU32(HexColor(Colors::POSITIVE_FILL, 0.16F))
                    );
                }

                ImGui::TableNextColumn();
                const std::string versionLabel = StrConcat(JdkPackageVersionLabel(package), "###JdkPackage", std::to_string(i));
                if (ImGui::Selectable(versionLabel.c_str(), selected, ImGuiSelectableFlags_SpanAllColumns) && !busy) {
                    work.SelectedPackage = i;
                }
                HoverTooltip(JdkPackageVersionLabel(package));

                ImGui::TableNextColumn();
                const std::string archiveName = package.ArchiveName.empty() ? "-" : package.ArchiveName;
                ImGui::TextDisabled("%s", archiveName.c_str());
                HoverTooltip(archiveName);

                ImGui::TableNextColumn();
                const std::string sizeText = package.SizeBytes > 0 ? FormatFileSize(package.SizeBytes) : "-";
                ImGui::Text("%s", sizeText.c_str());
                HoverTooltip(sizeText);
            }

            if (!listLoading && work.Packages.empty()) {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextDisabled("%s", Tr("No JDK packages are available for this system."));
            }

            ImGui::EndTable();
        }

        DrawJdkDownloadProgress(work, width);

        const float spacing = ImGui::GetStyle().ItemSpacing.x;
        const float halfWidth = (width - spacing) * 0.5F;
        const bool hasSelection = work.SelectedPackage >= 0 && work.SelectedPackage < static_cast<int>(work.Packages.size());
        if (installing) {
            const bool canCancel = work.Progress && !work.Progress->CancelRequested.load();
            if (NegativeButton("Cancel", canCancel, ImVec2(width, 0))) {
                RequestJdkOperationCancel(work.Progress);
            }
        } else {
            if (PositiveButton("Download & Use JDK", enabled && hasSelection && !busy && canUseRequestedInstallDirectory, ImVec2(halfWidth, 0))) {
                work.Progress = std::make_shared<SdkOperationProgress>();
                work.Installing = true;
                work.Error.clear();
                const JdkPackage package = work.Packages[work.SelectedPackage];
                const auto progress = work.Progress;
                work.InstallFuture = std::async(std::launch::async, [package, progress, requestedJavaHomePath] {
                    return InstallJdkPackage(package, progress, requestedJavaHomePath);
                });
            }
            ImGui::SameLine();
            if (PrimaryButton("Refresh", enabled && !busy, ImVec2(halfWidth, 0))) {
                StartJdkPackageList(work);
            }
        }

        if (!enabled) {
            ImGui::EndDisabled();
        }
        ImGui::PopID();
    }
}
