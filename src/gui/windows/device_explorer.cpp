#include "device_explorer.h"

#include <algorithm>
#include <chrono>
#include <ctime>
#include <optional>

#include "imgui.h"

#include "../../core/file_dialog.h"
#include "../../core/shared_folder.h"
#include "../../core/utilities.h"
#include "../localization.h"
#include "../theme.h"
#include "../widgets.h"

namespace CoreDeck {
    namespace {
        constexpr ImGuiWindowFlags WINDOW_FLAGS =
            ImGuiWindowFlags_NoCollapse;

        struct RunningAvdTarget {
            std::string Name;
            std::string Serial;
        };

        std::optional<RunningAvdTarget> SelectedRunningAvd(const Context &context) {
            const int selected = context.Catalog.SelectedAvd;
            if (selected < 0 || selected >= static_cast<int>(context.Catalog.Avds.size())) {
                return std::nullopt;
            }

            const std::string &avdName = context.Catalog.Avds[selected].Name;
            if (!context.Host.Manager.IsRunning(avdName)) {
                return std::nullopt;
            }

            const int consolePort = context.Host.Manager.GetConsolePort(avdName);
            const std::string serial = EmulatorSerialForConsolePort(consolePort);
            if (serial.empty()) {
                return std::nullopt;
            }

            return RunningAvdTarget{.Name = avdName, .Serial = serial};
        }

        std::string SelectedSerial(const Context::DeviceExplorerTabState &work) {
            if (work.SelectedDevice < 0 || work.SelectedDevice >= static_cast<int>(work.Devices.size())) {
                return "";
            }
            return work.Devices[work.SelectedDevice].Serial;
        }

        bool CanRunFileOperation(const Context::DeviceExplorerTabState &work) {
            const std::string serial = SelectedSerial(work);
            if (serial.empty() || work.OperationBusy.load() || work.FileList.Loading.load()) {
                return false;
            }
            const auto &device = work.Devices[work.SelectedDevice];
            return device.IsOnline();
        }

        bool CanRefreshDevices(const Context::DeviceExplorerTabState &work) {
            return !work.DeviceList.Loading.load() &&
                   !work.FileList.Loading.load() &&
                   !work.OperationBusy.load();
        }

        std::shared_ptr<std::atomic<bool>> ResetCancelToken(Context::DeviceExplorerTabState &work) {
            work.CancelRequested = std::make_shared<std::atomic<bool>>(false);
            return work.CancelRequested;
        }

        bool IsCancelRequested(const std::shared_ptr<std::atomic<bool>> &cancel) {
            return cancel != nullptr && cancel->load();
        }

        std::string DeviceDisplayLabel(const AdbDevice &device) {
            std::string label = device.Serial;
            if (!device.AvdName.empty()) {
                label += " - ";
                label += device.AvdName;
            } else if (!device.Model.empty()) {
                label += " - ";
                label += device.Model;
            }
            if (!device.IsOnline()) {
                label += " (";
                label += device.State;
                label += ")";
            }
            return label;
        }

        std::string FileKindLabel(const DeviceFileKind kind) {
            switch (kind) {
                case DeviceFileKind::Directory:
                    return Tr("Directory");
                case DeviceFileKind::File:
                    return Tr("File");
                case DeviceFileKind::Symlink:
                    return Tr("Symlink");
                case DeviceFileKind::Other:
                default:
                    return Tr("Other");
            }
        }

        const char *FileKindIcon(const DeviceFileKind kind) {
            switch (kind) {
                case DeviceFileKind::Directory:
                    return Icons::FOLDER;
                case DeviceFileKind::Symlink:
                    return Icons::COPY;
                case DeviceFileKind::File:
                case DeviceFileKind::Other:
                default:
                    return Icons::FILE;
            }
        }

        std::string FormatModifiedTime(const std::int64_t epochSeconds) {
            if (epochSeconds <= 0) {
                return "";
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
                return "";
            }
            return buffer;
        }

        int FindPreferredDeviceIndex(const Context::DeviceExplorerTabState &work) {
            if (!work.PreferredSerial.empty()) {
                for (int i = 0; i < static_cast<int>(work.Devices.size()); i++) {
                    if (work.Devices[i].Serial == work.PreferredSerial) {
                        return i;
                    }
                }
            }

            if (!work.PreferredAvdName.empty()) {
                for (int i = 0; i < static_cast<int>(work.Devices.size()); i++) {
                    if (work.Devices[i].AvdName == work.PreferredAvdName) {
                        return i;
                    }
                }
            }

            for (int i = 0; i < static_cast<int>(work.Devices.size()); i++) {
                if (work.Devices[i].IsOnline()) {
                    return i;
                }
            }
            return work.Devices.empty() ? -1 : 0;
        }

        bool HasPreferredDeviceTarget(const Context::DeviceExplorerTabState &work) {
            return !work.PreferredSerial.empty() || !work.PreferredAvdName.empty();
        }

        bool MatchesPreferredDeviceTarget(
            const Context::DeviceExplorerTabState &work,
            const AdbDevice &device
        ) {
            if (!work.PreferredSerial.empty()) {
                return device.Serial == work.PreferredSerial;
            }
            if (!work.PreferredAvdName.empty()) {
                return device.AvdName == work.PreferredAvdName;
            }
            return true;
        }

        std::string SelectedAvdName(const Context &context) {
            const int selected = context.Catalog.SelectedAvd;
            if (selected >= 0 && selected < static_cast<int>(context.Catalog.Avds.size())) {
                return context.Catalog.Avds[selected].Name;
            }
            return "";
        }

        std::string DeviceExplorerTabKey(const std::string &serial, const std::string &avdName) {
            if (!avdName.empty()) {
                return "avd:" + avdName;
            }
            if (!serial.empty()) {
                return "serial:" + serial;
            }
            return "generic";
        }

        std::string DeviceExplorerTabTitle(const std::string &serial, const std::string &avdName) {
            if (!avdName.empty()) {
                return avdName;
            }
            if (!serial.empty()) {
                return serial;
            }
            return Tr("Device Explorer");
        }

        void CopyPathToBuffer(Context::DeviceExplorerTabState &work) {
            std::snprintf(work.PathBuffer, sizeof(work.PathBuffer), "%s", work.CurrentPath.c_str());
        }

        void StartDeviceRefresh(Context &context, Context::DeviceExplorerTabState &work) {
            if (work.DeviceList.Loading.load()) {
                return;
            }

            work.Error.clear();
            if (!HasAdb(context.Host.Sdk)) {
                work.Devices.clear();
                work.Entries.clear();
                work.SelectedDevice = -1;
                work.SelectedEntry = -1;
                work.DeviceListReady = true;
                work.FileListReady = false;
                work.Status.clear();
                work.Error = "ADB was not found. Install Android SDK Platform-Tools.";
                return;
            }

            work.Status = "Refreshing devices...";
            work.DeviceListReady = false;
            work.DeviceList.Loading = true;
            const SdkInfo sdk = context.Host.Sdk;
            const auto cancel = ResetCancelToken(work);
            work.DeviceList.Future = std::async(std::launch::async, [sdk, cancel] {
                return ListAdbDevices(sdk, [cancel] {
                    return IsCancelRequested(cancel);
                });
            });
        }

        void StartFileRefresh(Context &context, Context::DeviceExplorerTabState &work, const std::string &path) {
            const std::string serial = SelectedSerial(work);
            if (serial.empty() || work.FileList.Loading.load()) {
                return;
            }
            const std::string normalizedPath = NormalizeDevicePath(path);

            work.Error.clear();
            work.Status = "Loading files...";
            work.CurrentPath = normalizedPath;
            CopyPathToBuffer(work);
            work.FileListReady = false;
            work.SelectedEntry = -1;
            work.FileList.Loading = true;
            const SdkInfo sdk = context.Host.Sdk;
            const bool ensureDirectory = work.EnsureCurrentDirectoryBeforeRefresh;
            work.EnsureCurrentDirectoryBeforeRefresh = false;
            const auto cancel = ResetCancelToken(work);
            work.FileList.Future = std::async(std::launch::async, [sdk, serial, currentPath = work.CurrentPath, ensureDirectory, cancel] {
                if (ensureDirectory) {
                    const auto created = CreateDeviceDirectory(sdk, serial, currentPath, [cancel] {
                        return IsCancelRequested(cancel);
                    });
                    if (!created.Success) {
                        return DeviceFileListResult{
                            .Success = false,
                            .Path = currentPath,
                            .Entries = {},
                            .Error = created.Output.empty() ? "Could not create shared folder on device." : created.Output,
                        };
                    }
                }
                return ListDeviceFiles(sdk, serial, currentPath, [cancel] {
                    return IsCancelRequested(cancel);
                });
            });
        }

        void StartOperation(
            Context::DeviceExplorerTabState &work,
            std::string name,
            std::future<DeviceFileOperationResult> future,
            const bool refreshAfterOperation
        ) {
            if (work.OperationBusy.load()) {
                return;
            }
            work.Error.clear();
            work.Status = std::move(name);
            work.RefreshFilesAfterOperation = refreshAfterOperation;
            work.OperationBusy = true;
            work.OperationFuture = std::move(future);
        }

        void PollDeviceExplorerWork(Context &context, Context::DeviceExplorerTabState &work) {
            if (work.DeviceList.Loading.load() && work.DeviceList.Future.valid() &&
                work.DeviceList.Future.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
                work.Devices = work.DeviceList.Future.get();
                if (HasPreferredDeviceTarget(work)) {
                    std::erase_if(work.Devices, [&](const AdbDevice &device) {
                        return !MatchesPreferredDeviceTarget(work, device);
                    });
                }
                work.DeviceList.Loading = false;
                work.DeviceListReady = true;
                work.Status.clear();
                work.SelectedDevice = FindPreferredDeviceIndex(work);
                if (work.SelectedDevice >= 0 && work.Devices[work.SelectedDevice].IsOnline()) {
                    StartFileRefresh(context, work, work.CurrentPath.empty() ? "/sdcard" : work.CurrentPath);
                } else if (work.Devices.empty()) {
                    work.Error = "No connected devices found.";
                    work.Entries.clear();
                } else {
                    work.Error = "Current device is offline.";
                    work.Entries.clear();
                }
            }

            if (work.FileList.Loading.load() && work.FileList.Future.valid() &&
                work.FileList.Future.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
                const auto result = work.FileList.Future.get();
                work.FileList.Loading = false;
                work.FileListReady = result.Success;
                work.Status.clear();
                if (result.Success) {
                    work.CurrentPath = result.Path;
                    CopyPathToBuffer(work);
                    work.Entries = result.Entries;
                    work.Error.clear();
                } else {
                    work.Error = result.Error.empty() ? "Could not list device files." : result.Error;
                    work.Entries.clear();
                }
            }

            if (work.OperationBusy.load() && work.OperationFuture.valid() &&
                work.OperationFuture.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
                const auto result = work.OperationFuture.get();
                work.OperationBusy = false;
                if (result.Success) {
                    work.Status = "Operation completed.";
                    work.Error.clear();
                    if (work.RefreshFilesAfterOperation) {
                        StartFileRefresh(context, work, work.CurrentPath);
                    }
                } else {
                    work.Status.clear();
                    work.Error = result.Output.empty() ? "Operation failed." : result.Output;
                }
                work.RefreshFilesAfterOperation = false;
            }
        }

        void PollSharedFolderOpen(Context &context) {
            auto &work = context.DeviceExplorer;
            if (!work.OpenInEmulatorBusy.load() || !work.OpenInEmulatorFuture.valid()) {
                return;
            }
            if (work.OpenInEmulatorFuture.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
                return;
            }

            const auto result = work.OpenInEmulatorFuture.get();
            work.OpenInEmulatorBusy = false;
            if (result.Success) {
                work.Status = "Shared folder opened.";
                work.Error.clear();
            } else {
                work.Status.clear();
                work.Error = result.Output.empty() ? "Operation failed." : result.Output;
            }
        }

        void DrawDeviceHeader(Context &context, Context::DeviceExplorerTabState &work) {
            const bool loading = work.DeviceList.Loading.load();
            const bool canRefresh = CanRefreshDevices(work);
            const std::string refreshDevicesLabel = StrConcat(Icons::REFRESH, "##DeviceRefresh");
            if (PrimaryButton(refreshDevicesLabel.c_str(), canRefresh)) {
                StartDeviceRefresh(context, work);
            }
            HoverTooltip(Tr("Refresh devices"));

            ImGui::SameLine();
            const std::string label = work.SelectedDevice >= 0 && work.SelectedDevice < static_cast<int>(work.Devices.size())
                                          ? DeviceDisplayLabel(work.Devices[work.SelectedDevice])
                                          : work.Title;
            ImGui::TextDisabled("%s", label.empty() ? Tr("Device Explorer") : label.c_str());

            if (loading) {
                ImGui::SameLine();
                ImGui::TextDisabled("%s", Tr("Loading devices..."));
            }
        }

        void DrawPathToolbar(Context &context, Context::DeviceExplorerTabState &work) {
            const bool canOperate = CanRunFileOperation(work);
            const bool fileLoading = work.FileList.Loading.load();

            if (PrimaryButton(Icons::HOME, canOperate && !fileLoading)) {
                StartFileRefresh(context, work, "/sdcard");
            }
            HoverTooltip(Tr("Home"));
            ImGui::SameLine();
            if (PrimaryButton(Icons::ARROW_UP, canOperate && !fileLoading && work.CurrentPath != "/")) {
                StartFileRefresh(context, work, ParentDevicePath(work.CurrentPath));
            }
            HoverTooltip(Tr("Up"));
            ImGui::SameLine();
            const std::string refreshFilesLabel = StrConcat(Icons::REFRESH, "##FileRefresh");
            if (PrimaryButton(refreshFilesLabel.c_str(), canOperate && !fileLoading)) {
                StartFileRefresh(context, work, work.CurrentPath);
            }
            HoverTooltip(Tr("Refresh files"));

            ImGui::SameLine();
            const ImGuiStyle &style = ImGui::GetStyle();
            const float goWidth = ImGui::CalcTextSize(Tr("Go")).x + (style.FramePadding.x * 2.0F);
            const float pathWidth = std::max(Em(10.0F), ImGui::GetContentRegionAvail().x - goWidth - style.ItemSpacing.x);
            ImGui::SetNextItemWidth(pathWidth);
            if (ImGui::InputText("##DeviceExplorerPath", work.PathBuffer, sizeof(work.PathBuffer), ImGuiInputTextFlags_EnterReturnsTrue)) {
                StartFileRefresh(context, work, work.PathBuffer);
            }
            ImGui::SameLine();
            if (PrimaryButton("Go", canOperate && !fileLoading)) {
                StartFileRefresh(context, work, work.PathBuffer);
            }
        }

        void DrawFileTable(Context &context, Context::DeviceExplorerTabState &work) {
            const float tableHeight = std::max(Eh(8.0F), ImGui::GetContentRegionAvail().y);
            const float tableWidth = std::max(ImGui::GetContentRegionAvail().x, Em(70.0F));
            PickerTableStyle pts;
            const ImGuiTableFlags tableFlags = PICKER_TABLE_FLAGS | ImGuiTableFlags_ScrollX;
            if (!ImGui::BeginTable("DeviceExplorerFiles", 5, tableFlags, ImVec2(0, tableHeight), tableWidth)) {
                return;
            }

            ImGui::TableSetupColumn(Tr("Name"), ImGuiTableColumnFlags_WidthStretch, 4.0F);
            ImGui::TableSetupColumn(Tr("Type"), ImGuiTableColumnFlags_WidthFixed, Em(10.0F));
            ImGui::TableSetupColumn(Tr("Size"), ImGuiTableColumnFlags_WidthFixed, Em(10.0F));
            ImGui::TableSetupColumn(Tr("Modified"), ImGuiTableColumnFlags_WidthFixed, Em(16.0F));
            ImGui::TableSetupColumn(Tr("Permissions"), ImGuiTableColumnFlags_WidthFixed, Em(12.0F));
            ImGui::TableHeadersRow();

            for (int i = 0; i < static_cast<int>(work.Entries.size()); i++) {
                const auto &entry = work.Entries[i];
                const bool selected = work.SelectedEntry == i;
                ImGui::PushID(i);
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                const std::string nameLabel = StrConcat(FileKindIcon(entry.Kind), "  ", entry.Name);
                if (ImGui::Selectable(nameLabel.c_str(), selected, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick)) {
                    work.SelectedEntry = i;
                    if (entry.Kind == DeviceFileKind::Directory && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                        StartFileRefresh(context, work, entry.Path);
                    }
                }
                HoverTooltip(entry.Path);

                ImGui::TableSetColumnIndex(1);
                ImGui::Text("%s", FileKindLabel(entry.Kind).c_str());
                ImGui::TableSetColumnIndex(2);
                ImGui::Text("%s", entry.Kind == DeviceFileKind::Directory ? "-" : FormatFileSize(entry.SizeBytes).c_str());
                ImGui::TableSetColumnIndex(3);
                const std::string modified = FormatModifiedTime(entry.ModifiedEpochSeconds);
                ImGui::Text("%s", modified.c_str());
                ImGui::TableSetColumnIndex(4);
                ImGui::Text("%s", entry.Permissions.c_str());
                ImGui::PopID();
            }

            ImGui::EndTable();
        }

        void DrawOperationToolbar(Context &context, Context::DeviceExplorerTabState &work) {
            const bool canOperate = CanRunFileOperation(work);
            const bool hasSelection = work.SelectedEntry >= 0 && work.SelectedEntry < static_cast<int>(work.Entries.size());
            const std::string serial = SelectedSerial(work);
            const SdkInfo sdk = context.Host.Sdk;
            const float iconSize = ImGui::GetFrameHeight();
            const ImVec2 iconButtonSize(iconSize, iconSize);

            const std::string downloadLabel = StrConcat(Icons::DOWNLOAD, "##DeviceExplorerDownload");
            if (PrimaryButton(downloadLabel.c_str(), canOperate && hasSelection, iconButtonSize)) {
                const auto localFolder = FileDialog::PickFolder("Choose download folder");
                if (localFolder) {
                    const std::string remotePath = work.Entries[work.SelectedEntry].Path;
                    const auto cancel = ResetCancelToken(work);
                    StartOperation(
                        work,
                        "Downloading...",
                        std::async(std::launch::async, [sdk, serial, remotePath, localPath = *localFolder, cancel] {
                            return PullDevicePath(sdk, serial, remotePath, localPath, [cancel] {
                                return IsCancelRequested(cancel);
                            });
                        }),
                        false
                    );
                }
            }
            HoverTooltip(Tr("Download"));
            ImGui::SameLine();

            const std::string uploadFileLabel = StrConcat(Icons::UPLOAD, "##DeviceExplorerUploadFile");
            if (PrimaryButton(uploadFileLabel.c_str(), canOperate, iconButtonSize)) {
                const auto localFile = FileDialog::PickFile("Select file to upload", nullptr, 0, "");
                if (localFile) {
                    const std::string remoteDirectory = work.CurrentPath;
                    const auto cancel = ResetCancelToken(work);
                    StartOperation(
                        work,
                        "Uploading...",
                        std::async(std::launch::async, [sdk, serial, localPath = *localFile, remoteDirectory, cancel] {
                            return PushLocalPath(sdk, serial, localPath, remoteDirectory, [cancel] {
                                return IsCancelRequested(cancel);
                            });
                        }),
                        true
                    );
                }
            }
            HoverTooltip(Tr("Upload File"));
            ImGui::SameLine();

            const std::string uploadFolderLabel = StrConcat(Icons::FOLDER, "##DeviceExplorerUploadFolder");
            if (PrimaryButton(uploadFolderLabel.c_str(), canOperate, iconButtonSize)) {
                const auto localFolder = FileDialog::PickFolder("Select folder to upload");
                if (localFolder) {
                    const std::string remoteDirectory = work.CurrentPath;
                    const auto cancel = ResetCancelToken(work);
                    StartOperation(
                        work,
                        "Uploading...",
                        std::async(std::launch::async, [sdk, serial, localPath = *localFolder, remoteDirectory, cancel] {
                            return PushLocalPath(sdk, serial, localPath, remoteDirectory, [cancel] {
                                return IsCancelRequested(cancel);
                            });
                        }),
                        true
                    );
                }
            }
            HoverTooltip(Tr("Upload Folder"));
            ImGui::SameLine();

            const std::string deleteLabel = StrConcat(Icons::TRASH, "##DeviceExplorerDelete");
            if (NegativeButton(deleteLabel.c_str(), canOperate && hasSelection, iconButtonSize)) {
                work.PendingDelete = work.Entries[work.SelectedEntry];
                work.ConfirmDelete = true;
            }
            HoverTooltip(Tr("Delete"));

            ImGui::Spacing();
            const ImGuiStyle &style = ImGui::GetStyle();
            const float createButtonWidth = iconButtonSize.x;
            const float folderInputWidth = std::max(Em(8.0F), ImGui::GetContentRegionAvail().x - createButtonWidth - style.ItemSpacing.x);
            ImGui::SetNextItemWidth(folderInputWidth);
            ImGui::InputTextWithHint("##DeviceExplorerNewFolder", Tr("Folder name"), work.NewFolderBuffer, sizeof(work.NewFolderBuffer));
            ImGui::SameLine();
            const std::string createFolderLabel = StrConcat(Icons::FOLDER_PLUS, "##DeviceExplorerCreateFolder");
            if (PositiveButton(createFolderLabel.c_str(), canOperate && work.NewFolderBuffer[0] != '\0', iconButtonSize)) {
                if (!IsValidDevicePathSegment(work.NewFolderBuffer)) {
                    work.Error = "Invalid folder name.";
                    return;
                }
                const std::string remotePath = JoinDevicePath(work.CurrentPath, work.NewFolderBuffer);
                work.NewFolderBuffer[0] = '\0';
                const auto cancel = ResetCancelToken(work);
                StartOperation(
                    work,
                    "Creating folder...",
                    std::async(std::launch::async, [sdk, serial, remotePath, cancel] {
                        return CreateDeviceDirectory(sdk, serial, remotePath, [cancel] {
                            return IsCancelRequested(cancel);
                        });
                    }),
                    true
                );
            }
            HoverTooltip(Tr("Create Folder"));
        }

        void DrawDeleteDialog(Context &context, Context::DeviceExplorerTabState &work) {
            if (!work.ConfirmDelete) {
                return;
            }

            const bool busy = work.OperationBusy.load();
            const std::string title = StrConcat(Tr("Delete"), " \"", work.PendingDelete.Name, "\"?");
            const std::string message = StrConcat(
                Tr("This will permanently delete the selected file or folder from the Android device."),
                "\n\n",
                work.PendingDelete.Path,
                "\n\n",
                Tr("All device directories are supported, including system paths. Double-check before deleting. This cannot be undone.")
            );
            const std::string dialogId = StrConcat("DeviceExplorerDelete###DeviceExplorerDelete", work.Key);
            const DialogResult result = SimpleDialog(
                {.Id = dialogId.c_str(),
                 .IsOpen = work.ConfirmDelete,
                 .Title = title.c_str(),
                 .Message = message.c_str(),
                 .ConfirmButtonTitle = "Delete",
                 .CancelButtonTitle = "Cancel",
                 .BusyButtonTitle = "Deleting...",
                 .Type = DialogType::Negative,
                 .IsBusy = busy}
            );

            if (result == DialogResult::Confirmed) {
                const SdkInfo sdk = context.Host.Sdk;
                const std::string serial = SelectedSerial(work);
                const std::string remotePath = work.PendingDelete.Path;
                work.ConfirmDelete = false;
                const auto cancel = ResetCancelToken(work);
                StartOperation(
                    work,
                    "Deleting...",
                    std::async(std::launch::async, [sdk, serial, remotePath, cancel] {
                        return DeleteDevicePath(sdk, serial, remotePath, [cancel] {
                            return IsCancelRequested(cancel);
                        });
                    }),
                    true
                );
            }
        }
        void CancelTabWork(Context::DeviceExplorerTabState &work) {
            if (work.CancelRequested != nullptr) {
                work.CancelRequested->store(true);
            }
            if (work.DeviceList.Future.valid()) {
                work.DeviceList.Future.wait();
            }
            if (work.FileList.Future.valid()) {
                work.FileList.Future.wait();
            }
            if (work.OperationFuture.valid()) {
                work.OperationFuture.wait();
            }
            work.DeviceList.Loading = false;
            work.FileList.Loading = false;
            work.OperationBusy = false;
        }

        std::shared_ptr<Context::DeviceExplorerTabState> FindOrCreateTab(
            Context &context,
            std::string serial,
            std::string avdName
        ) {
            if (avdName.empty()) {
                avdName = SelectedAvdName(context);
            }
            if (serial.empty() && !avdName.empty()) {
                serial = EmulatorSerialForConsolePort(context.Host.Manager.GetConsolePort(avdName));
            }

            const std::string key = DeviceExplorerTabKey(serial, avdName);
            for (auto &tab: context.DeviceExplorer.Tabs) {
                if (tab != nullptr && tab->Key == key) {
                    if (!serial.empty()) {
                        tab->PreferredSerial = serial;
                    }
                    if (!avdName.empty()) {
                        tab->PreferredAvdName = avdName;
                        tab->Title = avdName;
                    }
                    return tab;
                }
            }

            auto tab = std::make_shared<Context::DeviceExplorerTabState>();
            tab->Key = key;
            tab->Title = DeviceExplorerTabTitle(serial, avdName);
            tab->PreferredSerial = serial;
            tab->PreferredAvdName = avdName;
            context.DeviceExplorer.Tabs.push_back(tab);
            return tab;
        }

        void PollAllDeviceExplorerWork(Context &context) {
            for (auto &tab: context.DeviceExplorer.Tabs) {
                if (tab == nullptr) {
                    continue;
                }
                PollDeviceExplorerWork(context, *tab);
                if (tab->RefreshDevicesRequested && CanRefreshDevices(*tab)) {
                    tab->RefreshDevicesRequested = false;
                    StartDeviceRefresh(context, *tab);
                }
            }
        }

        std::shared_ptr<Context::DeviceExplorerTabState> ResolveSelectedExplorerTab(Context &context) {
            const auto target = SelectedRunningAvd(context);
            if (!target) {
                return nullptr;
            }
            return FindOrCreateTab(context, target->Serial, target->Name);
        }

        void CancelAllTabs(Context &context) {
            for (auto &tab: context.DeviceExplorer.Tabs) {
                if (tab != nullptr) {
                    CancelTabWork(*tab);
                }
            }
            context.DeviceExplorer.Tabs.clear();
        }

        void DrawDeviceExplorerPanel(Context &context, Context::DeviceExplorerTabState *work) {
            if (work == nullptr) {
                if (!context.DeviceExplorer.Error.empty()) {
                    ImGui::TextColored(HexColor(Colors::NEGATIVE), "%s", Tr(context.DeviceExplorer.Error.c_str()));
                } else {
                    ImGui::TextDisabled("%s", Tr("Select a running AVD first."));
                }
                return;
            }

            if (work->RefreshDevicesRequested && CanRefreshDevices(*work)) {
                work->RefreshDevicesRequested = false;
                StartDeviceRefresh(context, *work);
            }

            DrawDeviceHeader(context, *work);
            ImGui::Spacing();
            DrawPathToolbar(context, *work);
            ImGui::Spacing();
            DrawOperationToolbar(context, *work);
            ImGui::Separator();

            if (!work->Error.empty()) {
                ImGui::TextColored(HexColor(Colors::NEGATIVE), "%s", Tr(work->Error.c_str()));
            } else if (!work->Status.empty()) {
                ImGui::TextDisabled("%s", Tr(work->Status.c_str()));
            }

            if (work->FileList.Loading.load()) {
                ImGui::TextDisabled("%s", Tr("Loading files..."));
            } else if (work->FileListReady && work->Entries.empty()) {
                ImGui::TextDisabled("%s", Tr("No files found."));
            }

            DrawFileTable(context, *work);
            DrawDeleteDialog(context, *work);
        }
    }

    bool HasSelectedRunningAvd(const Context &context) {
        return SelectedRunningAvd(context).has_value();
    }

    void OpenDeviceExplorer(
        Context &context,
        const std::string &preferredSerial,
        const std::string &preferredAvdName,
        const std::string &preferredPath
    ) {
        std::string serial = preferredSerial;
        std::string avdName = preferredAvdName;
        if (serial.empty() && avdName.empty()) {
            const auto target = SelectedRunningAvd(context);
            if (!target) {
                context.DeviceExplorer.Status.clear();
                context.DeviceExplorer.Error = "Select a running AVD first.";
                return;
            }
            serial = target->Serial;
            avdName = target->Name;
        }

        auto tab = FindOrCreateTab(context, serial, avdName);
        context.DeviceExplorer.Status.clear();
        context.DeviceExplorer.Error.clear();
        const bool wasVisible = context.UI.ShowDeviceExplorerPanel;
        context.UI.ShowDeviceExplorerPanel = true;
        if (!context.DeviceExplorer.Open || !wasVisible) {
            context.DeviceExplorer.DockRequested = true;
        }
        context.DeviceExplorer.Open = true;
        context.DeviceExplorer.FocusRequested = true;
        if (!serial.empty()) {
            tab->PreferredSerial = serial;
        }
        if (!avdName.empty()) {
            tab->PreferredAvdName = avdName;
            tab->Title = avdName;
        }
        if (!preferredPath.empty()) {
            tab->CurrentPath = NormalizeDevicePath(preferredPath);
            CopyPathToBuffer(*tab);
        }
        tab->RefreshDevicesRequested = true;
    }

    void OpenSharedFolderInEmulator(Context &context) {
        if (context.DeviceExplorer.OpenInEmulatorBusy.load()) {
            context.DeviceExplorer.Status = "Opening shared folder in emulator...";
            return;
        }

        const auto target = SelectedRunningAvd(context);
        if (!target) {
            context.DeviceExplorer.Status.clear();
            context.DeviceExplorer.Error = "Select a running AVD first.";
            return;
        }

        context.DeviceExplorer.Status = "Opening shared folder in emulator...";
        context.DeviceExplorer.Error.clear();
        context.DeviceExplorer.OpenInEmulatorBusy = true;
        const SdkInfo sdk = context.Host.Sdk;
        const std::string serial = target->Serial;
        const std::string devicePath = GetSharedFolderDevicePath();
        context.DeviceExplorer.OpenInEmulatorFuture = std::async(std::launch::async, [sdk, serial, devicePath] {
            return OpenDeviceDirectoryInEmulator(sdk, serial, devicePath);
        });
    }

    void OpenSharedFolderOnHost(Context &context) {
        const auto target = SelectedRunningAvd(context);
        if (!target) {
            context.DeviceExplorer.Status.clear();
            context.DeviceExplorer.Error = "Select a running AVD first.";
            return;
        }

        std::string error;
        if (OpenSharedFolderHostPath(target->Name, &error)) {
            context.DeviceExplorer.Status = "Shared folder opened.";
            context.DeviceExplorer.Error.clear();
            return;
        }

        context.DeviceExplorer.Status.clear();
        context.DeviceExplorer.Error = error.empty() ? "Could not open shared folder." : error;
    }

    void CancelDeviceExplorerWork(Context &context) {
        CancelAllTabs(context);
        if (context.DeviceExplorer.OpenInEmulatorFuture.valid()) {
            context.DeviceExplorer.OpenInEmulatorFuture.wait();
        }
        context.DeviceExplorer.OpenInEmulatorBusy = false;
    }

    void PollDeviceExplorer(Context &context) {
        PollSharedFolderOpen(context);
        PollAllDeviceExplorerWork(context);
    }

    void BuildDeviceExplorerWindow(Context &context) {
        PollDeviceExplorer(context);
        if (!context.UI.ShowDeviceExplorerPanel) {
            return;
        }

        const ImGuiID dockId = context.UI.DeviceExplorerDockId != 0
                                  ? context.UI.DeviceExplorerDockId
                                  : context.UI.BottomDockId;
        if (dockId != 0 && context.DeviceExplorer.DockRequested) {
            ImGui::SetNextWindowDockID(dockId, ImGuiCond_Always);
            context.DeviceExplorer.DockRequested = false;
        }
        if (context.DeviceExplorer.FocusRequested) {
            ImGui::SetNextWindowFocus();
            context.DeviceExplorer.FocusRequested = false;
        }

        const std::string title = TrLabel("Device Explorer###DeviceExplorer");
        if (!ImGui::Begin(title.c_str(), nullptr, WINDOW_FLAGS)) {
            ImGui::End();
            return;
        }
        context.DeviceExplorer.Open = true;

        if (ImGui::GetWindowDockID() != 0) {
            context.UI.DeviceExplorerDockId = ImGui::GetWindowDockID();
            if (context.UI.BottomDockId == 0) {
                context.UI.BottomDockId = context.UI.DeviceExplorerDockId;
            }
        }

        auto tab = ResolveSelectedExplorerTab(context);
        DrawDeviceExplorerPanel(context, tab.get());

        ImGui::End();
    }
}
