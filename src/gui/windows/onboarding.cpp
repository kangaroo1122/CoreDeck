//
// Created by AbdulMuaz Aqeel on 15/04/2026.
//

#include <chrono>
#include <cstring>
#include <future>
#include <memory>
#include <mutex>
#include <string>

#include "imgui.h"

#include "onboarding.h"
#include "../application.h"
#include "../localization.h"
#include "../widgets.h"
#include "../theme.h"
#include "../../core/file_dialog.h"
#include "../../core/jdk.h"
#include "../../core/paths.h"
#include "../../core/sdk_bootstrap.h"
#include "../../core/sdk_packages.h"
#include "../../core/sdk.h"
#include "../../core/system_image.h"
#include "../../core/utilities.h"

namespace CoreDeck {
    namespace {
        enum class Step : uint8_t {
            Welcome,
            SdkSetup
        };

        struct OnboardingSdkBootstrapWork {
            bool Busy = false;
            bool AwaitingLicenseConsent = false;
            std::future<SdkBootstrapResult> ToolsFuture;
            std::future<LicenseCheckResult> LicenseCheckFuture;
            std::future<bool> LicenseAcceptFuture;
            std::future<SdkBootstrapResult> PackagesFuture;
            std::shared_ptr<SdkOperationProgress> Progress;
            std::string SdkRoot;
            std::string Error;
        };

        void CenteredText(const char *text, const ImVec4 &color) {
            const char *translatedText = Tr(text);
            const float width = ImGui::CalcTextSize(translatedText).x;
            ImGui::SetCursorPosX((ImGui::GetWindowWidth() - width) * 0.5F);
            ImGui::TextColored(color, "%s", translatedText);
        }

        void VerticalCenter(const float contentHeight) {
            const float available = ImGui::GetContentRegionAvail().y;
            const float offset = (available - contentHeight) * 0.5F;
            if (offset > 0.0F) {
                ImGui::Dummy(ImVec2(0, offset));
            }
        }

        void BuildWelcomeStep(Step &step) {
            VerticalCenter(260.0F);

            ImGui::PushFont(ImGui::GetIO().Fonts->Fonts[0]);
            CenteredText(COREDECK_TITLE, HexColor(Colors::TEXT_PRIMARY));
            ImGui::PopFont();

            ImGui::Spacing();
            CenteredText("Your Android emulator command center.", HexColor(Colors::TEXT_MUTED));

            ImGui::Spacing();
            ImGui::Spacing();
            ImGui::Spacing();

            constexpr const char *WELCOME_LINE1 = "Welcome! CoreDeck helps you manage Android emulators";
            constexpr const char *WELCOME_LINE2 = "faster and cleaner than the default tooling.";
            CenteredText(WELCOME_LINE1, HexColor(Colors::TEXT_SUBTLE));
            CenteredText(WELCOME_LINE2, HexColor(Colors::TEXT_SUBTLE));

            ImGui::Spacing();
            ImGui::Spacing();
            ImGui::Spacing();
            ImGui::Spacing();

            const float buttonWidth = Em(20.0F);
            ImGui::SetCursorPosX((ImGui::GetWindowWidth() - buttonWidth) * 0.5F);
            if (PositiveButton("Get Started", true, ImVec2(buttonWidth, 0))) {
                step = Step::SdkSetup;
            }
        }

        void ApplyOnboardingSdkPath(Context &context, const std::string &sdkRoot) {
            Paths::Onboarding::SaveSdkPathOverride(sdkRoot);
            context.Host.Sdk = DetectAndroidSdk();
            context.Host.Sdk.JavaHomePath = context.Prefs.JavaHomePath;
            context.Host.Manager.SetSdk(context.Host.Sdk);
            context.UI.HideInvalidSdkPathBanner = false;
        }

        void ApplyOnboardingJavaHome(Context &context, const std::string &javaHomePath) {
            context.Prefs.JavaHomePath = javaHomePath;
            context.Host.Sdk.JavaHomePath = context.Prefs.JavaHomePath;
            context.Host.Manager.SetSdk(context.Host.Sdk);
            PersistAppSettings(context);
        }

        void SetOnboardingProgress(
            const std::shared_ptr<SdkOperationProgress> &progress,
            const float percent,
            const char *status,
            const char *detail = ""
        ) {
            if (!progress) {
                return;
            }

            std::lock_guard lock(progress->Mutex);
            progress->Percent = percent;
            progress->StatusText = status;
            progress->DetailText = detail;
            progress->Finished = false;
            progress->Succeeded = false;
        }

        void FailOnboardingSdkBootstrap(
            OnboardingSdkBootstrapWork &work,
            const std::string &error
        ) {
            work.Busy = false;
            work.AwaitingLicenseConsent = false;
            work.Error = error;
            if (work.Progress) {
                std::lock_guard lock(work.Progress->Mutex);
                work.Progress->Finished = true;
                work.Progress->Succeeded = false;
                work.Progress->StatusText = "Android SDK setup failed.";
                work.Progress->DetailText = error;
            }
        }

        void CancelOnboardingSdkBootstrap(OnboardingSdkBootstrapWork &work) {
            if (!work.Progress) {
                return;
            }

            work.Progress->CancelRequested.store(true);
            std::lock_guard lock(work.Progress->Mutex);
            work.Progress->StatusText = "Cancelling...";
            work.Progress->DetailText.clear();
        }

        bool CompleteOnboardingCancelIfRequested(OnboardingSdkBootstrapWork &work) {
            if (!work.Progress || !work.Progress->CancelRequested.load()) {
                return false;
            }

            work.Busy = false;
            work.AwaitingLicenseConsent = false;
            work.Error.clear();
            std::lock_guard lock(work.Progress->Mutex);
            work.Progress->Finished = true;
            work.Progress->Succeeded = false;
            work.Progress->StatusText = "Cancelled.";
            work.Progress->DetailText.clear();
            return true;
        }

        void WrappedColoredText(const ImVec4 &color, const float width, const char *text) {
            ImGui::PushStyleColor(ImGuiCol_Text, color);
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + width);
            ImGui::TextWrapped("%s", Tr(text));
            ImGui::PopTextWrapPos();
            ImGui::PopStyleColor();
        }

        void StartOnboardingLicenseCheck(
            const Context &context,
            OnboardingSdkBootstrapWork &work
        ) {
            const SdkInfo sdk = BuildSdkInfoFromSdkRoot(work.SdkRoot, context.Prefs.JavaHomePath);
            if (sdk.SdkManagerPath.empty()) {
                FailOnboardingSdkBootstrap(work, "SDK Manager was not found.");
                return;
            }

            SetOnboardingProgress(work.Progress, 0.02F, "Checking licenses...");
            work.Busy = true;
            work.LicenseCheckFuture = std::async(std::launch::async, [sdk] {
                return CheckSdkLicensesDetailed(sdk);
            });
        }

        void StartOnboardingBasePackageInstall(
            const Context &context,
            OnboardingSdkBootstrapWork &work
        ) {
            const SdkInfo sdk = BuildSdkInfoFromSdkRoot(work.SdkRoot, context.Prefs.JavaHomePath);
            if (sdk.SdkManagerPath.empty()) {
                FailOnboardingSdkBootstrap(work, "SDK Manager was not found.");
                return;
            }

            work.Busy = true;
            const auto progress = work.Progress;
            work.PackagesFuture = std::async(std::launch::async, [sdk, progress] {
                return InstallBaseSdkPackages(sdk, progress);
            });
        }

        void PollOnboardingSdkBootstrap(Context &context, OnboardingSdkBootstrapWork &work) {
            if (work.ToolsFuture.valid() &&
                work.ToolsFuture.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
                const SdkBootstrapResult result = work.ToolsFuture.get();
                if (!result.Succeeded) {
                    if (result.Cancelled) {
                        work.Busy = false;
                        work.Error.clear();
                        return;
                    }
                    FailOnboardingSdkBootstrap(work, result.Error);
                    return;
                }

                work.Error.clear();
                StartOnboardingLicenseCheck(context, work);
            }

            if (work.LicenseCheckFuture.valid() &&
                work.LicenseCheckFuture.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
                const LicenseCheckResult result = work.LicenseCheckFuture.get();
                if (CompleteOnboardingCancelIfRequested(work)) {
                    return;
                }
                const LicenseStatus status = result.Status;
                if (status == LicenseStatus::AllAccepted) {
                    StartOnboardingBasePackageInstall(context, work);
                } else if (status == LicenseStatus::SomeUnaccepted) {
                    work.Busy = false;
                    work.AwaitingLicenseConsent = true;
                    SetOnboardingProgress(work.Progress, 0.02F, "Accept Android SDK License Terms");
                } else {
                    FailOnboardingSdkBootstrap(
                        work,
                        result.Output.empty()
                            ? "Could not query license state. Check that the SDK Manager is working."
                            : result.Output
                    );
                }
            }

            if (work.LicenseAcceptFuture.valid() &&
                work.LicenseAcceptFuture.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
                const bool ok = work.LicenseAcceptFuture.get();
                if (CompleteOnboardingCancelIfRequested(work)) {
                    return;
                }
                if (ok) {
                    work.AwaitingLicenseConsent = false;
                    StartOnboardingBasePackageInstall(context, work);
                } else {
                    FailOnboardingSdkBootstrap(work, "License acceptance failed. Try again or accept via Android Studio.");
                }
            }

            if (work.PackagesFuture.valid() &&
                work.PackagesFuture.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
                const SdkBootstrapResult result = work.PackagesFuture.get();
                work.Busy = false;
                if (!result.Succeeded) {
                    if (result.Cancelled) {
                        work.Error.clear();
                        return;
                    }
                    FailOnboardingSdkBootstrap(work, result.Error);
                    return;
                }

                work.Error.clear();
                ApplyOnboardingSdkPath(context, work.SdkRoot);
            }
        }

        void DrawOnboardingSdkProgress(const OnboardingSdkBootstrapWork &work, const float width) {
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

        void DrawOnboardingLicenseConsent(
            const Context &context,
            OnboardingSdkBootstrapWork &work,
            const float width
        ) {
            if (!work.AwaitingLicenseConsent) {
                return;
            }

            ImGui::Spacing();
            ImGui::Spacing();
            ImGui::Text("%s", Tr("Accept Android SDK License Terms"));
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + width);
            ImGui::TextWrapped(
                "%s",
                Tr("Some Android SDK package licenses have not been accepted yet. To install the base SDK tools, you must agree to Google's Android SDK license terms.")
            );
            ImGui::PopTextWrapPos();
            if (PrimaryButton("Open license terms in browser", true, ImVec2(width, 0))) {
                OpenUrl("https://developer.android.com/studio/terms");
            }

            if (work.Busy) {
                ImGui::Spacing();
                ImGui::TextDisabled("%s", Tr("Recording acceptance with the SDK Manager..."));
            }

            ImGui::Spacing();
            const float actionSpacing = ImGui::GetStyle().ItemSpacing.x;
            const float halfWidth = (width - actionSpacing) * 0.5F;
            if (PositiveButton("Agree & Install", !work.Busy, ImVec2(halfWidth, 0))) {
                const SdkInfo sdk = BuildSdkInfoFromSdkRoot(work.SdkRoot, context.Prefs.JavaHomePath);
                work.Busy = true;
                work.Error.clear();
                SetOnboardingProgress(work.Progress, 0.02F, "Recording acceptance with the SDK Manager...");
                work.LicenseAcceptFuture = std::async(std::launch::async, [sdk] {
                    return AcceptSdkLicenses(sdk);
                });
            }
            ImGui::SameLine();
            if (NegativeButton("Cancel", !work.Busy, ImVec2(halfWidth, 0))) {
                work.AwaitingLicenseConsent = false;
            }
        }

        std::string DrawOnboardingJdkPicker(
            Context &context,
            char *javaHomeBuffer,
            const size_t javaHomeBufferSize,
            JavaHomeStatus &versionState,
            const float width,
            const bool enabled
        ) {
            ImGui::Text("%s", Tr("JDK home"));
            const float browseWidth = Em(11.0F);
            const float spacing = ImGui::GetStyle().ItemSpacing.x;

            if (!enabled) {
                ImGui::BeginDisabled();
            }

            ImGui::SetNextItemWidth(width - browseWidth - spacing);
            ImGui::InputTextWithHint("##OnboardingJavaHome", Tr("Path to JDK home"), javaHomeBuffer, javaHomeBufferSize);
            ImGui::SameLine();
            if (PrimaryButton("Browse...###OnboardingJdkBrowse", true, ImVec2(browseWidth, 0))) {
                const auto picked = FileDialog::PickFolder(Tr("Select JDK home directory"), javaHomeBuffer);
                if (picked.has_value()) {
                    const std::string normalized = NormalizeJavaHomePath(*picked);
                    strncpy(javaHomeBuffer, normalized.c_str(), javaHomeBufferSize - 1);
                    javaHomeBuffer[javaHomeBufferSize - 1] = '\0';
                }
            }

            const std::string javaHomePath = NormalizeJavaHomePath(javaHomeBuffer);
            if (javaHomePath.empty()) {
                ImGui::PushStyleColor(ImGuiCol_Text, HexColor(Colors::TEXT_SUBTLE));
                ImGui::TextWrapped("%s", Tr("Leave empty to use the system default Java environment."));
                ImGui::PopStyleColor();
            } else if (LooksLikeJavaHome(javaHomePath)) {
                ImGui::TextColored(HexColor(Colors::POSITIVE), "%s", Tr("Java executable found under this JDK home."));
            } else {
                ImGui::TextColored(HexColor(Colors::NEGATIVE), "%s", Tr("No java executable found under bin."));
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

            if (!javaHomePath.empty()) {
                if (PrimaryButton("Use System Java", true)) {
                    javaHomeBuffer[0] = '\0';
                    RefreshJavaHomeStatus(versionState, "");
                    ApplyOnboardingJavaHome(context, "");
                }
            }

            if (!enabled) {
                ImGui::EndDisabled();
            }

            return javaHomePath;
        }

        void StartOnboardingSdkInstall(
            OnboardingSdkBootstrapWork &work,
            const std::string &sdkRoot
        ) {
            work.Progress = std::make_shared<SdkOperationProgress>();
            work.SdkRoot = sdkRoot;
            work.Error.clear();
            work.AwaitingLicenseConsent = false;
            work.Busy = true;
            const auto progress = work.Progress;
            const std::string root = work.SdkRoot;
            work.ToolsFuture = std::async(std::launch::async, [root, progress] {
                return BootstrapCommandLineTools(root, progress);
            });
        }

        void BuildSdkSetupStep(
            Context &context,
            Step &step,
            char *pathBuffer,
            size_t pathBufferSize,
            char *javaHomeBuffer,
            size_t javaHomeBufferSize,
            JavaHomeStatus &javaVersionState,
            OnboardingSdkBootstrapWork &bootstrap
        ) {
            PollOnboardingSdkBootstrap(context, bootstrap);
            if (pathBuffer[0] == '\0') {
                const std::string defaultPath = Paths::GetAndroidSdkDefaultPath();
                strncpy(pathBuffer, defaultPath.c_str(), pathBufferSize - 1);
                pathBuffer[pathBufferSize - 1] = '\0';
            }

            VerticalCenter(560.0F);

            ImGui::PushFont(ImGui::GetIO().Fonts->Fonts[0]);
            CenteredText("Locate your Android SDK", HexColor(Colors::TEXT_PRIMARY));
            ImGui::PopFont();

            ImGui::Spacing();
            CenteredText("CoreDeck needs to know where your Android SDK lives.", HexColor(Colors::TEXT_MUTED));
            CenteredText("This is where 'emulator', 'avdmanager' and system images are installed.", HexColor(Colors::TEXT_MUTED));

            ImGui::Spacing();
            ImGui::Spacing();
            ImGui::Spacing();

            const float formWidth = Em(66.0F);
            const float browseWidth = Em(11.0F);
            ImGui::SetCursorPosX((ImGui::GetWindowWidth() - formWidth) * 0.5F);
            ImGui::BeginGroup();

            const bool canEditPaths = !bootstrap.Busy && !bootstrap.AwaitingLicenseConsent;
            const std::string javaHomePath = DrawOnboardingJdkPicker(
                context,
                javaHomeBuffer,
                javaHomeBufferSize,
                javaVersionState,
                formWidth,
                canEditPaths
            );
            const bool javaHomeOk = javaHomePath.empty() || javaVersionState.HasJava;

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            ImGui::Text("%s", Tr("SDK path"));
            ImGui::SetNextItemWidth(formWidth - browseWidth - ImGui::GetStyle().ItemSpacing.x);
            if (!canEditPaths) {
                ImGui::BeginDisabled();
            }
            {
                ImGui::InputTextWithHint("##sdk_path", Tr("e.g. /Users/you/Library/Android/sdk"), pathBuffer, pathBufferSize);
                ImGui::SameLine();
                if (PrimaryButton("Browse...###OnboardingSdkBrowse", true, ImVec2(browseWidth, 0))) {
                    const auto picked = FileDialog::PickFolder(Tr("Select your Android SDK folder"), pathBuffer);
                    if (picked.has_value()) {
                        strncpy(pathBuffer, picked->c_str(), pathBufferSize - 1);
                        pathBuffer[pathBufferSize - 1] = '\0';
                    }
                }
            }
            if (!canEditPaths) {
                ImGui::EndDisabled();
            }

            ImGui::Spacing();
            const std::string currentPath = pathBuffer;
            const bool isValid = Paths::Onboarding::ValidateSdkPath(currentPath);
            const bool hasSdkManager = HasSdkManager(currentPath);
            if (!currentPath.empty()) {
                if (isValid) {
                    WrappedColoredText(
                        HexColor(Colors::POSITIVE),
                        formWidth,
                        "Looks good. Found the Android emulator at this location."
                    );
                } else if (hasSdkManager) {
                    WrappedColoredText(
                        HexColor(Colors::WARNING),
                        formWidth,
                        "Android SDK tools are incomplete. Install the base tools here, or continue and finish later from Android JDK/SDK preferences."
                    );
                } else {
                    WrappedColoredText(
                        HexColor(Colors::WARNING),
                        formWidth,
                        "Android SDK tools are missing. Install the base tools here or choose an existing SDK root."
                    );
                }
            } else {
                WrappedColoredText(
                    HexColor(Colors::TEXT_MUTED),
                    formWidth,
                    "Choose the folder containing your Android SDK (cmdline-tools, emulator, platform-tools, etc)."
                );
            }

            if (!bootstrap.Error.empty()) {
                WrappedColoredText(HexColor(Colors::NEGATIVE), formWidth, bootstrap.Error.c_str());
            } else {
                DrawOnboardingSdkProgress(bootstrap, formWidth);
            }
            DrawOnboardingLicenseConsent(context, bootstrap, formWidth);
            if (bootstrap.Busy && !bootstrap.AwaitingLicenseConsent && bootstrap.Progress) {
                ImGui::Spacing();
                if (NegativeButton("Cancel", !bootstrap.Progress->CancelRequested.load(), ImVec2(formWidth, 0))) {
                    CancelOnboardingSdkBootstrap(bootstrap);
                }
            }

            ImGui::Spacing();
            if (!isValid) {
                const bool canAct = canEditPaths && javaHomeOk && !currentPath.empty();

                if (!hasSdkManager) {
                    if (PositiveButton("Install Android SDK Tools", canAct, ImVec2(formWidth, 0))) {
                        ApplyOnboardingJavaHome(context, javaHomePath);
                        StartOnboardingSdkInstall(bootstrap, currentPath);
                    }
                } else if (PositiveButton(
                               "Install Android SDK Tools",
                               canAct,
                               ImVec2(formWidth, 0)
                           )) {
                    ApplyOnboardingJavaHome(context, javaHomePath);
                    bootstrap.SdkRoot = currentPath;
                    bootstrap.Progress = std::make_shared<SdkOperationProgress>();
                    StartOnboardingLicenseCheck(context, bootstrap);
                }
            }

            ImGui::EndGroup();

            ImGui::Spacing();
            ImGui::Spacing();
            ImGui::Spacing();

            const float footerButtonWidth = Em(14.0F);
            const float footerWidth = (footerButtonWidth * 2.0F) + ImGui::GetStyle().ItemSpacing.x;
            ImGui::SetCursorPosX((ImGui::GetWindowWidth() - footerWidth) * 0.5F);
            ImGui::BeginGroup();

            if (PrimaryButton("Back", true, ImVec2(footerButtonWidth, 0))) {
                step = Step::Welcome;
            }

            ImGui::SameLine();
            if (PositiveButton(
                    "Continue",
                    (isValid || hasSdkManager) && canEditPaths && javaHomeOk,
                    ImVec2(footerButtonWidth, 0)
                )) {
                ApplyOnboardingJavaHome(context, javaHomePath);
                ApplyOnboardingSdkPath(context, currentPath);
                Paths::Onboarding::MarkFirstRunComplete();

                RefreshAvds(context);
                if (!context.Host.Sdk.IsFound) {
                    context.UI.ShowPreferences = true;
                }
                context.Flow.CurrentScreen = Screen::Main;
            }

            ImGui::EndGroup();
        }
    }

    void BuildOnboardingWindow(Context &context) {
        static auto step = Step::Welcome;
        static char pathBuffer[1024] = {};
        static char javaHomeBuffer[2048] = {};
        static bool initialized = false;
        static OnboardingSdkBootstrapWork bootstrap;
        static JavaHomeStatus javaVersionState;

        if (!initialized) {
            if (!context.Host.Sdk.SdkPath.empty()) {
                strncpy(pathBuffer, context.Host.Sdk.SdkPath.c_str(), sizeof(pathBuffer) - 1);
                pathBuffer[sizeof(pathBuffer) - 1] = '\0';
            } else {
                const std::string defaultPath = Paths::GetAndroidSdkDefaultPath();
                strncpy(pathBuffer, defaultPath.c_str(), sizeof(pathBuffer) - 1);
                pathBuffer[sizeof(pathBuffer) - 1] = '\0';
            }
            const std::string &javaHome = context.Prefs.JavaHomePath;
            strncpy(javaHomeBuffer, javaHome.c_str(), sizeof(javaHomeBuffer) - 1);
            javaHomeBuffer[sizeof(javaHomeBuffer) - 1] = '\0';
            initialized = true;
        }

        const ImGuiViewport *viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(viewport->WorkPos);
        ImGui::SetNextWindowSize(viewport->WorkSize);

        constexpr ImGuiWindowFlags FLAGS =
            ImGuiWindowFlags_NoTitleBar |
            ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoCollapse |
            ImGuiWindowFlags_NoDocking |
            ImGuiWindowFlags_NoBringToFrontOnFocus |
            ImGuiWindowFlags_NoNavFocus;

        ImGui::Begin("##Onboarding", nullptr, FLAGS);

        switch (step) {
            case Step::Welcome:
                BuildWelcomeStep(step);
                break;
            case Step::SdkSetup:
                BuildSdkSetupStep(
                    context,
                    step,
                    pathBuffer,
                    sizeof(pathBuffer),
                    javaHomeBuffer,
                    sizeof(javaHomeBuffer),
                    javaVersionState,
                    bootstrap
                );
                break;
        }

        ImGui::End();
    }
}
