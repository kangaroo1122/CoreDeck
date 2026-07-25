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
            std::future<LicenseStatus> LicenseCheckFuture;
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
                return CheckSdkLicenses(sdk);
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
                    FailOnboardingSdkBootstrap(work, result.Error);
                    return;
                }

                work.Error.clear();
                StartOnboardingLicenseCheck(context, work);
            }

            if (work.LicenseCheckFuture.valid() &&
                work.LicenseCheckFuture.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
                const LicenseStatus status = work.LicenseCheckFuture.get();
                if (status == LicenseStatus::AllAccepted) {
                    StartOnboardingBasePackageInstall(context, work);
                } else if (status == LicenseStatus::SomeUnaccepted) {
                    work.Busy = false;
                    work.AwaitingLicenseConsent = true;
                    SetOnboardingProgress(work.Progress, 0.02F, "Accept Android SDK License Terms");
                } else {
                    FailOnboardingSdkBootstrap(work, "Could not query license state. Check that the SDK Manager is working.");
                }
            }

            if (work.LicenseAcceptFuture.valid() &&
                work.LicenseAcceptFuture.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
                const bool ok = work.LicenseAcceptFuture.get();
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
                    FailOnboardingSdkBootstrap(work, result.Error);
                    return;
                }

                work.Error.clear();
                ApplyOnboardingSdkPath(context, work.SdkRoot);
            }
        }

        void DrawOnboardingSdkProgress(const OnboardingSdkBootstrapWork &work) {
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
                ImGui::TextDisabled("%s", detailText.c_str());
            }
            ImGui::PushStyleColor(ImGuiCol_PlotHistogram, HexColor(Colors::POSITIVE));
            ImGui::ProgressBar(percent, ImVec2(-1.0F, 0.0F));
            ImGui::PopStyleColor();
        }

        void DrawOnboardingLicenseConsent(
            const Context &context,
            OnboardingSdkBootstrapWork &work
        ) {
            if (!work.AwaitingLicenseConsent) {
                return;
            }

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();
            ImGui::Text("%s", Tr("Accept Android SDK License Terms"));
            ImGui::TextWrapped(
                "%s",
                Tr("Some Android SDK package licenses have not been accepted yet. To install the base SDK tools, you must agree to Google's Android SDK license terms.")
            );
            if (PrimaryButton("Open license terms in browser")) {
                OpenUrl("https://developer.android.com/studio/terms");
            }

            if (work.Busy) {
                ImGui::Spacing();
                ImGui::TextDisabled("%s", Tr("Recording acceptance with the SDK Manager..."));
            }

            ImGui::Spacing();
            const float actionSpacing = ImGui::GetStyle().ItemSpacing.x;
            const float halfWidth = (ImGui::GetContentRegionAvail().x - actionSpacing) * 0.5F;
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
            OnboardingSdkBootstrapWork &bootstrap
        ) {
            PollOnboardingSdkBootstrap(context, bootstrap);
            if (pathBuffer[0] == '\0') {
                const std::string defaultPath = Paths::GetAndroidSdkDefaultPath();
                strncpy(pathBuffer, defaultPath.c_str(), pathBufferSize - 1);
                pathBuffer[pathBufferSize - 1] = '\0';
            }

            VerticalCenter(390.0F);

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

            ImGui::Text("%s", Tr("SDK path"));
            ImGui::SetNextItemWidth(formWidth - browseWidth - ImGui::GetStyle().ItemSpacing.x);
            ImGui::InputTextWithHint("##sdk_path", Tr("e.g. /Users/you/Library/Android/sdk"), pathBuffer, pathBufferSize);
            ImGui::SameLine();
            if (PrimaryButton("Browse...", true, ImVec2(browseWidth, 0))) {
                const auto picked = FileDialog::PickFolder(Tr("Select your Android SDK folder"), pathBuffer);
                if (picked.has_value()) {
                    strncpy(pathBuffer, picked->c_str(), pathBufferSize - 1);
                    pathBuffer[pathBufferSize - 1] = '\0';
                }
            }

            ImGui::Spacing();
            const std::string currentPath = pathBuffer;
            const bool isValid = Paths::Onboarding::ValidateSdkPath(currentPath);
            const bool hasSdkManager = HasSdkManager(currentPath);
            if (!currentPath.empty()) {
                if (isValid) {
                    ImGui::TextColored(
                        HexColor(Colors::POSITIVE),
                        "%s",
                        Tr("Looks good. Found the Android emulator at this location.")
                    );
                } else if (hasSdkManager) {
                    ImGui::TextColored(
                        HexColor(Colors::WARNING),
                        "%s",
                        Tr("Android SDK tools are incomplete. Install the base tools here, or continue and finish later from Android JDK/SDK preferences.")
                    );
                } else {
                    ImGui::TextColored(
                        HexColor(Colors::WARNING),
                        "%s",
                        Tr("Android SDK tools are missing. Install the base tools here or choose an existing SDK root.")
                    );
                }
            } else {
                ImGui::TextColored(
                    HexColor(Colors::TEXT_MUTED),
                    "%s",
                    Tr("Choose the folder containing your Android SDK (cmdline-tools, emulator, platform-tools, etc).")
                );
            }

            if (!bootstrap.Error.empty()) {
                ImGui::TextColored(HexColor(Colors::NEGATIVE), "%s", Tr(bootstrap.Error.c_str()));
            }
            DrawOnboardingSdkProgress(bootstrap);
            DrawOnboardingLicenseConsent(context, bootstrap);

            ImGui::Spacing();
            if (!isValid) {
                const bool canAct = !bootstrap.Busy && !bootstrap.AwaitingLicenseConsent && !currentPath.empty();

                if (!hasSdkManager) {
                    if (PositiveButton("Install Android SDK Tools", canAct, ImVec2(-1.0F, 0))) {
                        StartOnboardingSdkInstall(bootstrap, currentPath);
                    }
                } else if (PositiveButton(
                               "Install Android SDK Tools",
                               canAct,
                               ImVec2(-1.0F, 0)
                           )) {
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
                    (isValid || hasSdkManager) && !bootstrap.Busy && !bootstrap.AwaitingLicenseConsent,
                    ImVec2(footerButtonWidth, 0)
                )) {
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
        static bool initialized = false;
        static OnboardingSdkBootstrapWork bootstrap;

        if (!initialized) {
            if (!context.Host.Sdk.SdkPath.empty()) {
                strncpy(pathBuffer, context.Host.Sdk.SdkPath.c_str(), sizeof(pathBuffer) - 1);
                pathBuffer[sizeof(pathBuffer) - 1] = '\0';
            } else {
                const std::string defaultPath = Paths::GetAndroidSdkDefaultPath();
                strncpy(pathBuffer, defaultPath.c_str(), sizeof(pathBuffer) - 1);
                pathBuffer[sizeof(pathBuffer) - 1] = '\0';
            }
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
            ImGuiWindowFlags_NoScrollbar |
            ImGuiWindowFlags_NoDocking |
            ImGuiWindowFlags_NoBringToFrontOnFocus |
            ImGuiWindowFlags_NoNavFocus;

        ImGui::Begin("##Onboarding", nullptr, FLAGS);

        switch (step) {
            case Step::Welcome:
                BuildWelcomeStep(step);
                break;
            case Step::SdkSetup:
                BuildSdkSetupStep(context, step, pathBuffer, sizeof(pathBuffer), bootstrap);
                break;
        }

        ImGui::End();
    }
}
