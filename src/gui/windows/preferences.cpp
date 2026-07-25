//
// Created by AbdulMuaz Aqeel on 18/04/2026.
//

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <sstream>
#include <vector>

#include "imgui.h"
#include "imgui_internal.h"

#include "preferences.h"
#include "../widgets.h"
#include "../fonts.h"
#include "../localization.h"
#include "../theme.h"
#include "../application.h"
#include "../../core/paths.h"
#include "../../core/sdk.h"
#include "../../core/file_dialog.h"
#include "../../core/process.h"

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

        struct JavaVersionState {
            std::string Path;
            std::string Text;
            bool HasJava = false;
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
            ImGui::Spacing();
            ImGui::PopID();
            return changed;
        }

        std::string JavaExecutablePath(const std::string &path) {
#if defined(_WIN32)
            return Paths::JoinPaths({path, "bin", "java.exe"});
#else
            return Paths::JoinPaths({path, "bin", "java"});
#endif
        }

        std::string NormalizeJavaHomePath(const std::string &path) {
            if (path.empty() || std::filesystem::exists(JavaExecutablePath(path))) {
                return path;
            }

            const std::string bundleHome = Paths::JoinPaths({path, "Contents", "Home"});
            if (std::filesystem::exists(JavaExecutablePath(bundleHome))) {
                return bundleHome;
            }
            return path;
        }

        bool LooksLikeJavaHome(const std::string &path) {
            return path.empty() || std::filesystem::exists(JavaExecutablePath(NormalizeJavaHomePath(path)));
        }

        std::string FirstNonEmptyLine(const std::string &text) {
            std::istringstream stream(text);
            std::string line;
            while (std::getline(stream, line)) {
                while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\t')) {
                    line.pop_back();
                }
                const auto start = line.find_first_not_of(" \t");
                if (start != std::string::npos) {
                    return line.substr(start);
                }
            }
            return "";
        }

        void RefreshJavaVersionState(JavaVersionState &state, const std::string &javaHomePath) {
            const std::string normalizedPath = NormalizeJavaHomePath(javaHomePath);
            if (state.Path == normalizedPath && !state.Text.empty()) {
                return;
            }

            state.Path = normalizedPath;
            state.HasJava = false;

            if (normalizedPath.empty()) {
                state.Text = "Using the system default Java environment.";
                return;
            }

            const std::string javaPath = JavaExecutablePath(normalizedPath);
            if (!std::filesystem::exists(javaPath)) {
                state.Text = "No java executable found under bin.";
                return;
            }

            state.HasJava = true;
            const std::string output = RunCommandArgs(javaPath, {"-version"});
            const std::string firstLine = FirstNonEmptyLine(output);
            state.Text = firstLine.empty() ? "Unable to read Java version." : firstLine;
        }

        void LabelText(const char *label) {
            ImGui::PushStyleColor(ImGuiCol_Text, HexColor(Colors::TEXT_PRIMARY));
            ImGui::TextUnformatted(Tr(label));
            ImGui::PopStyleColor();
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

        void DrawAndroidSdkSection(
            Context &context,
            char *sdkPathBuffer,
            const size_t sdkBufferSize
        ) {
            SectionHeader("Android SDK", "Where CoreDeck looks for the emulator and command-line tools.");

            LabelText("SDK root");
            const float browseWidth = Em(12.0F);
            const float spacing = ImGui::GetStyle().ItemSpacing.x;
            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - browseWidth - spacing);
            ImGui::InputTextWithHint("##SdkPrefs", Tr("Path to Android SDK"), sdkPathBuffer, sdkBufferSize);
            ImGui::SameLine();
            if (PrimaryButton("Browse...", true, ImVec2(browseWidth, 0))) {
                if (const auto picked = FileDialog::PickFolder(Tr("Select Android SDK directory"), sdkPathBuffer)) {
                    strncpy(sdkPathBuffer, picked->c_str(), sdkBufferSize - 1);
                    sdkPathBuffer[sdkBufferSize - 1] = '\0';
                }
            }

            const std::string pathStr = sdkPathBuffer;
            const bool pathOk = Paths::Onboarding::ValidateSdkPath(pathStr);

            if (!pathStr.empty()) {
                if (pathOk) {
                    ImGui::TextColored(HexColor(Colors::POSITIVE), "%s", Tr("Valid Android SDK path."));
                } else {
                    ImGui::TextColored(
                        HexColor(Colors::NEGATIVE),
                        "%s",
                        Tr("Not a valid SDK (need emulator and cmdline-tools with avdmanager).")
                    );
                }
            } else {
                ImGui::PushStyleColor(ImGuiCol_Text, HexColor(Colors::TEXT_SUBTLE));
                ImGui::TextUnformatted(Tr("Leave empty to auto-detect from ANDROID_HOME or default install paths."));
                ImGui::PopStyleColor();
            }

            ImGui::Spacing();
            ImGui::Spacing();

            if (PrimaryButton("Apply SDK Path", pathOk)) {
                Paths::Onboarding::SaveSdkPathOverride(pathStr);
                context.Host.Sdk = DetectAndroidSdk();
                context.Host.Sdk.JavaHomePath = context.Prefs.JavaHomePath;
                context.Host.Manager.SetSdk(context.Host.Sdk);
                RefreshAvds(context);
                context.UI.HideInvalidSdkPathBanner = false;
                PersistAppSettings(context);
            }
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled) && !pathOk) {
                ImGui::SetTooltip("%s", Tr("Fix the path or validation errors before applying."));
            }

            ImGui::SameLine();
            if (PrimaryButton("Use Default Discovery", true)) {
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
        }

        void DrawJdkSection(
            Context &context,
            char *javaHomeBuffer,
            const size_t javaHomeBufferSize,
            JavaVersionState &versionState
        ) {
            SectionHeader("JDK", "Java used by Android SDK command-line tools launched from CoreDeck.");
            const float browseWidth = Em(12.0F);
            const float spacing = ImGui::GetStyle().ItemSpacing.x;
            LabelText("JDK home");
            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - browseWidth - spacing);
            ImGui::InputTextWithHint("##JavaHomePrefs", Tr("Path to JDK home"), javaHomeBuffer, javaHomeBufferSize);
            ImGui::SameLine();
            if (PrimaryButton("Browse...", true, ImVec2(browseWidth, 0))) {
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

            RefreshJavaVersionState(versionState, javaHomePath);
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

            const bool canApplyJavaHome = javaHomePath.empty() || versionState.HasJava;
            if (PrimaryButton("Apply JDK Path", canApplyJavaHome)) {
                context.Prefs.JavaHomePath = javaHomePath;
                context.Host.Sdk.JavaHomePath = context.Prefs.JavaHomePath;
                context.Host.Manager.SetSdk(context.Host.Sdk);
                strncpy(javaHomeBuffer, javaHomePath.c_str(), javaHomeBufferSize - 1);
                javaHomeBuffer[javaHomeBufferSize - 1] = '\0';
                PersistAppSettings(context);
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
            if (PrimaryButton("Use System Java", true)) {
                context.Prefs.JavaHomePath.clear();
                context.Host.Sdk.JavaHomePath.clear();
                context.Host.Manager.SetSdk(context.Host.Sdk);
                javaHomeBuffer[0] = '\0';
                PersistAppSettings(context);
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
        static JavaVersionState javaVersionState;
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
