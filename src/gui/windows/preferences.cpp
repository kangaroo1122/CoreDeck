//
// Created by AbdulMuaz Aqeel on 18/04/2026.
//

#include <algorithm>
#include <chrono>
#include <cstring>
#include <future>
#include <vector>

#include "imgui.h"
#include "imgui_internal.h"

#include "preferences.h"
#include "../widgets.h"
#include "../fonts.h"
#include "../localization.h"
#include "../theme.h"
#include "../application.h"
#include "../../core/jdk.h"
#include "../../core/paths.h"
#include "../../core/sdk_bootstrap.h"
#include "../../core/sdk_packages.h"
#include "../../core/sdk.h"
#include "../../core/file_dialog.h"
#include "../../core/system_image.h"
#include "../../core/utilities.h"

namespace CoreDeck {
    namespace {
        enum class PrefsSection : uint8_t {
            Appearance,
            General,
            AndroidSdk,
        };

        struct SidebarItem {
            PrefsSection Section;
            const char *Icon;
            const char *Label;
        };

        constexpr SidebarItem SIDEBAR_ITEMS[] = {
            {.Section = PrefsSection::Appearance, .Icon = Icons::CIRCLE, .Label = "Appearance"},
            {.Section = PrefsSection::General, .Icon = Icons::GEAR, .Label = "General"},
            {.Section = PrefsSection::AndroidSdk, .Icon = Icons::MOBILE, .Label = "Android JDK/SDK"},
        };

        bool SidebarRow(const SidebarItem &item, const bool selected) {
            ImGuiWindow *window = ImGui::GetCurrentWindow();
            const float width = ImGui::GetContentRegionAvail().x;
            const float height = ImGui::GetFrameHeight() + 6.0F;

            const ImVec2 pos = ImGui::GetCursorScreenPos();
            const ImRect bb(pos, ImVec2(pos.x + width, pos.y + height));

            ImGui::PushID(item.Label);
            const ImGuiID id = window->GetID(item.Label);
            ImGui::ItemSize(ImVec2(width, height));
            if (!ImGui::ItemAdd(bb, id)) {
                ImGui::PopID();
                return false;
            }

            bool hovered = false;
            bool held = false;
            const bool pressed = ImGui::ButtonBehavior(bb, id, &hovered, &held);

            ImU32 bg = 0;
            if (selected) {
                bg = ImGui::GetColorU32(HexColor(Colors::SURFACE3));
            } else if (hovered) {
                bg = ImGui::GetColorU32(HexColor(Colors::SURFACE2));
            }
            if (bg) {
                window->DrawList->AddRectFilled(bb.Min, bb.Max, bg);
            }

            if (selected) {
                const ImVec2 a(bb.Min.x, bb.Min.y);
                const ImVec2 b(bb.Min.x + 4.0F, bb.Max.y);
                window->DrawList->AddRectFilled(a, b, ImGui::GetColorU32(HexColor(Colors::ACCENT_INFO)));
            }

            const ImU32 textColor = ImGui::GetColorU32(selected ? HexColor(Colors::TEXT_PRIMARY) : HexColor(Colors::TEXT_SUBTLE));
            const char *translatedLabel = Tr(item.Label);
            const float textY = bb.Min.y + ((height - ImGui::GetTextLineHeight()) * 0.5F);
            window->DrawList->AddText(ImVec2(bb.Min.x + 14.0F, textY), textColor, item.Icon);
            window->DrawList->AddText(ImVec2(bb.Min.x + 38.0F, textY), textColor, translatedLabel);

            ImGui::PopID();
            return pressed;
        }

        void SectionHeader(const char *title, const char *subtitle) {
            ImGui::PushStyleColor(ImGuiCol_Text, HexColor(Colors::TEXT_PRIMARY));
            ImGui::TextUnformatted(Tr(title));
            ImGui::PopStyleColor();
            if (subtitle && *subtitle) {
                ImGui::PushStyleColor(ImGuiCol_Text, HexColor(Colors::TEXT_SUBTLE));
                ImGui::TextWrapped("%s", Tr(subtitle));
                ImGui::PopStyleColor();
            }
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();
        }

        bool CheckboxRow(const char *id, const char *title, const char *tooltip, bool *value) {
            ImGui::PushID(id);
            const bool changed = ImGui::Checkbox(Tr(title), value);
            if (tooltip && *tooltip) {
                ImGui::SameLine();
                ImGui::TextColored(HexColor(Colors::TEXT_MUTED), Icons::INFO);
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("%s", Tr(tooltip));
                }
            }
            ImGui::PopID();
            return changed;
        }

        void LabelText(const char *label) {
            ImGui::PushStyleColor(ImGuiCol_Text, HexColor(Colors::TEXT_PRIMARY));
            ImGui::TextUnformatted(Tr(label));
            ImGui::PopStyleColor();
        }

        void SubsectionHeader(const char *title, const char *subtitle = nullptr) {
            LabelText(title);
            if (subtitle && *subtitle) {
                ImGui::PushStyleColor(ImGuiCol_Text, HexColor(Colors::TEXT_SUBTLE));
                ImGui::TextWrapped("%s", Tr(subtitle));
                ImGui::PopStyleColor();
            }
            ImGui::Spacing();
        }

        std::string DefaultSdkPath() {
            return Paths::GetAndroidSdkDefaultPath();
        }

        bool CanUseSdkManager(const Context &context) {
            return !context.Host.Sdk.SdkManagerPath.empty();
        }

        bool IsSdkManagerBusy(const auto &work) {
            return work.List.Loading.load() ||
                   work.OperationBusy.load() ||
                   work.LicenseBusy.load() ||
                   work.BootstrapBusy.load() ||
                   work.AwaitingLicenseConsent;
        }

        void RefreshSdkPackageList(Context &context) {
            auto &work = context.SdkManagerWork;
            if (!CanUseSdkManager(context) || work.List.Loading.load()) {
                return;
            }

            work.Error.clear();
            work.List.Ready = false;
            work.List.Loading = true;
            const SdkInfo sdk = context.Host.Sdk;
            const bool includeObsolete = !work.HideObsoletePackages;
            work.List.Future = std::async(std::launch::async, [sdk, includeObsolete] {
                return ListSdkPackages(sdk, includeObsolete);
            });
        }

        void ApplySdkRoot(Context &context, const std::string &sdkRoot, char *sdkPathBuffer, const size_t sdkBufferSize) {
            Paths::Onboarding::SaveSdkPathOverride(sdkRoot);
            context.Host.Sdk = DetectAndroidSdk();
            context.Host.Sdk.JavaHomePath = context.Prefs.JavaHomePath;
            context.Host.Manager.SetSdk(context.Host.Sdk);
            RefreshAvds(context);
            context.UI.HideInvalidSdkPathBanner = false;
            strncpy(sdkPathBuffer, context.Host.Sdk.SdkPath.c_str(), sdkBufferSize - 1);
            sdkPathBuffer[sdkBufferSize - 1] = '\0';
            PersistAppSettings(context);
        }

        SdkPackageViewTab ToSdkPackageViewTab(const SdkManagerTab tab) {
            return tab == SdkManagerTab::Platforms ? SdkPackageViewTab::Platforms : SdkPackageViewTab::Tools;
        }

        bool MatchesSdkPackageSearch(const SdkPackageDisplayRow &row, const char *filter) {
            if (filter == nullptr || filter[0] == '\0') {
                return true;
            }
            return ContainsIgnoreCase(
                StrConcat(
                    row.Id,
                    " ",
                    row.Name,
                    " ",
                    row.ApiLevel,
                    " ",
                    row.Revision,
                    " ",
                    row.Version,
                    " ",
                    row.PackagePath,
                    " ",
                    row.Location
                ),
                filter
            );
        }

        ImVec4 SdkPackageStatusColor(const SdkPackageDisplayStatus status) {
            switch (status) {
                case SdkPackageDisplayStatus::Installed:
                    return HexColor(Colors::POSITIVE);
                case SdkPackageDisplayStatus::UpdateAvailable:
                    return HexColor(Colors::WARNING);
                case SdkPackageDisplayStatus::NotInstalled:
                    return HexColor(Colors::TEXT_MUTED);
            }
            return HexColor(Colors::TEXT_MUTED);
        }

        bool IsProtectedSdkPackageRow(const SdkPackageDisplayRow &row) {
            return std::ranges::any_of(row.RemovePackagePaths, [](const std::string &path) {
                return path == "cmdline-tools;latest";
            });
        }

        void ClearPendingSdkPackageInstall(auto &work) {
            work.PendingPackagePaths.clear();
            work.PendingSdk = {};
        }

        void StartPendingSdkPackageInstall(auto &work) {
            work.Progress = std::make_shared<SdkOperationProgress>();
            work.OperationBusy = true;
            const SdkInfo sdk = work.PendingSdk;
            const std::vector<std::string> packagePaths = work.PendingPackagePaths;
            const auto progress = work.Progress;
            work.OperationFuture = std::async(std::launch::async, [sdk, packagePaths, progress] {
                return InstallSdkPackages(sdk, packagePaths, progress);
            });
            ClearPendingSdkPackageInstall(work);
        }

        void PollSdkManagerWork(Context &context, char *sdkPathBuffer, const size_t sdkBufferSize) {
            auto &work = context.SdkManagerWork;

            if (work.List.Future.valid() &&
                work.List.Future.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
                work.LastListResult = work.List.Future.get();
                work.Packages = work.LastListResult.Packages;
                work.Error = work.LastListResult.Error;
                work.List.Loading = false;
                work.List.Ready = true;
                work.SelectedPackageRowId.clear();
            }

            if (work.OperationFuture.valid() &&
                work.OperationFuture.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
                const bool ok = work.OperationFuture.get();
                work.OperationBusy = false;
                ClearPendingSdkPackageInstall(work);
                if (ok) {
                    context.Host.Sdk = DetectAndroidSdk();
                    context.Host.Sdk.JavaHomePath = context.Prefs.JavaHomePath;
                    context.Host.Manager.SetSdk(context.Host.Sdk);
                    RefreshAvds(context);
                    RefreshSdkPackageList(context);
                }
            }

            if (work.LicenseCheckFuture.valid() &&
                work.LicenseCheckFuture.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
                const LicenseStatus status = work.LicenseCheckFuture.get();
                work.LicenseBusy = false;
                if (status == LicenseStatus::AllAccepted) {
                    if (work.PendingPackagePaths.empty() || work.PendingSdk.SdkManagerPath.empty()) {
                        work.Error = "SDK Manager was not found.";
                        ClearPendingSdkPackageInstall(work);
                        return;
                    }
                    StartPendingSdkPackageInstall(work);
                } else if (status == LicenseStatus::SomeUnaccepted) {
                    work.AwaitingLicenseConsent = true;
                } else {
                    work.Error = "Could not query license state. Check that the SDK Manager is working.";
                    ClearPendingSdkPackageInstall(work);
                }
            }

            if (work.LicenseAcceptFuture.valid() &&
                work.LicenseAcceptFuture.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
                const bool ok = work.LicenseAcceptFuture.get();
                work.LicenseBusy = false;
                work.AwaitingLicenseConsent = false;
                if (ok && !work.PendingPackagePaths.empty() && !work.PendingSdk.SdkManagerPath.empty()) {
                    StartPendingSdkPackageInstall(work);
                } else {
                    work.Error = work.PendingSdk.SdkManagerPath.empty()
                                     ? "SDK Manager was not found."
                                     : "License acceptance failed. Try again or accept via Android Studio.";
                    ClearPendingSdkPackageInstall(work);
                }
            }

            if (work.BootstrapFuture.valid() &&
                work.BootstrapFuture.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
                const SdkBootstrapResult result = work.BootstrapFuture.get();
                work.BootstrapBusy = false;
                if (result.Succeeded) {
                    work.Error.clear();
                    ApplySdkRoot(context, work.BootstrapSdkRoot, sdkPathBuffer, sdkBufferSize);
                    RefreshSdkPackageList(context);
                } else if (result.Cancelled) {
                    work.Error.clear();
                } else {
                    work.Error = result.Error.empty()
                                     ? "Command-line tools installation failed."
                                     : result.Error;
                }
            }
        }

        void RequestSdkOperationCancel(const std::shared_ptr<SdkOperationProgress> &progress) {
            if (!progress) {
                return;
            }

            progress->CancelRequested.store(true);
            std::lock_guard lock(progress->Mutex);
            progress->StatusText = "Cancelling...";
            progress->DetailText.clear();
        }

        void RequestFontReload(Context &context) {
            SetCurrentLanguage(context.Prefs.Language);
            context.UI.FontReloadRequested = true;
            PersistAppSettings(context);
        }

        bool CjkFontPathInCandidates(const std::vector<std::string> &candidates, const std::string &path) {
            return std::ranges::find(candidates, path) != candidates.end();
        }

        std::string CjkFontPreviewLabel(
            const std::string &customPath,
            const bool customPathValid,
            const std::vector<std::string> &candidates
        ) {
            if (customPath.empty()) {
                if (!candidates.empty()) {
                    return StrConcat(Tr("Automatic"), " - ", FontPathDisplayName(candidates.front()));
                }
                return Tr("Automatic");
            }
            if (customPathValid) {
                return FontPathDisplayName(customPath);
            }
            return Tr("Selected font is no longer available.");
        }

        void DrawAppearanceSection(Context &context) {
            SectionHeader("Appearance", "Visual preferences for CoreDeck.");

            LabelText("Theme");

            if (CategoryChip("Dark###ThemeDark", context.Prefs.Theme == ThemeMode::Dark)) {
                context.Prefs.Theme = ThemeMode::Dark;
                ApplyCustomImGuiTheme(context.Prefs.Theme, GetDpiScale());
                PersistAppSettings(context);
            }
            ImGui::SameLine();
            if (CategoryChip("Light###ThemeLight", context.Prefs.Theme == ThemeMode::Light)) {
                context.Prefs.Theme = ThemeMode::Light;
                ApplyCustomImGuiTheme(context.Prefs.Theme, GetDpiScale());
                PersistAppSettings(context);
            }

            ImGui::Dummy(ImVec2(0, 4));

            LabelText("Language");
            ImGui::SetNextItemWidth(Em(30.0F));
            {
                static constexpr AppLanguage LANGUAGE_OPTIONS[] = {
                    AppLanguage::English,
                    AppLanguage::SimplifiedChinese,
                };
                ComboStyle cs;
                if (ImGui::BeginCombo("##AppLanguage", LanguageDisplayName(context.Prefs.Language))) {
                    for (const AppLanguage language: LANGUAGE_OPTIONS) {
                        const bool selected = context.Prefs.Language == language;
                        if (RoundedSelectable(LanguageDisplayName(language), selected)) {
                            context.Prefs.Language = language;
                            RequestFontReload(context);
                        }
                        if (selected) {
                            ImGui::SetItemDefaultFocus();
                        }
                    }
                    ImGui::EndCombo();
                }
            }

            ImGui::Dummy(ImVec2(0, 4));

            LabelText("CJK fallback font");
            ImGui::PushStyleColor(ImGuiCol_Text, HexColor(Colors::TEXT_SUBTLE));
            ImGui::TextWrapped("%s", Tr("Used for Chinese glyphs while keeping the built-in UI font."));
            ImGui::PopStyleColor();

            static const char *fontError = nullptr;
            const std::vector<std::string> cjkCandidates = FindSystemCjkFontPaths();
            const bool customPathValid = IsSupportedFontPath(context.Prefs.CustomCjkFontPath);
            const std::string preview = CjkFontPreviewLabel(
                context.Prefs.CustomCjkFontPath,
                customPathValid,
                cjkCandidates
            );

            const float chooseWidth = Em(15.0F);
            const float spacing = ImGui::GetStyle().ItemSpacing.x;
            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - chooseWidth - spacing);
            {
                ComboStyle cs;
                if (ImGui::BeginCombo("##CjkFont", preview.c_str())) {
                    if (RoundedSelectable("Automatic (recommended)###CjkFontAutomatic", context.Prefs.CustomCjkFontPath.empty())) {
                        context.Prefs.CustomCjkFontPath.clear();
                        fontError = nullptr;
                        RequestFontReload(context);
                    }

                    if (!cjkCandidates.empty()) {
                        ImGui::Separator();
                        ImGui::TextDisabled("%s", Tr("System candidates"));
                        for (int i = 0; i < static_cast<int>(cjkCandidates.size()); i++) {
                            const auto &candidate = cjkCandidates[i];
                            const bool selected =
                                !context.Prefs.CustomCjkFontPath.empty() &&
                                context.Prefs.CustomCjkFontPath == candidate;
                            const std::string label = StrConcat(
                                FontPathDisplayName(candidate),
                                "###CjkFontCandidate",
                                std::to_string(i)
                            );
                            if (RoundedSelectable(label.c_str(), selected)) {
                                context.Prefs.CustomCjkFontPath = candidate;
                                fontError = nullptr;
                                RequestFontReload(context);
                            }
                            if (selected) {
                                ImGui::SetItemDefaultFocus();
                            }
                        }
                    }

                    if (customPathValid && !CjkFontPathInCandidates(cjkCandidates, context.Prefs.CustomCjkFontPath)) {
                        ImGui::Separator();
                        ImGui::TextDisabled("%s", Tr("Manual font"));
                        const std::string manualLabel = StrConcat(
                            FontPathDisplayName(context.Prefs.CustomCjkFontPath),
                            "###CjkFontManual"
                        );
                        if (RoundedSelectable(manualLabel.c_str(), true)) {
                            fontError = nullptr;
                        }
                        ImGui::SetItemDefaultFocus();
                    }
                    ImGui::EndCombo();
                }
            }

            ImGui::SameLine();
            static constexpr const char *FONT_FILTERS[] = {"*.ttf", "*.otf", "*.ttc"};
            if (PrimaryButton("Choose Font...", true, ImVec2(chooseWidth, 0))) {
                const auto picked = FileDialog::PickFile(
                    Tr("Select a CJK font file"),
                    FONT_FILTERS,
                    IM_ARRAYSIZE(FONT_FILTERS),
                    Tr("Font files"),
                    context.Prefs.CustomCjkFontPath
                );
                if (picked.has_value()) {
                    if (IsSupportedFontPath(*picked)) {
                        context.Prefs.CustomCjkFontPath = *picked;
                        fontError = nullptr;
                        RequestFontReload(context);
                    } else {
                        fontError = "The selected file is not a supported font.";
                    }
                }
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s", Tr("Choose a .ttf, .otf, or .ttc font file."));
            }

            const std::string effectivePath =
                customPathValid
                    ? context.Prefs.CustomCjkFontPath
                    : (!cjkCandidates.empty() ? cjkCandidates.front() : std::string{});

            if (fontError != nullptr) {
                ImGui::TextColored(HexColor(Colors::NEGATIVE), "%s", Tr(fontError));
            } else if (!context.Prefs.CustomCjkFontPath.empty() && !customPathValid) {
                ImGui::TextColored(HexColor(Colors::NEGATIVE), "%s", Tr("Selected font is no longer available."));
            } else if (effectivePath.empty()) {
                ImGui::TextColored(HexColor(Colors::WARNING), "%s", Tr("No system CJK font detected."));
            } else {
                const char *pathLabel = context.Prefs.CustomCjkFontPath.empty() ? "Auto-detected font" : "Selected font";
                ImGui::TextDisabled("%s", Tr(pathLabel));
                ImGui::SameLine();
                ImGui::TextWrapped("%s", effectivePath.c_str());
            }

            if (!context.Prefs.CustomCjkFontPath.empty()) {
                if (PrimaryButton("Use Automatic", true)) {
                    context.Prefs.CustomCjkFontPath.clear();
                    fontError = nullptr;
                    RequestFontReload(context);
                }
            }
        }

        void DrawGeneralSection(Context &context) {
            SectionHeader("General", "Behavior of CoreDeck while you work with AVDs.");

            if (SubtitledCheckbox(
                    "AutoScrollLogs",
                    &context.Logs.AutoScroll,
                    "Enable auto-scrolling of output logs",
                    "Keep the log view pinned to the most recent line as new output arrives."
                )) {
                PersistAppSettings(context);
            }

            ImGui::Dummy(ImVec2(0, 4));

            if (SubtitledCheckbox(
                    "ConfirmDeleteAvd",
                    &context.Prefs.ConfirmBeforeDeleteAvd,
                    "Confirm before deleting an AVD",
                    "Show a confirmation dialog when you delete a virtual device."
                )) {
                PersistAppSettings(context);
            }

            ImGui::Dummy(ImVec2(0, 4));

            if (SubtitledCheckbox(
                    "ConfirmWipeAndRun",
                    &context.Prefs.ConfirmBeforeWipeAndRun,
                    "Confirm before wiping and running an AVD",
                    "Show a confirmation dialog before launching an AVD with wiped user data."
                )) {
                PersistAppSettings(context);
            }

            ImGui::Dummy(ImVec2(0, 4));

            if (SubtitledCheckbox(
                    "CrashReporting",
                    &context.Prefs.CrashReportingEnabled,
                    "Send crash reports and diagnostics to " COREDECK_TITLE,
                    "Share anonymous crash reports and error diagnostics (Restart Required)."
                )) {
                PersistAppSettings(context);
            }
        }

        void DrawSdkPackageManager(Context &context) {
            auto &work = context.SdkManagerWork;

            ImGui::Spacing();
            LabelText("SDK Packages");

            if (!CanUseSdkManager(context)) {
                ImGui::TextColored(
                    HexColor(Colors::WARNING),
                    "%s",
                    Tr("Install Android SDK command-line tools before managing SDK packages.")
                );
                return;
            }

            if (!work.List.Ready && !work.List.Loading.load()) {
                RefreshSdkPackageList(context);
            }

            if (CategoryChip("SDK Platforms###SdkPlatformsTab", work.ActiveTab == SdkManagerTab::Platforms)) {
                work.ActiveTab = SdkManagerTab::Platforms;
                work.SelectedPackageRowId.clear();
            }
            ImGui::SameLine();
            if (CategoryChip("SDK Tools###SdkToolsTab", work.ActiveTab == SdkManagerTab::Tools)) {
                work.ActiveTab = SdkManagerTab::Tools;
                work.SelectedPackageRowId.clear();
            }

            ImGui::Spacing();
            const float refreshWidth = Em(12.0F);
            const float spacing = ImGui::GetStyle().ItemSpacing.x;
            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - refreshWidth - spacing);
            const std::string searchHint = IconWithLabel(Icons::SEARCH, "Search SDK packages...");
            if (ImGui::InputTextWithHint("##SdkPackageSearch", searchHint.c_str(), work.SearchFilter, sizeof(work.SearchFilter))) {
                work.SelectedPackageRowId.clear();
            }
            ImGui::SameLine();
            if (PrimaryButton("Refresh", !IsSdkManagerBusy(work), ImVec2(refreshWidth, 0))) {
                RefreshSdkPackageList(context);
            }

            if (CheckboxRow(
                    "HideObsoleteSdkPackages",
                    "Hide obsolete packages",
                    "",
                    &work.HideObsoletePackages
                )) {
                RefreshSdkPackageList(context);
            }
            ImGui::SameLine();
            if (CheckboxRow(
                    "ShowSdkPackageDetails",
                    "Show package details",
                    "",
                    &work.ShowPackageDetails
                )) {
                work.SelectedPackageRowId.clear();
            }
            ImGui::Spacing();

            if (!work.Error.empty()) {
                ImGui::TextColored(HexColor(Colors::NEGATIVE), "%s", Tr(work.Error.c_str()));
            }

            if (work.List.Loading.load()) {
                ImGui::TextDisabled("%s", Tr("Fetching SDK packages from official sources..."));
            }

            const std::vector<SdkPackageDisplayRow> displayRows = BuildSdkPackageDisplayRows(
                work.Packages,
                ToSdkPackageViewTab(work.ActiveTab),
                work.ShowPackageDetails ? SdkPackageViewMode::Details : SdkPackageViewMode::Summary
            );

            const bool busy = IsSdkManagerBusy(work);
            if (busy) {
                ImGui::BeginDisabled();
            }

            PickerTableStyle pts;
            ImGui::BeginChild("##SdkPackageTableFrame", ImVec2(-1.0F, Eh(11.0F)), 1, ImGuiWindowFlags_NoScrollbar);
            if (ImGui::BeginTable("##SdkPackageTable", 4, PICKER_TABLE_FLAGS, ImVec2(-1.0F, -1.0F))) {
                const bool platformTab = work.ActiveTab == SdkManagerTab::Platforms;
                ImGui::TableSetupScrollFreeze(0, 1);
                if (platformTab) {
                    ImGui::TableSetupColumn(Tr("Name"), ImGuiTableColumnFlags_WidthStretch, 2.8F);
                    ImGui::TableSetupColumn(Tr("API Level"), ImGuiTableColumnFlags_WidthStretch, 0.9F);
                    ImGui::TableSetupColumn(Tr("Revision"), ImGuiTableColumnFlags_WidthStretch, 0.9F);
                    ImGui::TableSetupColumn(Tr("Status"), ImGuiTableColumnFlags_WidthStretch, 1.1F);
                } else {
                    ImGui::TableSetupColumn(Tr("Name"), ImGuiTableColumnFlags_WidthStretch, 2.8F);
                    ImGui::TableSetupColumn(Tr("Version"), ImGuiTableColumnFlags_WidthStretch, 1.0F);
                    ImGui::TableSetupColumn(Tr("Status"), ImGuiTableColumnFlags_WidthStretch, 1.1F);
                    ImGui::TableSetupColumn(Tr("Package"), ImGuiTableColumnFlags_WidthStretch, 2.2F);
                }
                ImGui::TableHeadersRow();

                int visibleCount = 0;
                for (const SdkPackageDisplayRow &row: displayRows) {
                    if (!MatchesSdkPackageSearch(row, work.SearchFilter)) {
                        continue;
                    }

                    visibleCount++;
                    const bool selected = work.SelectedPackageRowId == row.Id;
                    ImGui::TableNextRow();
                    if (selected) {
                        ImGui::TableSetBgColor(
                            ImGuiTableBgTarget_RowBg0,
                            ImGui::GetColorU32(HexColor(Colors::POSITIVE_FILL, 0.16F))
                        );
                    }

                    ImGui::TableNextColumn();
                    const std::string label = StrConcat(row.Name, "###SdkPackage", row.Id);
                    if (ImGui::Selectable(label.c_str(), selected, ImGuiSelectableFlags_SpanAllColumns)) {
                        work.SelectedPackageRowId = row.Id;
                    }
                    if (selected) {
                        ImGui::SetItemDefaultFocus();
                    }

                    ImGui::TableNextColumn();
                    if (platformTab) {
                        ImGui::Text("%s", row.ApiLevel.empty() ? "-" : row.ApiLevel.c_str());
                    } else {
                        ImGui::Text("%s", row.Version.empty() ? "-" : row.Version.c_str());
                    }

                    ImGui::TableNextColumn();
                    if (platformTab) {
                        ImGui::Text("%s", row.Revision.empty() ? "-" : row.Revision.c_str());
                    } else {
                        const char *status = SdkPackageDisplayStatusText(row.Status);
                        ImGui::TextColored(SdkPackageStatusColor(row.Status), "%s", Tr(status));
                    }

                    ImGui::TableNextColumn();
                    if (platformTab) {
                        const char *status = SdkPackageDisplayStatusText(row.Status);
                        ImGui::TextColored(SdkPackageStatusColor(row.Status), "%s", Tr(status));
                    } else {
                        ImGui::TextDisabled("%s", row.PackagePath.empty() ? "-" : row.PackagePath.c_str());
                    }
                }

                if (!work.List.Loading.load() && visibleCount == 0) {
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::TextDisabled("%s", Tr("No SDK packages match the current filters."));
                }

                ImGui::EndTable();
            }
            ImGui::EndChild();

            if (busy) {
                ImGui::EndDisabled();
            }

            if (work.Progress && !work.BootstrapBusy.load()) {
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
                    ImGui::TextDisabled("%s", detailText.c_str());
                }
                ImGui::PushStyleColor(ImGuiCol_PlotHistogram, HexColor(Colors::POSITIVE));
                ImGui::ProgressBar(percent, ImVec2(-1.0F, 0.0F));
                ImGui::PopStyleColor();
            }

            const auto selectedIt = std::ranges::find_if(displayRows, [&](const SdkPackageDisplayRow &row) {
                return row.Id == work.SelectedPackageRowId && MatchesSdkPackageSearch(row, work.SearchFilter);
            });
            const SdkPackageDisplayRow *selected =
                selectedIt == displayRows.end() ? nullptr : &*selectedIt;
            const bool canInstallOrUpdate =
                selected != nullptr && !selected->InstallPackagePaths.empty() && !busy;
            const bool canUpdate =
                selected != nullptr && selected->Status == SdkPackageDisplayStatus::UpdateAvailable;
            const bool canRemove =
                selected != nullptr &&
                !selected->RemovePackagePaths.empty() &&
                !IsProtectedSdkPackageRow(*selected) &&
                !busy;

            ImGui::Spacing();
            const float actionSpacing = ImGui::GetStyle().ItemSpacing.x;
            const float thirdWidth = (ImGui::GetContentRegionAvail().x - (actionSpacing * 2.0F)) / 3.0F;

            if (PositiveButton(canUpdate ? "Update" : "Install", canInstallOrUpdate, ImVec2(thirdWidth, 0))) {
                work.PendingPackagePaths = selected->InstallPackagePaths;
                work.PendingSdk = context.Host.Sdk;
                work.Error.clear();
                work.LicenseBusy = true;
                const SdkInfo sdk = work.PendingSdk;
                work.LicenseCheckFuture = std::async(std::launch::async, [sdk] {
                    return CheckSdkLicenses(sdk);
                });
            }
            ImGui::SameLine();
            if (NegativeButton("Remove", canRemove, ImVec2(thirdWidth, 0))) {
                work.Progress = std::make_shared<SdkOperationProgress>();
                work.OperationBusy = true;
                const SdkInfo sdk = context.Host.Sdk;
                const std::vector<std::string> packagePaths = selected->RemovePackagePaths;
                const auto progress = work.Progress;
                work.OperationFuture = std::async(std::launch::async, [sdk, packagePaths, progress] {
                    return UninstallSdkPackages(sdk, packagePaths, progress);
                });
            }
            ImGui::SameLine();
            const bool operationBusy = work.OperationBusy.load();
            const bool canCancelOperation = operationBusy && work.Progress && !work.Progress->CancelRequested.load();
            if (operationBusy) {
                if (NegativeButton("Cancel", canCancelOperation, ImVec2(thirdWidth, 0))) {
                    RequestSdkOperationCancel(work.Progress);
                }
            } else if (PrimaryButton("Clear Status", work.Progress != nullptr && !busy, ImVec2(thirdWidth, 0))) {
                work.Progress.reset();
            }

            if (work.AwaitingLicenseConsent) {
                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Spacing();
                ImGui::Text("%s", Tr("Accept Android SDK License Terms"));
                ImGui::TextWrapped(
                    "%s",
                    Tr("Some Android SDK package licenses have not been accepted yet. To install this package, you must agree to Google's Android SDK license terms.")
                );
                if (PrimaryButton("Open license terms in browser")) {
                    OpenUrl("https://developer.android.com/studio/terms");
                }

                const float halfWidth = (ImGui::GetContentRegionAvail().x - actionSpacing) * 0.5F;
                if (PositiveButton("Agree & Install", !work.LicenseBusy.load(), ImVec2(halfWidth, 0))) {
                    work.LicenseBusy = true;
                    const SdkInfo sdk = work.PendingSdk;
                    work.LicenseAcceptFuture = std::async(std::launch::async, [sdk] {
                        return AcceptSdkLicenses(sdk);
                    });
                }
                ImGui::SameLine();
                if (NegativeButton("Cancel", !work.LicenseBusy.load(), ImVec2(halfWidth, 0))) {
                    work.AwaitingLicenseConsent = false;
                    ClearPendingSdkPackageInstall(work);
                }
            }
        }

        void DrawAndroidSdkSection(
            Context &context,
            char *sdkPathBuffer,
            const size_t sdkBufferSize
        ) {
            PollSdkManagerWork(context, sdkPathBuffer, sdkBufferSize);

            SubsectionHeader("SDK root", "Where CoreDeck installs and manages Android SDK command-line tools and packages.");
            const float browseWidth = Em(12.0F);
            const float spacing = ImGui::GetStyle().ItemSpacing.x;
            if (sdkPathBuffer[0] == '\0') {
                const std::string defaultPath = DefaultSdkPath();
                strncpy(sdkPathBuffer, defaultPath.c_str(), sdkBufferSize - 1);
                sdkPathBuffer[sdkBufferSize - 1] = '\0';
            }
            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - browseWidth - spacing);
            ImGui::InputTextWithHint("##SdkPrefs", Tr("Path to Android SDK"), sdkPathBuffer, sdkBufferSize);
            ImGui::SameLine();
            if (PrimaryButton("Browse...###PrefsSdkBrowse", true, ImVec2(browseWidth, 0))) {
                if (const auto picked = FileDialog::PickFolder(Tr("Select Android SDK directory"), sdkPathBuffer)) {
                    strncpy(sdkPathBuffer, picked->c_str(), sdkBufferSize - 1);
                    sdkPathBuffer[sdkBufferSize - 1] = '\0';
                }
            }

            const std::string pathStr = sdkPathBuffer;
            const bool pathOk = Paths::Onboarding::ValidateSdkPath(pathStr);
            const bool hasSdkManager = HasSdkManager(pathStr);
            const bool sdkManagerBusy = IsSdkManagerBusy(context.SdkManagerWork);
            const bool bootstrapBusy = context.SdkManagerWork.BootstrapBusy.load();

            if (!pathStr.empty()) {
                if (pathOk) {
                    ImGui::TextColored(HexColor(Colors::POSITIVE), "%s", Tr("Valid Android SDK path."));
                } else if (hasSdkManager) {
                    ImGui::TextColored(HexColor(Colors::WARNING), "%s", Tr("Command-line tools are installed. Install emulator and platform-tools below."));
                } else {
                    ImGui::TextColored(
                        HexColor(Colors::WARNING),
                        "%s",
                        Tr("Command-line tools are missing. Install them to manage SDK packages.")
                    );
                }
            } else {
                ImGui::PushStyleColor(ImGuiCol_Text, HexColor(Colors::TEXT_SUBTLE));
                ImGui::TextUnformatted(Tr("CoreDeck will use the platform default Android SDK directory."));
                ImGui::PopStyleColor();
            }
            if (!hasSdkManager && !context.SdkManagerWork.Error.empty()) {
                ImGui::TextColored(HexColor(Colors::NEGATIVE), "%s", Tr(context.SdkManagerWork.Error.c_str()));
            }

            ImGui::Spacing();
            ImGui::Spacing();

            const bool canApplySdkPath = (pathOk || hasSdkManager) && !sdkManagerBusy;
            if (PrimaryButton("Apply SDK Path", canApplySdkPath)) {
                ApplySdkRoot(context, pathStr, sdkPathBuffer, sdkBufferSize);
                RefreshSdkPackageList(context);
            }
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled) && !canApplySdkPath) {
                ImGui::SetTooltip("%s", Tr("Install command-line tools and required SDK packages before applying as a working SDK."));
            }

            ImGui::SameLine();
            if (PrimaryButton("Use Default Discovery", !sdkManagerBusy)) {
                Paths::Onboarding::ClearSdkPathOverride();
                context.Host.Sdk = DetectAndroidSdk();
                context.Host.Sdk.JavaHomePath = context.Prefs.JavaHomePath;
                context.Host.Manager.SetSdk(context.Host.Sdk);
                RefreshAvds(context);
                context.UI.HideInvalidSdkPathBanner = false;
                const std::string &p = context.Host.Sdk.SdkPath;
                strncpy(sdkPathBuffer, p.c_str(), sdkBufferSize - 1);
                sdkPathBuffer[sdkBufferSize - 1] = '\0';
                PersistAppSettings(context);
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s", Tr("Forget the saved override and detect the SDK from ANDROID_HOME / default paths."));
            }

            if (!hasSdkManager) {
                ImGui::SameLine();
                if (PositiveButton("Install Command-line Tools", !sdkManagerBusy && !pathStr.empty())) {
                    context.SdkManagerWork.Error.clear();
                    context.SdkManagerWork.Progress = std::make_shared<SdkOperationProgress>();
                    context.SdkManagerWork.BootstrapSdkRoot = pathStr;
                    context.SdkManagerWork.BootstrapBusy = true;
                    const auto progress = context.SdkManagerWork.Progress;
                    context.SdkManagerWork.BootstrapFuture = std::async(std::launch::async, [pathStr, progress] {
                        return BootstrapCommandLineTools(pathStr, progress);
                    });
                }
            }

            if (!hasSdkManager && context.SdkManagerWork.Progress) {
                ImGui::Spacing();
                float percent = 0.0F;
                std::string statusText;
                std::string detailText;
                const bool cancelRequested = context.SdkManagerWork.Progress->CancelRequested.load();
                {
                    std::lock_guard lock(context.SdkManagerWork.Progress->Mutex);
                    percent = context.SdkManagerWork.Progress->Percent;
                    statusText = context.SdkManagerWork.Progress->StatusText;
                    detailText = context.SdkManagerWork.Progress->DetailText;
                }
                ImGui::Text("%s", Tr(statusText.c_str()));
                if (!detailText.empty()) {
                    ImGui::TextDisabled("%s", detailText.c_str());
                }
                ImGui::PushStyleColor(ImGuiCol_PlotHistogram, HexColor(Colors::POSITIVE));
                ImGui::ProgressBar(percent, ImVec2(-1.0F, 0.0F));
                ImGui::PopStyleColor();

                if (bootstrapBusy && NegativeButton("Cancel", !cancelRequested)) {
                    RequestSdkOperationCancel(context.SdkManagerWork.Progress);
                }
            }

            DrawSdkPackageManager(context);
        }

        void DrawJdkSection(
            Context &context,
            char *javaHomeBuffer,
            const size_t javaHomeBufferSize,
            JavaHomeStatus &versionState
        ) {
            SubsectionHeader("JDK", "Java used by Android SDK command-line tools launched from CoreDeck.");
            const float browseWidth = Em(12.0F);
            const float spacing = ImGui::GetStyle().ItemSpacing.x;
            LabelText("JDK home");
            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - browseWidth - spacing);
            ImGui::InputTextWithHint("##JavaHomePrefs", Tr("Path to JDK home"), javaHomeBuffer, javaHomeBufferSize);
            ImGui::SameLine();
            if (PrimaryButton("Browse...###PrefsJdkBrowse", true, ImVec2(browseWidth, 0))) {
                if (const auto picked = FileDialog::PickFolder(Tr("Select JDK home directory"), javaHomeBuffer)) {
                    const std::string normalized = NormalizeJavaHomePath(*picked);
                    strncpy(javaHomeBuffer, normalized.c_str(), javaHomeBufferSize - 1);
                    javaHomeBuffer[javaHomeBufferSize - 1] = '\0';
                }
            }

            const std::string javaHomePath = NormalizeJavaHomePath(javaHomeBuffer);
            if (javaHomePath.empty()) {
                ImGui::PushStyleColor(ImGuiCol_Text, HexColor(Colors::TEXT_SUBTLE));
                ImGui::TextUnformatted(Tr("Leave empty to use the system default Java environment."));
                ImGui::PopStyleColor();
            } else if (LooksLikeJavaHome(javaHomePath)) {
                ImGui::TextColored(HexColor(Colors::POSITIVE), "%s", Tr("Java executable found under this JDK home."));
            } else {
                ImGui::TextColored(
                    HexColor(Colors::NEGATIVE),
                    "%s",
                    Tr("No java executable found under bin.")
                );
            }

            RefreshJavaHomeStatus(versionState, javaHomePath);
            if (javaHomePath.empty()) {
                ImGui::TextColored(HexColor(Colors::TEXT_MUTED), "%s", Tr(versionState.Text.c_str()));
            } else {
                ImGui::TextColored(
                    versionState.HasJava ? HexColor(Colors::TEXT_SUBTLE) : HexColor(Colors::TEXT_MUTED),
                    Tr("Version: %s"),
                    versionState.Text.c_str()
                );
            }

            ImGui::Spacing();
            ImGui::Spacing();

            const bool sdkManagerBusy = IsSdkManagerBusy(context.SdkManagerWork);
            const bool canApplyJavaHome = (javaHomePath.empty() || versionState.HasJava) && !sdkManagerBusy;
            if (PrimaryButton("Apply JDK Path", canApplyJavaHome)) {
                context.Prefs.JavaHomePath = javaHomePath;
                context.Host.Sdk.JavaHomePath = context.Prefs.JavaHomePath;
                context.Host.Manager.SetSdk(context.Host.Sdk);
                strncpy(javaHomeBuffer, javaHomePath.c_str(), javaHomeBufferSize - 1);
                javaHomeBuffer[javaHomeBufferSize - 1] = '\0';
                PersistAppSettings(context);
                RefreshSdkPackageList(context);
            }
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                ImGui::SetTooltip(
                    "%s",
                    canApplyJavaHome
                        ? Tr("Use this JDK only for Android SDK command-line tools launched by CoreDeck.")
                        : Tr("Select a valid JDK home or clear the path to use system Java.")
                );
            }

            ImGui::SameLine();
            if (PrimaryButton("Use System Java", !sdkManagerBusy)) {
                context.Prefs.JavaHomePath.clear();
                context.Host.Sdk.JavaHomePath.clear();
                context.Host.Manager.SetSdk(context.Host.Sdk);
                javaHomeBuffer[0] = '\0';
                PersistAppSettings(context);
                RefreshSdkPackageList(context);
            }
        }
    }

    void BuildPreferencesWindow(Context &context) {
        if (context.UI.ShowPreferences && !ImGui::IsPopupOpen("Preferences###CoreDeckPrefs")) {
            ImGui::OpenPopup("Preferences###CoreDeckPrefs");
        }

        const ImVec2 center = ImGui::GetMainViewport()->GetCenter();
        ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5F, 0.5F));
        ImGui::SetNextWindowSize(EmV(100.0F, 24.0F), ImGuiCond_Appearing);

        static char sdkPathBuffer[2048];
        static char javaHomeBuffer[2048];
        static JavaHomeStatus javaVersionState;
        static auto activeSection = PrefsSection::Appearance;

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        if (RoundedBeginPopupModal("Preferences###CoreDeckPrefs", &context.UI.ShowPreferences, WINDOW_NO_RESIZE_FLAGS)) {
            ImGui::PopStyleVar();

            if (ImGui::IsWindowAppearing()) {
                const std::string &p = context.Host.Sdk.SdkPath;
                strncpy(sdkPathBuffer, p.c_str(), sizeof(sdkPathBuffer) - 1);
                sdkPathBuffer[sizeof(sdkPathBuffer) - 1] = '\0';
                const std::string &javaHome = context.Prefs.JavaHomePath;
                strncpy(javaHomeBuffer, javaHome.c_str(), sizeof(javaHomeBuffer) - 1);
                javaHomeBuffer[sizeof(javaHomeBuffer) - 1] = '\0';
                javaVersionState.Path.clear();
                if (!context.Host.Sdk.IsFound) {
                    activeSection = PrefsSection::AndroidSdk;
                }
            }

            const float sidebarWidth = Em(22.0F);

            ImGui::PushStyleColor(ImGuiCol_ChildBg, HexColor(Colors::SURFACE0));
            ImGui::BeginChild("##PrefsSidebar", ImVec2(sidebarWidth, 0), 0);
            ImGui::PopStyleColor();
            ImGui::Dummy(ImVec2(0, 12));
            ImGui::SetWindowFontScale(1.4F);
            const char *brand = "CoreDeck";
            const float brandW = ImGui::CalcTextSize(brand).x;
            ImGui::SetCursorPosX((sidebarWidth - brandW) * 0.5F);
            ImGui::PushStyleColor(ImGuiCol_Text, HexColor(Colors::TEXT_PRIMARY));
            ImGui::TextUnformatted(brand);
            ImGui::PopStyleColor();
            ImGui::SetWindowFontScale(1.0F);
            const char *version = "v" COREDECK_VERSION;
            const float versionW = ImGui::CalcTextSize(version).x;
            ImGui::SetCursorPosX((sidebarWidth - versionW) * 0.5F);
            ImGui::PushStyleColor(ImGuiCol_Text, HexColor(Colors::TEXT_MUTED));
            ImGui::TextUnformatted(version);
            ImGui::PopStyleColor();

            ImGui::Dummy(ImVec2(0, 12));
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 2));
            for (const auto &item: SIDEBAR_ITEMS) {
                if (SidebarRow(item, item.Section == activeSection)) {
                    activeSection = item.Section;
                }
            }
            ImGui::PopStyleVar();
            ImGui::EndChild();

            // Vertical divider
            const ImVec2 popupPos = ImGui::GetWindowPos();
            const ImVec2 popupSize = ImGui::GetWindowSize();
            const float dividerX = popupPos.x + sidebarWidth;
            ImGui::GetWindowDrawList()->AddLine(
                ImVec2(dividerX, popupPos.y),
                ImVec2(dividerX, popupPos.y + popupSize.y),
                ImGui::GetColorU32(HexColor(Colors::BORDER_SUBTLE)),
                1.0F
            );

            ImGui::SameLine(0, 0);

            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12.0F, 12.0F));
            ImGui::BeginChild("##PrefsContent", ImVec2(0, 0), ImGuiChildFlags_AlwaysUseWindowPadding);
            ImGui::PopStyleVar();
            switch (activeSection) {
                case PrefsSection::General:
                    DrawGeneralSection(context);
                    break;
                case PrefsSection::Appearance:
                    DrawAppearanceSection(context);
                    break;
                case PrefsSection::AndroidSdk:
                    SectionHeader("Android JDK/SDK", "Manage Java, the Android SDK location, platforms, and tools.");
                    DrawJdkSection(
                        context,
                        javaHomeBuffer,
                        sizeof(javaHomeBuffer),
                        javaVersionState
                    );
                    ImGui::Spacing();
                    ImGui::Separator();
                    ImGui::Spacing();
                    DrawAndroidSdkSection(
                        context,
                        sdkPathBuffer,
                        sizeof(sdkPathBuffer)
                    );
                    break;
            }
            ImGui::EndChild();

            ImGui::EndPopup();
        } else {
            ImGui::PopStyleVar();
        }
    }
}
