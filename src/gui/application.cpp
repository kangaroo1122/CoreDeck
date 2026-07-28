//
// Created by AbdulMuaz Aqeel on 04/04/2026.
//

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#define GLFW_EXPOSE_NATIVE_WIN32
#endif

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <filesystem>
#include <future>
#include <mutex>
#include <vector>
#include "imgui.h"
#include "imgui_internal.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include <GLFW/glfw3.h>
#if defined(_WIN32)
#include <GLFW/glfw3native.h>
#endif

#include "application.h"
#include "fonts.h"
#include "localization.h"
#include "shared_folder_sync.h"
#include "theme.h"
#include "../core/app_settings.h"
#include "../core/paths.h"
#include "windows/about.h"
#include "windows/avd_info.h"
#include "windows/avd_list.h"
#include "windows/avd_logs.h"
#include "windows/avd_options.h"
#include "windows/create_avd.h"
#include "windows/delete_avd.h"
#include "windows/device_explorer.h"
#include "windows/install_image.h"
#include "windows/main_menu_bar.h"
#include "windows/onboarding.h"
#include "windows/preferences.h"
#include "windows/quit_confirm.h"
#include "windows/rename_avd.h"
#include "windows/sdk_banner.h"
#include "windows/storage.h"
#include "windows/update.h"
#include "../core/version_check.h"
#if defined(__APPLE__)
#include "../platform/macos_menu.h"
#endif

namespace CoreDeck {
    namespace {
        constexpr std::uint8_t DOCK_PANEL_AVD_LIST = 1U << 0U;
        constexpr std::uint8_t DOCK_PANEL_OPTIONS = 1U << 1U;
        constexpr std::uint8_t DOCK_PANEL_DETAILS = 1U << 2U;
        constexpr std::uint8_t DOCK_PANEL_OUTPUT_LOG = 1U << 3U;
        constexpr std::uint8_t DOCK_PANEL_DEVICE_EXPLORER = 1U << 4U;
        constexpr float DOCK_MIN_RATIO = 0.10F;

        struct DockLayoutRatios {
            float BottomGroup = 1.0F / 3.0F;
            float TopOptions = 0.25F;
            float TopDetails = 0.2625F;
            float TopSideOnlyOptions = 0.50F;
            float BottomExplorer = 1.0F / 3.0F;
        };

        struct DockLayoutNodeIds {
            ImGuiID TopGroup = 0;
            ImGuiID BottomGroup = 0;
            ImGuiID Options = 0;
            ImGuiID Avds = 0;
            ImGuiID Details = 0;
            ImGuiID OutputLog = 0;
            ImGuiID DeviceExplorer = 0;
        };

        struct DockLayoutState {
            DockLayoutRatios Ratios;
            DockLayoutNodeIds Nodes;
            std::uint8_t LastLayoutMask = 0xFF;
            bool BuiltOnce = false;
        };

        DockLayoutState &GetDockLayoutState() {
            static DockLayoutState layoutState;
            return layoutState;
        }

        void ShowFatalError(const char *title, const char *message) {
#if defined(_WIN32)
            MessageBoxA(nullptr, message, title, MB_OK | MB_ICONERROR);
#else
            (void) title;
            (void) std::fprintf(stderr, "%s\n", message);
#endif
        }

        void ConfigureDockNode(const ImGuiID id) {
            if (ImGuiDockNode *node = ImGui::DockBuilderGetNode(id)) {
                node->LocalFlags |= ImGuiDockNodeFlags_NoWindowMenuButton;
            }
        }

        float ClampDockRatio(const float value, const float fallback) {
            if (!std::isfinite(value)) {
                return fallback;
            }
            return std::clamp(value, DOCK_MIN_RATIO, 1.0F - DOCK_MIN_RATIO);
        }

        float DockNodeSizeOnAxis(const ImGuiDockNode *node, const ImGuiAxis axis) {
            if (node == nullptr) {
                return 0.0F;
            }
            const float size = axis == ImGuiAxis_X ? node->Size.x : node->Size.y;
            if (size > 0.0F) {
                return size;
            }
            return axis == ImGuiAxis_X ? node->SizeRef.x : node->SizeRef.y;
        }

        float DockNodeSize(const ImGuiID id, const ImGuiAxis axis) {
            return DockNodeSizeOnAxis(ImGui::DockBuilderGetNode(id), axis);
        }

        bool CaptureDockRatio(
            const ImGuiID firstId,
            const ImGuiID secondId,
            const ImGuiAxis axis,
            float &ratio,
            const bool captureSecond = false
        ) {
            const ImGuiDockNode *first = ImGui::DockBuilderGetNode(firstId);
            const ImGuiDockNode *second = ImGui::DockBuilderGetNode(secondId);
            const float firstSize = DockNodeSizeOnAxis(first, axis);
            const float secondSize = DockNodeSizeOnAxis(second, axis);
            const float total = firstSize + secondSize;
            if (total <= 0.0F) {
                return false;
            }

            ratio = ClampDockRatio((captureSecond ? secondSize : firstSize) / total, ratio);
            return true;
        }

        void NormalizeTopOuterRatios(DockLayoutRatios &ratios) {
            ratios.TopOptions = ClampDockRatio(ratios.TopOptions, 0.25F);
            ratios.TopDetails = ClampDockRatio(ratios.TopDetails, 0.2625F);
            ratios.TopSideOnlyOptions = ClampDockRatio(ratios.TopSideOnlyOptions, 0.50F);
        }

        void NormalizeDockRatios(DockLayoutRatios &ratios) {
            ratios.BottomGroup = ClampDockRatio(ratios.BottomGroup, 1.0F / 3.0F);
            ratios.BottomExplorer = ClampDockRatio(ratios.BottomExplorer, 1.0F / 3.0F);
            NormalizeTopOuterRatios(ratios);
        }

        void SeedDockLayoutStateFromContext(const Context &context) {
            DockLayoutState &state = GetDockLayoutState();
            state.Ratios.BottomGroup = context.UI.DockBottomGroupRatio;
            state.Ratios.TopOptions = context.UI.DockTopOptionsRatio;
            state.Ratios.TopDetails = context.UI.DockTopDetailsRatio;
            state.Ratios.TopSideOnlyOptions = context.UI.DockTopSideOnlyOptionsRatio;
            state.Ratios.BottomExplorer = context.UI.DockBottomExplorerRatio;
            NormalizeDockRatios(state.Ratios);
        }

        void SyncDockLayoutRatiosToContext(Context &context) {
            const DockLayoutState &state = GetDockLayoutState();
            if (!state.BuiltOnce) {
                return;
            }
            context.UI.DockBottomGroupRatio = state.Ratios.BottomGroup;
            context.UI.DockTopOptionsRatio = state.Ratios.TopOptions;
            context.UI.DockTopDetailsRatio = state.Ratios.TopDetails;
            context.UI.DockTopSideOnlyOptionsRatio = state.Ratios.TopSideOnlyOptions;
            context.UI.DockBottomExplorerRatio = state.Ratios.BottomExplorer;
        }

        std::uint8_t ResolveDockLayoutMask(const Context &context) {
            std::uint8_t mask = 0U;
            if (context.UI.ShowAvdListPanel) {
                mask |= DOCK_PANEL_AVD_LIST;
            }
            if (context.UI.ShowOptionsPanel) {
                mask |= DOCK_PANEL_OPTIONS;
            }
            if (context.UI.ShowDetailsPanel) {
                mask |= DOCK_PANEL_DETAILS;
            }
            if (context.UI.ShowLogPanel) {
                mask |= DOCK_PANEL_OUTPUT_LOG;
            }
            if (context.UI.ShowDeviceExplorerPanel) {
                mask |= DOCK_PANEL_DEVICE_EXPLORER;
            }
            return mask;
        }

        void DockWindowIfVisible(const char *windowName, const ImGuiID dockId) {
            if (dockId != 0) {
                ImGui::DockBuilderDockWindow(windowName, dockId);
            }
        }

        void CaptureTopDockRatios(DockLayoutState &state) {
            const std::uint8_t layoutMask = state.LastLayoutMask;
            const bool showLeft = (layoutMask & DOCK_PANEL_OPTIONS) != 0U;
            const bool showMiddle = (layoutMask & DOCK_PANEL_AVD_LIST) != 0U;
            const bool showRight = (layoutMask & DOCK_PANEL_DETAILS) != 0U;

            if (showLeft && showMiddle && showRight) {
                const float leftSize = DockNodeSize(state.Nodes.Options, ImGuiAxis_X);
                const float middleSize = DockNodeSize(state.Nodes.Avds, ImGuiAxis_X);
                const float rightSize = DockNodeSize(state.Nodes.Details, ImGuiAxis_X);
                const float total = leftSize + middleSize + rightSize;
                if (total > 0.0F) {
                    state.Ratios.TopOptions = ClampDockRatio(leftSize / total, state.Ratios.TopOptions);
                    state.Ratios.TopDetails = ClampDockRatio(rightSize / total, state.Ratios.TopDetails);
                }
                if (leftSize + rightSize > 0.0F) {
                    state.Ratios.TopSideOnlyOptions =
                        ClampDockRatio(leftSize / (leftSize + rightSize), state.Ratios.TopSideOnlyOptions);
                }
                NormalizeTopOuterRatios(state.Ratios);
                return;
            }

            if (showLeft && showMiddle) {
                CaptureDockRatio(state.Nodes.Options, state.Nodes.Avds, ImGuiAxis_X, state.Ratios.TopOptions);
                NormalizeTopOuterRatios(state.Ratios);
                return;
            }

            if (showMiddle && showRight) {
                CaptureDockRatio(
                    state.Nodes.Avds,
                    state.Nodes.Details,
                    ImGuiAxis_X,
                    state.Ratios.TopDetails,
                    true
                );
                NormalizeTopOuterRatios(state.Ratios);
                return;
            }

            if (showLeft && showRight) {
                CaptureDockRatio(
                    state.Nodes.Options,
                    state.Nodes.Details,
                    ImGuiAxis_X,
                    state.Ratios.TopSideOnlyOptions
                );
                NormalizeTopOuterRatios(state.Ratios);
            }
        }

        void CaptureBottomDockRatios(DockLayoutState &state) {
            const std::uint8_t layoutMask = state.LastLayoutMask;
            const bool showLog = (layoutMask & DOCK_PANEL_OUTPUT_LOG) != 0U;
            const bool showExplorer = (layoutMask & DOCK_PANEL_DEVICE_EXPLORER) != 0U;

            if (!showLog || !showExplorer) {
                return;
            }

            CaptureDockRatio(
                state.Nodes.OutputLog,
                state.Nodes.DeviceExplorer,
                ImGuiAxis_X,
                state.Ratios.BottomExplorer,
                true
            );
            NormalizeDockRatios(state.Ratios);
        }

        void CaptureDockLayoutRatios(DockLayoutState &state) {
            const std::uint8_t layoutMask = state.LastLayoutMask;
            const bool showTop = (layoutMask & (DOCK_PANEL_AVD_LIST | DOCK_PANEL_OPTIONS | DOCK_PANEL_DETAILS)) != 0U;
            const bool showBottom = (layoutMask & (DOCK_PANEL_OUTPUT_LOG | DOCK_PANEL_DEVICE_EXPLORER)) != 0U;

            if (showTop && showBottom) {
                CaptureDockRatio(
                    state.Nodes.TopGroup,
                    state.Nodes.BottomGroup,
                    ImGuiAxis_Y,
                    state.Ratios.BottomGroup,
                    true
                );
            }
            if (showTop) {
                CaptureTopDockRatios(state);
            }
            if (showBottom) {
                CaptureBottomDockRatios(state);
            }
            NormalizeDockRatios(state.Ratios);
        }

        float TopDetailsSplitRatio(const DockLayoutRatios &ratios) {
            return ClampDockRatio(ratios.TopDetails / (1.0F - ratios.TopOptions), 0.35F);
        }

        void BuildTopDockLayout(
            Context &context,
            const ImGuiID dockId,
            const DockLayoutRatios &ratios,
            DockLayoutNodeIds &nodes
        ) {
            // Top row order: Options | AVDs | Details.
            const bool showLeft = context.UI.ShowOptionsPanel;
            const bool showMiddle = context.UI.ShowAvdListPanel;
            const bool showRight = context.UI.ShowDetailsPanel;
            const float leftRatio = ClampDockRatio(ratios.TopOptions, 0.25F);
            const float rightFullRatio = ClampDockRatio(ratios.TopDetails, 0.2625F);
            const float rightAfterLeftRatio = TopDetailsSplitRatio(ratios);
            const float sideOnlyRatio = ClampDockRatio(ratios.TopSideOnlyOptions, 0.50F);

            if (!showLeft && !showMiddle && !showRight) {
                return;
            }

            nodes.TopGroup = dockId;

            if (showLeft && showMiddle && showRight) {
                ImGuiID leftId = 0;
                ImGuiID centerId = 0;
                ImGuiID rightId = 0;
                ImGuiID middleId = 0;
                ImGui::DockBuilderSplitNode(dockId, ImGuiDir_Left, leftRatio, &leftId, &centerId);
                ImGui::DockBuilderSplitNode(centerId, ImGuiDir_Right, rightAfterLeftRatio, &rightId, &middleId);
                nodes.Options = leftId;
                nodes.Avds = middleId;
                nodes.Details = rightId;
                DockWindowIfVisible("Options", leftId);
                DockWindowIfVisible("AVDs", middleId);
                DockWindowIfVisible("Details", rightId);
                ConfigureDockNode(leftId);
                ConfigureDockNode(middleId);
                ConfigureDockNode(rightId);
                return;
            }

            if (showLeft && showMiddle && !showRight) {
                ImGuiID leftId = 0;
                ImGuiID middleId = 0;
                ImGui::DockBuilderSplitNode(dockId, ImGuiDir_Left, leftRatio, &leftId, &middleId);
                nodes.Options = leftId;
                nodes.Avds = middleId;
                DockWindowIfVisible("Options", leftId);
                DockWindowIfVisible("AVDs", middleId);
                ConfigureDockNode(leftId);
                ConfigureDockNode(middleId);
                return;
            }

            if (showLeft && !showMiddle && showRight) {
                ImGuiID leftId = 0;
                ImGuiID rightId = 0;
                ImGui::DockBuilderSplitNode(dockId, ImGuiDir_Left, sideOnlyRatio, &leftId, &rightId);
                nodes.Options = leftId;
                nodes.Details = rightId;
                DockWindowIfVisible("Options", leftId);
                DockWindowIfVisible("Details", rightId);
                ConfigureDockNode(leftId);
                ConfigureDockNode(rightId);
                return;
            }

            if (!showLeft && showMiddle && showRight) {
                ImGuiID rightId = 0;
                ImGuiID middleId = 0;
                ImGui::DockBuilderSplitNode(dockId, ImGuiDir_Right, rightFullRatio, &rightId, &middleId);
                nodes.Avds = middleId;
                nodes.Details = rightId;
                DockWindowIfVisible("AVDs", middleId);
                DockWindowIfVisible("Details", rightId);
                ConfigureDockNode(middleId);
                ConfigureDockNode(rightId);
                return;
            }

            if (showLeft && !showMiddle && !showRight) {
                nodes.Options = dockId;
                DockWindowIfVisible("Options", dockId);
            } else if (!showLeft && showMiddle && !showRight) {
                nodes.Avds = dockId;
                DockWindowIfVisible("AVDs", dockId);
            } else if (!showLeft && !showMiddle && showRight) {
                nodes.Details = dockId;
                DockWindowIfVisible("Details", dockId);
            }
            ConfigureDockNode(dockId);
        }

        void BuildBottomDockLayout(
            Context &context,
            const ImGuiID dockId,
            const DockLayoutRatios &ratios,
            DockLayoutNodeIds &nodes
        ) {
            const bool showLog = context.UI.ShowLogPanel;
            const bool showExplorer = context.UI.ShowDeviceExplorerPanel;

            if (!showLog && !showExplorer) {
                context.UI.OutputLogDockId = 0;
                context.UI.DeviceExplorerDockId = 0;
                return;
            }

            if (showLog && showExplorer) {
                ImGuiID explorerId = 0;
                ImGuiID logId = 0;
                ImGui::DockBuilderSplitNode(dockId, ImGuiDir_Right, ratios.BottomExplorer, &explorerId, &logId);
                nodes.BottomGroup = dockId;
                nodes.OutputLog = logId;
                nodes.DeviceExplorer = explorerId;
                context.UI.OutputLogDockId = logId;
                context.UI.DeviceExplorerDockId = explorerId;
                DockWindowIfVisible("Output Log", logId);
                DockWindowIfVisible("Device Explorer###DeviceExplorer", explorerId);
                ConfigureDockNode(logId);
                ConfigureDockNode(explorerId);
                return;
            }

            if (showLog) {
                nodes.BottomGroup = dockId;
                nodes.OutputLog = dockId;
                context.UI.OutputLogDockId = dockId;
                context.UI.DeviceExplorerDockId = 0;
                DockWindowIfVisible("Output Log", dockId);
                ConfigureDockNode(dockId);
                return;
            }

            nodes.BottomGroup = dockId;
            nodes.DeviceExplorer = dockId;
            context.UI.OutputLogDockId = 0;
            context.UI.DeviceExplorerDockId = dockId;
            DockWindowIfVisible("Device Explorer###DeviceExplorer", dockId);
            ConfigureDockNode(dockId);
        }

        void BuildDockLayout(Context &context, const ImGuiID dockSpaceId, const bool force = false) {
            DockLayoutState &layoutState = GetDockLayoutState();

            const std::uint8_t layoutMask = ResolveDockLayoutMask(context);
            if (!force && layoutState.BuiltOnce && layoutState.LastLayoutMask == layoutMask) {
                CaptureDockLayoutRatios(layoutState);
                SyncDockLayoutRatiosToContext(context);
                return;
            }

            if (!layoutState.BuiltOnce) {
                SeedDockLayoutStateFromContext(context);
            }
            if (layoutState.BuiltOnce) {
                CaptureDockLayoutRatios(layoutState);
            }
            layoutState.BuiltOnce = true;
            layoutState.LastLayoutMask = layoutMask;
            NormalizeDockRatios(layoutState.Ratios);

            ImGui::DockBuilderRemoveNode(dockSpaceId);
            ImGui::DockBuilderAddNode(dockSpaceId, ImGuiDockNodeFlags_DockSpace);
            ImGui::DockBuilderSetNodeSize(dockSpaceId, ImGui::GetMainViewport()->Size);

            context.UI.BottomDockId = 0;
            context.UI.OutputLogDockId = 0;
            context.UI.DeviceExplorerDockId = 0;

            const bool showTop = (layoutMask & (DOCK_PANEL_AVD_LIST | DOCK_PANEL_OPTIONS | DOCK_PANEL_DETAILS)) != 0U;
            const bool showBottom = (layoutMask & (DOCK_PANEL_OUTPUT_LOG | DOCK_PANEL_DEVICE_EXPLORER)) != 0U;

            ImGuiID topDockId = dockSpaceId;
            ImGuiID bottomDockId = dockSpaceId;
            DockLayoutNodeIds nodes;

            if (showTop && showBottom) {
                ImGuiID bottomId = 0;
                ImGuiID topId = 0;
                ImGui::DockBuilderSplitNode(dockSpaceId, ImGuiDir_Down, layoutState.Ratios.BottomGroup, &bottomId, &topId);
                topDockId = topId;
                bottomDockId = bottomId;
                context.UI.BottomDockId = bottomDockId;
                nodes.TopGroup = topDockId;
                nodes.BottomGroup = bottomDockId;
                ConfigureDockNode(topDockId);
                ConfigureDockNode(bottomDockId);
            } else if (showTop) {
                topDockId = dockSpaceId;
                nodes.TopGroup = topDockId;
                ConfigureDockNode(topDockId);
            } else if (showBottom) {
                bottomDockId = dockSpaceId;
                context.UI.BottomDockId = bottomDockId;
                nodes.BottomGroup = bottomDockId;
                ConfigureDockNode(bottomDockId);
            }

            if (showTop) {
                BuildTopDockLayout(context, topDockId, layoutState.Ratios, nodes);
            }
            if (showBottom) {
                if (context.UI.BottomDockId == 0) {
                    context.UI.BottomDockId = bottomDockId;
                }
                BuildBottomDockLayout(context, bottomDockId, layoutState.Ratios, nodes);
            }

            layoutState.Nodes = nodes;
            SyncDockLayoutRatiosToContext(context);
            ConfigureDockNode(dockSpaceId);
            ImGui::DockBuilderFinish(dockSpaceId);
        }

        float ResolveUiFontPixelSize(const Context &context, const float fontPixelScale) {
            const float scale = (fontPixelScale > 0.0F) ? fontPixelScale : 1.0F;
            return NormalizeUiFontSize(context.Prefs.UiFontSize) * scale;
        }

        template <typename T>
        void ConsumeFuture(std::future<T> &future) {
            if (!future.valid()) {
                return;
            }
            try {
                (void)future.get();
            } catch (...) {
            }
        }

        void RequestProgressCancel(const std::shared_ptr<SdkOperationProgress> &progress) {
            if (!progress) {
                return;
            }

            progress->CancelRequested.store(true);
            std::lock_guard lock(progress->Mutex);
            if (!progress->Finished) {
                progress->StatusText = "Cancelling...";
            }
            progress->DetailText.clear();
        }

        void ApplyImGuiFontSizeBase(const Context &context, const float fontPixelScale) {
            const float fontSize = ResolveUiFontPixelSize(context, fontPixelScale);
            auto &style = ImGui::GetStyle();
            // Since ImGui 1.92, AddFont* size does not by itself change the active UI font size.
            style.FontSizeBase = fontSize;
            style._NextFrameFontSizeBase = fontSize;
        }

    }

    Application::Application() : m_Context(DetectAndroidSdk()) {
        EnsureOptionsConfigDirectoryExists();
        ApplyAppSettingsToContext(m_Context, LoadAppSettings());

        if (!Paths::Onboarding::IsFirstRunComplete()) {
            m_Context.Flow.CurrentScreen = Screen::Onboarding;
        } else {
            m_Context.Flow.CurrentScreen = Screen::Main;
            RefreshAvds(m_Context);
        }
    }


    int Application::Run() {
        if (!m_InitPlatform()) {
            return 1;
        }
        if (!m_CreateMainWindow()) {
            return 1;
        }

        m_InitImGui();
        m_ApplyDpiScale();
        m_LoadFonts();

        ApplyCustomImGuiTheme(m_Context.Prefs.Theme, m_DpiScale);
        ApplyImGuiFontSizeBase(m_Context, m_FontPixelScale);

        const char *glslVersion = "#version 330";
        ImGui_ImplGlfw_InitForOpenGL(m_Window, true);
        ImGui_ImplOpenGL3_Init(glslVersion);
        m_ImGuiBackendsInitialized = true;
        m_SetupCallbacks();
        m_RunLoop();
        m_Shutdown();

        return 0;
    }

    Application::~Application() {
        m_Shutdown();
    }

    void Application::m_Build() {
        m_HandleNativeMenuActions();
        m_SyncNativeMenuState();

        if (m_Context.Flow.CurrentScreen == Screen::Onboarding) {
            BuildOnboardingWindow(m_Context);
            BuildQuitConfirmWindow(m_Context);
            return;
        }

#ifdef NDEBUG
        m_PollUpdateCheckIfNeeded();
#endif

        if (m_Context.Catalog.SelectedAvd != m_Context.Catalog.PreviousSelectedAvd) {
            if (m_Context.Catalog.SelectedAvd >= 0 && m_Context.Catalog.SelectedAvd < m_Context.Catalog.Avds.size()) {
                const std::string &avdName = m_Context.Catalog.Avds[m_Context.Catalog.SelectedAvd].Name;
                if (!m_Context.Catalog.PerAvdOptions.contains(avdName)) {
                    LoadAvdOptions(m_Context, avdName);
                }
            }
            m_Context.Catalog.PreviousSelectedAvd = m_Context.Catalog.SelectedAvd;
        }

        const ImGuiID dockSpaceId = ImGui::DockSpaceOverViewport(
            0,
            ImGui::GetMainViewport(),
            ImGuiDockNodeFlags_NoUndocking
        );

#if !defined(__APPLE__)
        BuildMainMenuBar(m_Context);
#endif

        BuildDockLayout(m_Context, dockSpaceId);

        BuildSdkMissingBanner(m_Context);
        BuildDeleteAvdWindow(m_Context);
        BuildAvdOptionsWindow(m_Context);
        BuildAvdListWindow(m_Context);
        BuildAvdInfoWindow(m_Context);
        BuildRenameAvdWindow(m_Context);
        BuildAvdLogsWindow(m_Context);
        BuildDeviceExplorerWindow(m_Context);
        BuildAboutWindow(m_Context);
        BuildPreferencesWindow(m_Context);
        BuildUpdateNoticeWindow(m_Context);
        BuildCreateAvdWindow(m_Context);
        if (!m_Context.UI.ShowCreateAvdDialog) {
            BuildInstallImageWindow(m_Context);
        }
        BuildStorageWindow(m_Context);
        BuildQuitConfirmWindow(m_Context);

        m_Context.Host.Manager.Update();
        DriveSharedFolderSync(m_Context);
    }


    bool Application::m_InitPlatform() {
        if (!glfwInit()) {
            ShowFatalError(COREDECK_TITLE, "Failed to initialize GLFW.");
            return false;
        }
        m_GlfwInitialized = true;

        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#if defined(__APPLE__)
        glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif

        glfwWindowHint(GLFW_SCALE_TO_MONITOR, GLFW_TRUE);
#ifdef GLFW_SCALE_FRAMEBUFFER
        glfwWindowHint(GLFW_SCALE_FRAMEBUFFER, GLFW_TRUE);
#endif
        glfwWindowHint(GLFW_COCOA_RETINA_FRAMEBUFFER, GLFW_TRUE);
        return true;
    }

    bool Application::m_CreateMainWindow() {
        m_Window = glfwCreateWindow(1200, 900, COREDECK_TITLE, nullptr, nullptr);
        if (!m_Window) {
            ShowFatalError(COREDECK_TITLE, "Failed to create window.\nYour system may not support OpenGL 3.3.");
            return false;
        }

        glfwMakeContextCurrent(m_Window);
        glfwSwapInterval(1);

#if defined(_WIN32)
        HWND hwnd = glfwGetWin32Window(m_Window);
        HICON icon = LoadIcon(GetModuleHandle(nullptr), MAKEINTRESOURCE(1));
        SendMessage(hwnd, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(icon));
        SendMessage(hwnd, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(icon));
#endif

        m_Context.UI.MainWindow = m_Window;
#if defined(__APPLE__)
        MacosMenu::Install();
        m_SyncNativeMenuState();
#endif
        return true;
    }

    void Application::m_InitImGui() {
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        m_ImGuiContextCreated = true;

        ImGuiIO &io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
        // Do NOT enable ConfigDpiScaleFonts. It instructs ImGui to overwrite
        // style.FontScaleDpi every time DisplayFramebufferScale changes, which
        // would compound with the per-platform scaling we apply explicitly in
        // m_ApplyDpiScale() + m_LoadFonts() and produce text that's roughly
        // square-of-scale too large (e.g. 2.25x at Windows 150%), causing text
        // to overflow buttons and sub-windows. We own font sizing ourselves.
        //
        // ConfigDpiScaleViewports stays on -- it only affects ImGui's
        // per-viewport rectangle math for multi-monitor docking, not fonts.
        io.ConfigDpiScaleViewports = true;

        static std::string imguiIniPath = Paths::GetAppConfigPath("imgui.ini");
        io.IniFilename = imguiIniPath.c_str();
    }

    void Application::m_LoadFonts() const {
        ImGuiIO &io = ImGui::GetIO();

        const std::string resourcesDir = Paths::GetResourcesDirectory();
        const std::vector<std::string> bundledFonts = FindBundledFontPaths();
        const std::string defaultTextFontPath = !bundledFonts.empty()
                                                    ? bundledFonts.front()
                                                    : Paths::JoinPaths({resourcesDir, "assets", "fonts", "PingFang SC Heavy.ttf"});
        const std::string fallbackTextFontPath = Paths::JoinPaths(
            {resourcesDir, "assets", "fonts", "JetBrainsMono-Regular.ttf"}
        );
        const std::string iconFontPath = Paths::JoinPaths(
            {resourcesDir, "assets", "fonts", "FontAwesome7Free-Solid-900.otf"}
        );

        const float textSize = ResolveUiFontPixelSize(m_Context, m_FontPixelScale);
        const float iconSize = textSize * 0.75F;
        const float glyphMinAdvance = textSize;

        auto isBundledLatinOnlyFont = [](const std::string &path) {
            const std::string fileName = std::filesystem::path(path).filename().string();
            return fileName == "JetBrainsMono-Regular.ttf" || fileName == "JetBrainsMono-Bold.ttf";
        };

        static ImVector<ImWchar> textGlyphRanges;
        ImFontGlyphRangesBuilder textGlyphBuilder;
        static constexpr ImWchar TEXT_RANGES[] = {
            0x0020,
            0x00FF,
            0x2000,
            0x206F,
            0,
        };
        textGlyphBuilder.AddRanges(TEXT_RANGES);
        textGlyphBuilder.AddRanges(io.Fonts->GetGlyphRangesChineseSimplifiedCommon());
        textGlyphBuilder.AddText(SimplifiedChineseGlyphText());
        textGlyphRanges.clear();
        textGlyphBuilder.BuildRanges(&textGlyphRanges);

        ImFont *textFont = nullptr;
        std::string loadedTextFontPath;
        const bool hasValidCustomFont = IsSupportedFontPath(m_Context.Prefs.CustomCjkFontPath);
        const std::string textFontPath = hasValidCustomFont
                                             ? m_Context.Prefs.CustomCjkFontPath
                                             : defaultTextFontPath;
        if (std::filesystem::exists(textFontPath)) {
            textFont = io.Fonts->AddFontFromFileTTF(textFontPath.c_str(), textSize, nullptr, textGlyphRanges.Data);
            if (textFont != nullptr) {
                loadedTextFontPath = textFontPath;
            }
        }

        if (textFont == nullptr && std::filesystem::exists(fallbackTextFontPath)) {
            textFont = io.Fonts->AddFontFromFileTTF(fallbackTextFontPath.c_str(), textSize, nullptr, TEXT_RANGES);
            if (textFont != nullptr) {
                loadedTextFontPath = fallbackTextFontPath;
            }
        }

        const std::string automaticCjkFontPath = FindSystemCjkFontPath();
        const std::string cjkFontPath =
            (textFont == nullptr || isBundledLatinOnlyFont(loadedTextFontPath)) ? automaticCjkFontPath : "";

        if (IsSupportedFontPath(cjkFontPath)) {
            static ImVector<ImWchar> cjkGlyphRanges;
            ImFontGlyphRangesBuilder glyphBuilder;
            glyphBuilder.AddRanges(io.Fonts->GetGlyphRangesChineseSimplifiedCommon());
            glyphBuilder.AddText(SimplifiedChineseGlyphText());
            cjkGlyphRanges.clear();
            glyphBuilder.BuildRanges(&cjkGlyphRanges);

            ImFontConfig cjkConfig;
            cjkConfig.MergeMode = textFont != nullptr;
            cjkConfig.PixelSnapH = true;
            io.Fonts->AddFontFromFileTTF(
                cjkFontPath.c_str(),
                textSize,
                &cjkConfig,
                cjkGlyphRanges.Data
            );
        }

        if (std::filesystem::exists(iconFontPath)) {
            ImFontConfig iconConfig;
            iconConfig.MergeMode = true;
            iconConfig.PixelSnapH = true;
            iconConfig.GlyphMinAdvanceX = glyphMinAdvance;

            static constexpr ImWchar ICON_RANGES[] = {0xf000, 0xf8ff, 0};
            io.Fonts->AddFontFromFileTTF(iconFontPath.c_str(), iconSize, &iconConfig, ICON_RANGES);
        }
    }

    void Application::m_RebuildFonts() {
        ImGuiIO &io = ImGui::GetIO();
        io.FontDefault = nullptr;
        io.Fonts->Clear();
        m_LoadFonts();
        ApplyImGuiFontSizeBase(m_Context, m_FontPixelScale);
    }

    void Application::m_HandleFontReloadRequest() {
        if (!m_Context.UI.FontReloadRequested) {
            return;
        }
        m_Context.UI.FontReloadRequested = false;
        m_RebuildFonts();
    }

    void Application::m_ApplyDpiScale() {
        float xscale = 1.0F;
        float yscale = 1.0F;
        if (m_Window) {
            glfwGetWindowContentScale(m_Window, &xscale, &yscale);
        }
        const float reportedScale = (xscale > 0.0F) ? xscale : 1.0F;

#if defined(__APPLE__)
        m_DpiScale = 1.0F;
        m_FontPixelScale = 1.0F;
#else
        m_DpiScale = reportedScale;
        m_FontPixelScale = reportedScale;
#endif
        ImGui::GetStyle().FontScaleDpi = 1.0F;
    }

    void Application::m_SetupCallbacks() {
        glfwSetWindowUserPointer(m_Window, this);

        glfwSetScrollCallback(m_Window, [](GLFWwindow *, const double x, const double y) {
            ImGuiIO &imGuiIO = ImGui::GetIO();
            imGuiIO.AddMouseWheelEvent(static_cast<float>(x) * 0.3F, static_cast<float>(y) * 0.3F);
        });

        glfwSetWindowCloseCallback(m_Window, [](GLFWwindow *w) {
            auto *self = static_cast<Application *>(glfwGetWindowUserPointer(w));
            if (self == nullptr) {
                return;
            }
            if (self->m_Context.UI.QuitConfirmed) {
                return;
            }

            RequestQuitConfirmation(self->m_Context);
            glfwSetWindowShouldClose(w, GLFW_FALSE);
        });

#if !defined(__APPLE__)
        glfwSetWindowContentScaleCallback(m_Window, [](GLFWwindow *w, const float xscale, const float /*yscale*/) {
            auto *self = static_cast<Application *>(glfwGetWindowUserPointer(w));
            if (self == nullptr || xscale <= 0.0F) {
                return;
            }
            if (std::abs(xscale - self->m_DpiScale) < 0.01F) {
                return;
            }

            self->m_DpiScale = xscale;
            self->m_FontPixelScale = xscale;
            ImGui::GetStyle().FontScaleDpi = 1.0F;

            self->m_RebuildFonts();

            ApplyCustomImGuiTheme(self->m_Context.Prefs.Theme, self->m_DpiScale);
        });
#endif

        glfwSetFramebufferSizeCallback(m_Window, [](GLFWwindow *w, const int width, const int height) {
            if (width == 0 || height == 0) {
                return;
            }

            auto *self = static_cast<Application *>(glfwGetWindowUserPointer(w));

            self->m_HandleFontReloadRequest();

            glViewport(0, 0, width, height);

            ImGui_ImplOpenGL3_NewFrame();
            ImGui_ImplGlfw_NewFrame();
            ImGui::NewFrame();

            self->m_Build();

            ImGui::Render();
            const ImVec4 clearColor = GetAppClearColor();
            glClearColor(clearColor.x, clearColor.y, clearColor.z, clearColor.w);
            glClear(GL_COLOR_BUFFER_BIT);
            ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
            glfwSwapBuffers(w);
        });
    }

    void Application::m_RunLoop() {
        while (!glfwWindowShouldClose(m_Window)) {
            const bool focused = glfwGetWindowAttrib(m_Window, GLFW_FOCUSED) != 0;
            const bool hovered = glfwGetWindowAttrib(m_Window, GLFW_HOVERED) != 0;
            const double timeout = focused && hovered ? 1.0 / 60.0 : 0.25;
            glfwWaitEventsTimeout(timeout);

            m_HandleFontReloadRequest();

            ImGui_ImplOpenGL3_NewFrame();
            ImGui_ImplGlfw_NewFrame();
            ImGui::NewFrame();

            m_Build();

            ImGui::Render();
            int displayW = 0;
            int displayH = 0;
            glfwGetFramebufferSize(m_Window, &displayW, &displayH);
            glViewport(0, 0, displayW, displayH);
            const ImVec4 clearColor = GetAppClearColor();
            glClearColor(clearColor.x, clearColor.y, clearColor.z, clearColor.w);
            glClear(GL_COLOR_BUFFER_BIT);
            ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
            glfwSwapBuffers(m_Window);
        }
    }

    void Application::m_DrainAsyncWork() {
        RequestProgressCancel(m_Context.SdkManagerWork.Progress);
        RequestProgressCancel(m_Context.JdkDownloadWork.Progress);

        ShutdownOnboardingSdkBootstrapWork();
        CancelDeviceExplorerWork(m_Context);

        ConsumeFuture(m_UpdateCheckFuture);
        ConsumeFuture(m_Context.AvdCreationWork.Prefetch.Future);
        ConsumeFuture(m_Context.AvdCreationWork.SystemImageRemoval.Future);
        ConsumeFuture(m_Context.ImageInstallationWork.Prefetch.Future);
        ConsumeFuture(m_Context.ImageInstallationWork.InstallFuture);
        ConsumeFuture(m_Context.ImageInstallationWork.LicenseCheckFuture);
        ConsumeFuture(m_Context.ImageInstallationWork.LicenseAcceptFuture);
        ConsumeFuture(m_Context.SdkManagerWork.List.Future);
        ConsumeFuture(m_Context.SdkManagerWork.OperationFuture);
        ConsumeFuture(m_Context.SdkManagerWork.LicenseCheckFuture);
        ConsumeFuture(m_Context.SdkManagerWork.LicenseAcceptFuture);
        ConsumeFuture(m_Context.SdkManagerWork.BootstrapFuture);
        ConsumeFuture(m_Context.JdkDownloadWork.List.Future);
        ConsumeFuture(m_Context.JdkDownloadWork.InstallFuture);
        ConsumeFuture(m_Context.Jobs.AvdCreation.Future);
        ConsumeFuture(m_Context.Jobs.AvdDeletion.Future);
        ConsumeFuture(m_Context.Jobs.AvdWipe.Future);
        ConsumeFuture(m_Context.DiskUsage.Future);
    }

    void Application::m_Shutdown() {
        if (m_ShutdownCompleted) {
            return;
        }
        m_ShutdownCompleted = true;

        m_DrainAsyncWork();
        PullRunningSharedFoldersBeforeShutdown(m_Context);
        PersistAppSettings(m_Context);

        if (m_ImGuiBackendsInitialized) {
            ImGui_ImplOpenGL3_Shutdown();
            ImGui_ImplGlfw_Shutdown();
            m_ImGuiBackendsInitialized = false;
        }
        if (m_ImGuiContextCreated) {
            ImGui::DestroyContext();
            m_ImGuiContextCreated = false;
        }
        if (m_Window) {
            glfwDestroyWindow(m_Window);
            m_Window = nullptr;
        }
#if defined(__APPLE__)
        MacosMenu::Shutdown();
#endif
        if (m_GlfwInitialized) {
            glfwTerminate();
            m_GlfwInitialized = false;
        }
    }

    void Application::m_PollUpdateCheckIfNeeded() {
        if (m_UpdateCheckFuture.valid()) {
            if (m_UpdateCheckFuture.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
                return;
            }

            std::optional<RemoteRelease> newer = m_UpdateCheckFuture.get();
            m_Context.Updates.UpdateCheckInFlight = false;

            if (newer) {
                m_Context.Updates.LatestVersion = std::move(newer->Version);
                m_Context.Updates.LatestNotes = std::move(newer->Notes);
                m_Context.Updates.ShowNewVersionModal = true;
            } else if (m_UpdateCheckWasManual) {
                m_Context.Updates.ShowUpToDateModal = true;
            }
            m_UpdateCheckWasManual = false;
            return;
        }

        bool start = false;
        if (!m_AutoUpdateCheckStarted) {
            m_AutoUpdateCheckStarted = true;
            m_UpdateCheckWasManual = false;
            start = true;
        } else if (m_Context.Updates.RequestManualUpdateCheck) {
            m_Context.Updates.RequestManualUpdateCheck = false;
            m_UpdateCheckWasManual = true;
            start = true;
        }

        if (start) {
            m_Context.Updates.UpdateCheckInFlight = true;
            m_UpdateCheckFuture = std::async(std::launch::async, []() -> std::optional<RemoteRelease> {
                try {
                    return QueryRemoteNewerVersion();
                } catch (...) {
                    return std::nullopt;
                }
            });
        }
    }

    void Application::m_HandleNativeMenuActions() {
#if defined(__APPLE__)
        while (const std::optional<NativeMenuAction> action = MacosMenu::PollAction()) {
            if (m_Context.Flow.CurrentScreen == Screen::Onboarding &&
                *action != NativeMenuAction::Quit) {
                continue;
            }

            switch (*action) {
                case NativeMenuAction::Preferences:
                    m_Context.UI.ShowPreferences = true;
                    break;
                case NativeMenuAction::Quit:
                    RequestQuitConfirmation(m_Context);
                    break;
                case NativeMenuAction::ToggleAvdList:
                    m_Context.UI.ShowAvdListPanel = !m_Context.UI.ShowAvdListPanel;
                    PersistAppSettings(m_Context);
                    break;
                case NativeMenuAction::ToggleOptions:
                    m_Context.UI.ShowOptionsPanel = !m_Context.UI.ShowOptionsPanel;
                    PersistAppSettings(m_Context);
                    break;
                case NativeMenuAction::ToggleDetails:
                    m_Context.UI.ShowDetailsPanel = !m_Context.UI.ShowDetailsPanel;
                    PersistAppSettings(m_Context);
                    break;
                case NativeMenuAction::ToggleOutputLog:
                    m_Context.UI.ShowLogPanel = !m_Context.UI.ShowLogPanel;
                    PersistAppSettings(m_Context);
                    break;
                case NativeMenuAction::ToggleDeviceExplorer:
                    m_Context.UI.ShowDeviceExplorerPanel = !m_Context.UI.ShowDeviceExplorerPanel;
                    if (m_Context.UI.ShowDeviceExplorerPanel) {
                        m_Context.DeviceExplorer.ActiveTabKey.clear();
                    }
                    PersistAppSettings(m_Context);
                    break;
                case NativeMenuAction::StorageOverview:
                    m_Context.UI.ShowStorageDialog = true;
                    break;
                case NativeMenuAction::OpenSharedFolderHost:
                    OpenSharedFolderOnHost(m_Context);
                    break;
                case NativeMenuAction::OpenSharedFolderEmulator:
                    OpenSharedFolderInEmulator(m_Context);
                    break;
                case NativeMenuAction::About:
                    m_Context.UI.ShowAboutDialog = true;
                    break;
                case NativeMenuAction::CheckForUpdates:
                    if (!m_Context.Updates.UpdateCheckInFlight) {
                        m_Context.Updates.RequestManualUpdateCheck = true;
                    }
                    break;
            }
        }
#endif
    }

    void Application::m_SyncNativeMenuState() const {
#if defined(__APPLE__)
        MacosMenu::Update({
            .Interactive = m_Context.Flow.CurrentScreen == Screen::Main,
            .ShowAvdListPanel = m_Context.UI.ShowAvdListPanel,
            .ShowOptionsPanel = m_Context.UI.ShowOptionsPanel,
            .ShowDetailsPanel = m_Context.UI.ShowDetailsPanel,
            .ShowLogPanel = m_Context.UI.ShowLogPanel,
            .ShowDeviceExplorerPanel = m_Context.UI.ShowDeviceExplorerPanel,
            .ShowToolsMenu = HasSelectedRunningAvd(m_Context),
            .UpdateCheckInFlight = m_Context.Updates.UpdateCheckInFlight,
        });
#endif
    }

    AppSettings CaptureAppSettingsFromContext(const Context &context) {
        AppSettings s;
        s.SchemaVersion = 1;
        s.AutoScroll = context.Logs.AutoScroll;
        s.ConfirmBeforeDeleteAvd = context.Prefs.ConfirmBeforeDeleteAvd;
        s.ConfirmBeforeWipeAndRun = context.Prefs.ConfirmBeforeWipeAndRun;
        s.CrashReportingEnabled = context.Prefs.CrashReportingEnabled;
        s.ThemeMode = static_cast<int>(context.Prefs.Theme);
        s.Language = static_cast<int>(context.Prefs.Language);
        s.CustomCjkFontPath = context.Prefs.CustomCjkFontPath;
        s.UiFontSize = NormalizeUiFontSize(context.Prefs.UiFontSize);
        s.JavaHomePath = context.Prefs.JavaHomePath;
        s.ShowAvdListPanel = context.UI.ShowAvdListPanel;
        s.ShowOptionsPanel = context.UI.ShowOptionsPanel;
        s.ShowDetailsPanel = context.UI.ShowDetailsPanel;
        s.ShowLogPanel = context.UI.ShowLogPanel;
        s.ShowDeviceExplorerPanel = context.UI.ShowDeviceExplorerPanel;
        s.DockBottomGroupRatio = context.UI.DockBottomGroupRatio;
        s.DockTopOptionsRatio = context.UI.DockTopOptionsRatio;
        s.DockTopDetailsRatio = context.UI.DockTopDetailsRatio;
        s.DockTopSideOnlyOptionsRatio = context.UI.DockTopSideOnlyOptionsRatio;
        s.DockBottomExplorerRatio = context.UI.DockBottomExplorerRatio;
        s.AvdSortMode = static_cast<int>(context.Catalog.SortMode);
        s.AvdSortAscending = context.Catalog.SortAscending;
        return s;
    }

    void ApplyAppSettingsToContext(Context &context, const AppSettings &settings) {
        context.Logs.AutoScroll = settings.AutoScroll;
        context.Prefs.ConfirmBeforeDeleteAvd = settings.ConfirmBeforeDeleteAvd;
        context.Prefs.ConfirmBeforeWipeAndRun = settings.ConfirmBeforeWipeAndRun;
        context.Prefs.CrashReportingEnabled = settings.CrashReportingEnabled;
        context.Prefs.Theme = settings.ThemeMode == static_cast<int>(ThemeMode::Light) ? ThemeMode::Light : ThemeMode::Dark;
        context.Prefs.Language = settings.Language == static_cast<int>(AppLanguage::SimplifiedChinese)
                                     ? AppLanguage::SimplifiedChinese
                                     : AppLanguage::English;
        SetCurrentLanguage(context.Prefs.Language);
        context.Prefs.CustomCjkFontPath = settings.CustomCjkFontPath;
        context.Prefs.UiFontSize = NormalizeUiFontSize(settings.UiFontSize);
        context.Prefs.JavaHomePath = settings.JavaHomePath;
        context.Host.Sdk.JavaHomePath = settings.JavaHomePath;
        context.Host.Manager.SetSdk(context.Host.Sdk);
        context.UI.ShowAvdListPanel = settings.ShowAvdListPanel;
        context.UI.ShowOptionsPanel = settings.ShowOptionsPanel;
        context.UI.ShowDetailsPanel = settings.ShowDetailsPanel;
        context.UI.ShowLogPanel = settings.ShowLogPanel;
        context.UI.ShowDeviceExplorerPanel = settings.ShowDeviceExplorerPanel;
        DockLayoutRatios dockRatios;
        dockRatios.BottomGroup = settings.DockBottomGroupRatio;
        dockRatios.TopOptions = settings.DockTopOptionsRatio;
        dockRatios.TopDetails = settings.DockTopDetailsRatio;
        dockRatios.TopSideOnlyOptions = settings.DockTopSideOnlyOptionsRatio;
        dockRatios.BottomExplorer = settings.DockBottomExplorerRatio;
        NormalizeDockRatios(dockRatios);
        context.UI.DockBottomGroupRatio = dockRatios.BottomGroup;
        context.UI.DockTopOptionsRatio = dockRatios.TopOptions;
        context.UI.DockTopDetailsRatio = dockRatios.TopDetails;
        context.UI.DockTopSideOnlyOptionsRatio = dockRatios.TopSideOnlyOptions;
        context.UI.DockBottomExplorerRatio = dockRatios.BottomExplorer;

        if (const int sortMode = settings.AvdSortMode; sortMode >= 0 && sortMode <= 2) {
            context.Catalog.SortMode = static_cast<AvdSortMode>(sortMode);
        }
        context.Catalog.SortAscending = settings.AvdSortAscending;
    }

    void PersistAppSettings(Context &context) {
        SyncDockLayoutRatiosToContext(context);
        SaveAppSettings(CaptureAppSettingsFromContext(context));
    }

    void RefreshAvds(Context &context) {
        context.Catalog.AvdNames = ListAvdNames(context.Host.Sdk);
        context.Catalog.Avds = LoadAvds(context.Catalog.AvdNames);

        for (const auto &avdName: context.Catalog.AvdNames) {
            LoadAvdOptions(context, avdName);
        }

        context.DiskUsage.PerAvdCache.clear();
        if (!context.DiskUsage.Loading.load()) {
            context.DiskUsage.LastScan = {};
            context.DiskUsage.Ready = false;
        }

        if (!context.Catalog.Avds.empty()) {
            context.Catalog.SelectedAvd = 0;
        } else {
            context.Catalog.SelectedAvd = -1;
        }
        context.Catalog.PreviousSelectedAvd = -1;
    }

    void LoadAvdOptions(Context &context, const std::string &avdName) {
        const std::string configPath = GetOptionsConfigPath(avdName);
        context.Catalog.PerAvdOptions[avdName] = LoadOptionsFromFile(configPath);
    }

    void SaveAvdOptions(Context &context, const std::string &avdName) {
        if (!context.Catalog.PerAvdOptions.contains(avdName)) {
            return;
        }

        const std::string configPath = GetOptionsConfigPath(avdName);
        SaveOptionsToFile(configPath, context.Catalog.PerAvdOptions[avdName]);
    }

    std::vector<EmulatorOption> &GetDefaultAvdOptions(Context &context) {
        if (context.Catalog.SelectedAvd >= 0 && context.Catalog.SelectedAvd < context.Catalog.Avds.size()) {
            const std::string &avdName = context.Catalog.Avds[context.Catalog.SelectedAvd].Name;

            if (!context.Catalog.PerAvdOptions.contains(avdName)) {
                LoadAvdOptions(context, avdName);
            }
            return context.Catalog.PerAvdOptions[avdName];
        }

        // Fallback
        static std::vector<EmulatorOption> defaultOptions = GetEmulatorOptions();
        return defaultOptions;
    }
}
