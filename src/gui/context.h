//
// Created by AbdulMuaz Aqeel on 18/04/2026.
//

#ifndef COREDECK_CONTEXT_H
#define COREDECK_CONTEXT_H

#include <atomic>
#include <chrono>
#include <cstdint>
#include <future>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "../core/adb.h"
#include "../core/avd.h"
#include "../core/device_file.h"
#include "../core/emulator.h"
#include "../core/jdk_download.h"
#include "../core/options.h"
#include "../core/sdk.h"
#include "../core/sdk_bootstrap.h"
#include "../core/sdk_packages.h"
#include "../core/shared_folder.h"
#include "../core/skin.h"
#include "../core/system_image.h"
#include "localization.h"
#include "theme.h"

struct GLFWwindow;

namespace CoreDeck {
    enum class Screen : uint8_t {
        Onboarding,
        Main,
    };

    enum class AvdSortMode : uint8_t {
        Name,
        ApiLevel,
        Device,
    };

    enum class ImageCategory : uint8_t {
        All,
        PhoneTablet,
        Wear,
        Tv,
        Automotive,
        Desktop,
        Xr,
        Other,
    };

    enum class ImageInstallFilter : uint8_t {
        All,
        Installed,
    };

    enum class DeviceCategory : uint8_t {
        All,
        Phone,
        Tablet,
        Wear,
        Tv,
        Automotive,
        Desktop,
        Other,
    };

    enum class SdkManagerTab : uint8_t {
        Platforms,
        Tools,
    };

    struct StorageScanResult {
        std::uintmax_t TotalAvdSize = 0;
        std::uintmax_t SystemImagesSize = 0;
    };

    struct AvdSnapshotListResult {
        std::vector<AvdSnapshotInfo> Snapshots;
        std::string Error;
    };

    struct AvdSnapshotDeleteResult {
        bool Succeeded = false;
        std::string Error;
    };

    struct AvdCreationPrefetchResult {
        std::vector<SystemImage> SystemImages;
        std::vector<DeviceProfile> DeviceProfiles;
        std::vector<Skin> Skins;
        std::string Error;
    };

    struct SystemImagePrefetchResult {
        std::vector<SystemImage> LocalImages;
        std::vector<RemoteSystemImage> RemoteImages;
        int SelectedImage = -1;
        std::string Error;
    };

    struct Context {
        struct Host {
            SdkInfo Sdk;
            EmulatorManager Manager;

            explicit Host(SdkInfo sdk) : Sdk(std::move(sdk)), Manager(Sdk) {
            }
        } Host;

        struct Flow {
            Screen CurrentScreen = Screen::Main;
        } Flow;

        struct Catalog {
            std::vector<std::string> AvdNames;
            std::vector<AvdInfo> Avds;
            int SelectedAvd = -1;
            int PreviousSelectedAvd = -1;
            std::unordered_map<std::string, std::vector<EmulatorOption>> PerAvdOptions;

            char SearchFilter[128] = {};
            AvdSortMode SortMode = AvdSortMode::Name;
            bool SortAscending = true;
            std::vector<int> FilteredIndices;
        } Catalog;

        struct LogViewState {
            std::string Search;
            int ActiveMatchIndex = 0;
            bool UseRegex = false;
        };

        struct Logs {
            std::unordered_map<std::string, LogViewState> PerAvdView;
            bool AutoScroll = true;
            bool PendingScroll = false;
            bool PendingFocus = false;
            int PendingSyncFrames = 0;
        } Logs;

        struct Prefs {
            bool ConfirmBeforeDeleteAvd = true;
            bool ConfirmBeforeWipeAndRun = true;
            bool CrashReportingEnabled = true;
            ThemeMode Theme = ThemeMode::Dark;
            AppLanguage Language = AppLanguage::English;
            std::string CustomCjkFontPath;
            float UiFontSize = 16.0F;
            std::string JavaHomePath;
        } Prefs;

        struct UI {
            bool ShowAboutDialog = false;
            bool ShowDeleteAvdDialog = false;
            bool ShowWipeAndRunDialog = false;
            bool ShowRenameAvdDialog = false;
            bool ShowAvdSnapshotsDialog = false;
            bool ShowCreateAvdDialog = false;
            bool ShowDeviceProfileDialog = false;
            bool ShowSkinDialog = false;
            bool ShowInstallImageDialog = false;
            bool ReopenCreateAvdOnInstallClose = false;
            bool ShowPreferences = false;
            bool ShowStorageDialog = false;
            bool ShowWipeDataDialog = false;
            bool ShowQuitDialog = false;
            bool QuitConfirmed = false;
            bool ShowAvdListPanel = true;
            bool ShowOptionsPanel = true;
            bool ShowDetailsPanel = true;
            bool ShowLogPanel = true;
            bool ShowDeviceExplorerPanel = false;
            int WindowWidth = 1200;
            int WindowHeight = 900;
            bool WindowMaximized = false;
            float DockBottomGroupRatio = 1.0F / 3.0F;
            float DockTopOptionsRatio = 0.25F;
            float DockTopDetailsRatio = 0.2625F;
            float DockTopSideOnlyOptionsRatio = 0.50F;
            float DockBottomExplorerRatio = 1.0F / 3.0F;
            std::uint32_t BottomDockId = 0;
            std::uint32_t OutputLogDockId = 0;
            std::uint32_t DeviceExplorerDockId = 0;
            GLFWwindow *MainWindow = nullptr;
            bool HideInvalidSdkPathBanner = false;
            bool FontReloadRequested = false;
        } UI;

        struct AvdRenameWork {
            std::string TargetName;
            std::string TargetPath;
            char DisplayNameBuffer[128] = {};
            std::string Error;
        } AvdRenameWork;

        struct AvdSnapshotWork {
            std::string TargetName;
            std::string TargetDisplayName;
            std::string TargetPath;
            std::vector<AvdSnapshotInfo> Snapshots;
            int SelectedSnapshot = -1;
            std::string PendingDeleteName;
            std::string Error;
            std::string Status;
            bool Ready = false;

            std::atomic<bool> Loading{false};
            std::future<AvdSnapshotListResult> ListFuture;
            std::atomic<bool> Deleting{false};
            std::future<AvdSnapshotDeleteResult> DeleteFuture;
        } AvdSnapshotWork;

        struct AvdCreationWork {
            std::vector<SystemImage> SystemImages;
            std::vector<DeviceProfile> DeviceProfiles;
            std::vector<Skin> Skins;
            AvdCreationData CreationData;
            int SelectedSystemImage = 0;
            int SelectedDevice = 0;
            int PendingSelectedDevice = 0;
            int SelectedSkin = 0; // 0 = "No skin"
            int PendingSelectedSkin = 0;
            char SkinSearchFilter[128] = {};
            DeviceCategory SelectedDeviceCategory = DeviceCategory::Phone;
            char DeviceSearchFilter[128] = {};
            int SelectedGpuMode = 0;
            bool NameAutoFilled = true;
            bool DisplayNameAutoFilled = true;
            bool SkinAutoFilled = true;
            int LastDeviceForSkinAuto = -1;
            std::string Error;

            struct {
                std::atomic<bool> Loading{false};
                std::atomic<bool> Ready{false};
                std::future<AvdCreationPrefetchResult> Future;
            } Prefetch;

            struct {
                std::atomic<bool> Busy{false};
                std::future<bool> Future;
            } SystemImageRemoval;
        } AvdCreationWork;

        struct ImageInstallationWork {
            std::vector<RemoteSystemImage> RemoteImages;
            int SelectedImage = -1;
            ImageCategory SelectedCategory = ImageCategory::PhoneTablet;
            ImageInstallFilter InstallFilter = ImageInstallFilter::Installed;
            char SearchFilter[128] = {};

            struct {
                std::atomic<bool> Loading{false};
                std::atomic<bool> Ready{false};
                std::future<SystemImagePrefetchResult> Future;
            } Prefetch;

            std::atomic<bool> Installing{false};
            std::shared_ptr<InstallProgressData> Progress;
            std::future<bool> InstallFuture;
            bool AwaitingLicenseConsent = false;
            std::atomic<bool> LicenseBusy{false};
            std::future<LicenseStatus> LicenseCheckFuture;
            std::future<bool> LicenseAcceptFuture;
            std::string PendingPackagePath;
            std::string LicenseError;
            std::string Error;
        } ImageInstallationWork;

        struct SdkManagerWork {
            std::vector<SdkPackage> Packages;
            SdkPackageListResult LastListResult;
            SdkManagerTab ActiveTab = SdkManagerTab::Platforms;
            std::string SelectedPackageRowId;
            char SearchFilter[128] = {};
            bool HideObsoletePackages = true;
            bool ShowPackageDetails = false;
            bool AwaitingLicenseConsent = false;
            std::vector<std::string> PendingPackagePaths;
            SdkInfo PendingSdk;
            std::string Error;

            struct {
                std::atomic<bool> Loading{false};
                std::atomic<bool> Ready{false};
                std::future<SdkPackageListResult> Future;
            } List;

            std::atomic<bool> OperationBusy{false};
            std::shared_ptr<SdkOperationProgress> Progress;
            std::future<bool> OperationFuture;
            std::atomic<bool> LicenseBusy{false};
            std::future<LicenseStatus> LicenseCheckFuture;
            std::future<bool> LicenseAcceptFuture;

            std::atomic<bool> BootstrapBusy{false};
            std::future<SdkBootstrapResult> BootstrapFuture;
            std::string BootstrapSdkRoot;
        } SdkManagerWork;

        struct JdkDownloadState {
            JdkVendor SelectedVendor = JdkVendor::EclipseTemurin;
            std::vector<JdkPackage> Packages;
            int SelectedPackage = -1;
            std::string Error;

            struct {
                std::atomic<bool> Loading{false};
                bool Ready = false;
                JdkVendor Vendor = JdkVendor::EclipseTemurin;
                std::future<JdkPackageListResult> Future;
            } List;

            std::atomic<bool> Installing{false};
            std::shared_ptr<SdkOperationProgress> Progress;
            std::future<JdkInstallResult> InstallFuture;
        } JdkDownloadWork;

        struct DeviceExplorerTabState {
            std::string Key;
            std::string Title;
            std::vector<AdbDevice> Devices;
            std::vector<DeviceFileEntry> Entries;
            int SelectedDevice = -1;
            int SelectedEntry = -1;
            std::string CurrentPath = "/sdcard";
            char PathBuffer[512] = "/sdcard";
            char NewFolderBuffer[128] = {};
            std::string PreferredSerial;
            std::string PreferredAvdName;
            std::string Error;
            std::string Status;
            bool DeviceListReady = false;
            bool FileListReady = false;
            bool RefreshDevicesRequested = true;
            bool RefreshFilesAfterOperation = false;
            bool EnsureCurrentDirectoryBeforeRefresh = false;
            bool WaitingForDeviceReady = false;
            bool ConfirmDelete = false;
            DeviceFileEntry PendingDelete;
            std::shared_ptr<std::atomic<bool>> CancelRequested = std::make_shared<std::atomic<bool>>(false);
            std::chrono::steady_clock::time_point LastDeviceRefreshAttempt{};

            struct {
                std::atomic<bool> Loading{false};
                std::future<std::vector<AdbDevice>> Future;
            } DeviceList;

            struct {
                std::atomic<bool> Loading{false};
                std::future<DeviceFileListResult> Future;
            } FileList;

            std::atomic<bool> OperationBusy{false};
            std::future<DeviceFileOperationResult> OperationFuture;
        };

        struct DeviceExplorerState {
            std::vector<std::shared_ptr<DeviceExplorerTabState>> Tabs;
            std::string ActiveTabKey;
            bool FocusRequested = false;
            std::string Error;
            std::string Status;
            std::atomic<bool> OpenInEmulatorBusy{false};
            std::future<DeviceFileOperationResult> OpenInEmulatorFuture;
        } DeviceExplorer;

        struct SharedFolderSyncJob {
            std::string AvdName;
            std::string Serial;
            bool Busy = false;
            bool InitialSynced = false;
            bool PendingStop = false;
            SharedFolderSyncMode Mode = SharedFolderSyncMode::Bidirectional;
            std::chrono::steady_clock::time_point LastAttempt{};
            std::future<SharedFolderSyncResult> Future;
        };

        struct SharedFolderSyncState {
            std::unordered_map<std::string, SharedFolderSyncJob> PerAvd;
            std::string Error;
            std::string Status;
        } SharedFolderSync;

        struct Jobs {
            struct {
                std::atomic<bool> Busy{false};
                std::future<bool> Future;
                std::string Error;
                std::string TargetName;
            } AvdCreation, AvdDeletion, AvdWipe;
        } Jobs;

        struct DiskUsage {
            std::unordered_map<std::string, std::uintmax_t> PerAvdCache;
            StorageScanResult LastScan;
            std::atomic<bool> Loading{false};
            bool Ready = false;
            std::future<StorageScanResult> Future;
        } DiskUsage;

        struct Updates {
            bool ShowNewVersionModal = false;
            std::string LatestVersion;
            std::string LatestNotes;
            bool ShowUpToDateModal = false;
            bool RequestManualUpdateCheck = false;
            bool UpdateCheckInFlight = false;
        } Updates;

        explicit Context(SdkInfo sdk) : Host(std::move(sdk)) {
        }
    };
}

#endif // COREDECK_CONTEXT_H
