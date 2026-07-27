#include "shared_folder.h"

#include <cctype>
#include <fstream>
#include <filesystem>
#include <set>
#include <system_error>
#include <utility>

#include "adb.h"
#include "device_file.h"
#include "paths.h"
#include "process.h"

namespace CoreDeck {
    namespace {
        std::string TrimCopy(const std::string &value) {
            const auto start = value.find_first_not_of(" \t\r\n");
            if (start == std::string::npos) {
                return "";
            }
            const auto end = value.find_last_not_of(" \t\r\n");
            return value.substr(start, end - start + 1);
        }

        std::string SanitizeFolderName(const std::string &value) {
            std::string result;
            for (const unsigned char ch: TrimCopy(value)) {
                if (std::isalnum(ch) || ch == '-' || ch == '_' || ch == '.') {
                    result.push_back(static_cast<char>(ch));
                } else {
                    result.push_back('_');
                }
            }
            return result == "." || result == ".." ? "" : result;
        }

        SharedFolderSyncResult Failure(std::string message) {
            return {.Success = false, .Output = std::move(message)};
        }

        struct PullSharedFolderResult : SharedFolderSyncResult {
            std::filesystem::path StagingPath;
            std::string HostPath;
        };

        PullSharedFolderResult PullFailure(std::string message) {
            PullSharedFolderResult result;
            result.Success = false;
            result.Output = std::move(message);
            return result;
        }

        SharedFolderSyncResult RunSharedFolderAdbCommand(
            const SdkInfo &sdk,
            const std::vector<std::string> &args,
            const std::function<bool()> &shouldCancel
        ) {
            if (!HasAdb(sdk)) {
                return Failure("ADB was not found.");
            }

            std::string output;
            const bool ok = StreamCommandArgsWithEnvCancelable(
                sdk.AdbPath,
                args,
                "",
                BuildAndroidToolEnvironment(sdk),
                [&output](const std::string &line) {
                    output += line;
                    output.push_back('\n');
                },
                shouldCancel
            );
            return {.Success = ok, .Output = std::move(output)};
        }

        std::string BuildAvdStateFolderName(const std::string &avdName) {
            std::string folderName = SanitizeFolderName(avdName);
            if (folderName.empty()) {
                folderName = "default";
            }
            return folderName;
        }

        std::filesystem::path BuildStagingPath(const std::string &avdName) {
            const std::string folderName = BuildAvdStateFolderName(avdName);
            return Paths::JoinPaths({Paths::GetAppConfigPath("shared_staging"), folderName});
        }

        std::filesystem::path BuildSnapshotPath(const std::string &avdName) {
            return Paths::JoinPaths({Paths::GetAppConfigPath("shared_state"), BuildAvdStateFolderName(avdName) + ".txt"});
        }

        bool IsSafeSnapshotRelativePath(const std::string &relativePath) {
            if (relativePath.empty()) {
                return false;
            }

            const std::filesystem::path path(relativePath);
            if (path.is_absolute() || path.has_root_name()) {
                return false;
            }

            for (const auto &part: path) {
                const std::string segment = part.string();
                if (segment.empty() || segment == "." || segment == "..") {
                    return false;
                }
            }
            return true;
        }

        bool CollectRelativeFilePaths(
            const std::filesystem::path &rootPath,
            std::set<std::string> *files,
            std::string *error
        ) {
            if (files == nullptr) {
                return false;
            }
            files->clear();

            std::error_code ec;
            if (!std::filesystem::is_directory(rootPath, ec) || ec) {
                if (error != nullptr) {
                    *error = ec ? ec.message() : "Shared folder path is not a directory.";
                }
                return false;
            }

            std::filesystem::recursive_directory_iterator it(
                rootPath,
                std::filesystem::directory_options::skip_permission_denied,
                ec
            );
            if (ec) {
                if (error != nullptr) {
                    *error = ec.message();
                }
                return false;
            }

            const std::filesystem::recursive_directory_iterator end;
            while (it != end) {
                if (it->is_regular_file(ec)) {
                    if (ec) {
                        if (error != nullptr) {
                            *error = ec.message();
                        }
                        return false;
                    }

                    std::filesystem::path relative = std::filesystem::relative(it->path(), rootPath, ec);
                    if (ec) {
                        if (error != nullptr) {
                            *error = ec.message();
                        }
                        return false;
                    }
                    files->insert(relative.generic_string());
                } else if (ec) {
                    if (error != nullptr) {
                        *error = ec.message();
                    }
                    return false;
                }

                it.increment(ec);
                if (ec) {
                    if (error != nullptr) {
                        *error = ec.message();
                    }
                    return false;
                }
            }
            return true;
        }

        bool LoadSharedFolderSnapshot(
            const std::string &snapshotPath,
            std::set<std::string> *files,
            std::string *error
        ) {
            if (files == nullptr) {
                return false;
            }
            files->clear();

            std::error_code ec;
            if (snapshotPath.empty() || !std::filesystem::exists(snapshotPath, ec)) {
                return !ec;
            }
            if (ec) {
                if (error != nullptr) {
                    *error = ec.message();
                }
                return false;
            }

            std::ifstream file(snapshotPath);
            if (!file.is_open()) {
                if (error != nullptr) {
                    *error = "Could not read shared folder sync state.";
                }
                return false;
            }

            std::string line;
            while (std::getline(file, line)) {
                if (!line.empty() && line.back() == '\r') {
                    line.pop_back();
                }
                if (IsSafeSnapshotRelativePath(line)) {
                    files->insert(line);
                }
            }
            return true;
        }

        void PruneEmptyParents(const std::filesystem::path &hostRoot, std::filesystem::path parent) {
            std::error_code ec;
            while (parent != hostRoot && parent.has_relative_path()) {
                if (!std::filesystem::is_directory(parent, ec) || ec || !std::filesystem::is_empty(parent, ec) || ec) {
                    break;
                }
                std::filesystem::remove(parent, ec);
                if (ec) {
                    break;
                }
                parent = parent.parent_path();
            }
        }

        bool RemoveHostFilesDeletedOnDevice(
            const std::filesystem::path &hostRoot,
            const std::set<std::string> &previousFiles,
            const std::set<std::string> &currentDeviceFiles,
            std::string *error
        ) {
            std::error_code ec;
            for (const auto &relativePath: previousFiles) {
                if (currentDeviceFiles.contains(relativePath) || !IsSafeSnapshotRelativePath(relativePath)) {
                    continue;
                }

                const std::filesystem::path target = hostRoot / std::filesystem::path(relativePath);
                const auto status = std::filesystem::symlink_status(target, ec);
                if (ec) {
                    if (error != nullptr) {
                        *error = ec.message();
                    }
                    return false;
                }
                if (!std::filesystem::exists(status)) {
                    continue;
                }
                if (!std::filesystem::is_regular_file(status) && !std::filesystem::is_symlink(status)) {
                    continue;
                }

                std::filesystem::remove(target, ec);
                if (ec) {
                    if (error != nullptr) {
                        *error = ec.message();
                    }
                    return false;
                }
                PruneEmptyParents(hostRoot, target.parent_path());
            }
            return true;
        }

        bool SaveSharedFolderSnapshot(
            const std::string &snapshotPath,
            const std::string &rootPath,
            std::string *error
        ) {
            std::set<std::string> files;
            if (!CollectRelativeFilePaths(rootPath, &files, error)) {
                return false;
            }

            const std::filesystem::path path(snapshotPath);
            std::error_code ec;
            std::filesystem::create_directories(path.parent_path(), ec);
            if (ec) {
                if (error != nullptr) {
                    *error = ec.message();
                }
                return false;
            }

            const std::filesystem::path tempPath = path.string() + ".tmp";
            std::ofstream file(tempPath);
            if (!file.is_open()) {
                if (error != nullptr) {
                    *error = "Could not write shared folder sync state.";
                }
                return false;
            }
            for (const auto &relativePath: files) {
                file << relativePath << '\n';
            }
            file.close();
            if (!file.good()) {
                if (error != nullptr) {
                    *error = "Could not write shared folder sync state.";
                }
                return false;
            }

            std::filesystem::remove(path, ec);
            ec.clear();
            std::filesystem::rename(tempPath, path, ec);
            if (ec) {
                if (error != nullptr) {
                    *error = ec.message();
                }
                return false;
            }
            return true;
        }

        SharedFolderSyncResult PushSharedFolderToDevice(
            const SdkInfo &sdk,
            const std::string &serial,
            const std::string &avdName,
            const std::function<bool()> &shouldCancel
        ) {
            std::string error;
            if (!EnsureSharedFolderHostPath(avdName, &error)) {
                return Failure(error.empty() ? "Could not prepare shared folder." : error);
            }

            const std::string hostPath = GetSharedFolderHostPath(avdName);
            const std::string devicePath = GetSharedFolderDevicePath();
            const auto created = CreateDeviceDirectory(sdk, serial, devicePath, shouldCancel);
            if (!created.Success) {
                return Failure(created.Output.empty() ? "Could not create shared folder on device." : created.Output);
            }

            std::error_code ec;
            if (std::filesystem::is_empty(hostPath, ec) && !ec) {
                return {.Success = true, .Output = created.Output};
            }

            auto pushed = RunSharedFolderAdbCommand(
                sdk,
                BuildPushSharedFolderToDeviceArgs(serial, hostPath, devicePath),
                shouldCancel
            );
            if (!created.Output.empty()) {
                pushed.Output = created.Output + pushed.Output;
            }
            return pushed;
        }

        PullSharedFolderResult PullSharedFolderFromDevice(
            const SdkInfo &sdk,
            const std::string &serial,
            const std::string &avdName,
            const std::function<bool()> &shouldCancel
        ) {
            std::string error;
            if (!EnsureSharedFolderHostPath(avdName, &error)) {
                return PullFailure(error.empty() ? "Could not prepare shared folder." : error);
            }

            const std::string hostPath = GetSharedFolderHostPath(avdName);
            const std::string devicePath = GetSharedFolderDevicePath();
            const auto created = CreateDeviceDirectory(sdk, serial, devicePath, shouldCancel);
            if (!created.Success) {
                return PullFailure(created.Output.empty() ? "Could not create shared folder on device." : created.Output);
            }

            const std::filesystem::path stagingPath = BuildStagingPath(avdName);
            std::error_code ec;
            std::filesystem::remove_all(stagingPath, ec);
            ec.clear();
            std::filesystem::create_directories(stagingPath, ec);
            if (ec) {
                return PullFailure(ec.message());
            }

            auto pulled = RunSharedFolderAdbCommand(
                sdk,
                BuildPullSharedFolderFromDeviceArgs(serial, devicePath, stagingPath.string()),
                shouldCancel
            );
            PullSharedFolderResult result;
            result.Success = pulled.Success;
            result.Output = std::move(pulled.Output);
            result.StagingPath = stagingPath;
            result.HostPath = hostPath;
            if (!pulled.Success) {
                std::filesystem::remove_all(stagingPath, ec);
                return result;
            }

            if (!created.Output.empty()) {
                result.Output = created.Output + result.Output;
            }
            return result;
        }
    }

    std::string GetSharedFolderHostPath(const std::string &avdName) {
        const std::string rootPath = Paths::GetAppConfigPath("shared");
        const std::string folderName = SanitizeFolderName(avdName);
        if (rootPath.empty() || folderName.empty()) {
            return rootPath;
        }
        return Paths::JoinPaths({rootPath, folderName});
    }

    std::string GetSharedFolderDevicePath() {
        return "/sdcard/CoreDeckShared";
    }

    const char *GetOpenSharedFolderHostLabel() {
#if defined(_WIN32)
        return "Open Shared Folder in File Explorer";
#elif defined(__APPLE__)
        return "Open Shared Folder in Finder";
#else
        return "Open Shared Folder in File Manager";
#endif
    }

    OpenFolderCommand BuildOpenFolderCommand(const std::string &folderPath) {
#if defined(_WIN32)
        return {.Executable = "explorer.exe", .Args = {folderPath}};
#elif defined(__APPLE__)
        return {.Executable = "/usr/bin/open", .Args = {folderPath}};
#else
        return {.Executable = "xdg-open", .Args = {folderPath}};
#endif
    }

    std::string BuildSharedFolderContentPath(const std::string &folderPath) {
        return (std::filesystem::path(folderPath) / ".").string();
    }

    std::string BuildSharedFolderDeviceContentPath(const std::string &devicePath) {
        const std::string normalized = NormalizeDevicePath(devicePath);
        return normalized == "/" ? "/." : normalized + "/.";
    }

    std::vector<std::string> BuildPushSharedFolderToDeviceArgs(
        const std::string &serial,
        const std::string &hostPath,
        const std::string &devicePath
    ) {
        return {
            "-s",
            serial,
            "push",
            BuildSharedFolderContentPath(hostPath),
            NormalizeDevicePath(devicePath),
        };
    }

    std::vector<std::string> BuildPullSharedFolderFromDeviceArgs(
        const std::string &serial,
        const std::string &devicePath,
        const std::string &hostPath
    ) {
        return {
            "-s",
            serial,
            "pull",
            "-a",
            BuildSharedFolderDeviceContentPath(devicePath),
            hostPath,
        };
    }

    bool MergePulledSharedFolder(
        const std::string &stagingPath,
        const std::string &hostPath,
        std::string *error
    ) {
        if (error != nullptr) {
            error->clear();
        }

        const std::filesystem::path stagingRoot = stagingPath;
        const std::filesystem::path hostRoot = hostPath;

        std::error_code ec;
        if (!std::filesystem::is_directory(stagingRoot, ec) || ec) {
            if (error != nullptr) {
                *error = ec ? ec.message() : "Pulled shared folder staging path is not a directory.";
            }
            return false;
        }

        std::filesystem::create_directories(hostRoot, ec);
        if (ec) {
            if (error != nullptr) {
                *error = ec.message();
            }
            return false;
        }

        for (std::filesystem::recursive_directory_iterator it(
                 stagingRoot,
                 std::filesystem::directory_options::skip_permission_denied,
                 ec
             ),
             end;
             it != end;
             it.increment(ec)) {
            if (ec) {
                if (error != nullptr) {
                    *error = ec.message();
                }
                return false;
            }

            const std::filesystem::path source = it->path();
            std::filesystem::path relative = std::filesystem::relative(source, stagingRoot, ec);
            if (ec) {
                if (error != nullptr) {
                    *error = ec.message();
                }
                return false;
            }

            const std::filesystem::path target = hostRoot / relative;
            if (it->is_directory(ec)) {
                if (ec) {
                    if (error != nullptr) {
                        *error = ec.message();
                    }
                    return false;
                }
                if (std::filesystem::exists(target, ec) && !ec && !std::filesystem::is_directory(target, ec)) {
                    it.disable_recursion_pending();
                    continue;
                }
                if (ec) {
                    if (error != nullptr) {
                        *error = ec.message();
                    }
                    return false;
                }
                std::filesystem::create_directories(target, ec);
                if (ec) {
                    if (error != nullptr) {
                        *error = ec.message();
                    }
                    return false;
                }
                continue;
            }

            if (!it->is_regular_file(ec)) {
                if (ec) {
                    if (error != nullptr) {
                        *error = ec.message();
                    }
                    return false;
                }
                continue;
            }

            const std::filesystem::file_time_type sourceTime = std::filesystem::last_write_time(source, ec);
            if (ec) {
                if (error != nullptr) {
                    *error = ec.message();
                }
                return false;
            }

            bool shouldCopy = true;
            if (std::filesystem::exists(target, ec) && !ec) {
                if (!std::filesystem::is_regular_file(target, ec) || ec) {
                    if (ec) {
                        if (error != nullptr) {
                            *error = ec.message();
                        }
                        return false;
                    }
                    continue;
                }

                const std::filesystem::file_time_type targetTime = std::filesystem::last_write_time(target, ec);
                if (ec) {
                    if (error != nullptr) {
                        *error = ec.message();
                    }
                    return false;
                }
                shouldCopy = sourceTime > targetTime;
            } else if (ec) {
                if (error != nullptr) {
                    *error = ec.message();
                }
                return false;
            }

            if (!shouldCopy) {
                continue;
            }

            std::filesystem::create_directories(target.parent_path(), ec);
            if (ec) {
                if (error != nullptr) {
                    *error = ec.message();
                }
                return false;
            }
            std::filesystem::copy_file(source, target, std::filesystem::copy_options::overwrite_existing, ec);
            if (ec) {
                if (error != nullptr) {
                    *error = ec.message();
                }
                return false;
            }
            std::filesystem::last_write_time(target, sourceTime, ec);
            if (ec) {
                if (error != nullptr) {
                    *error = ec.message();
                }
                return false;
            }
        }

        return true;
    }

    bool ReconcilePulledSharedFolder(
        const std::string &stagingPath,
        const std::string &hostPath,
        const std::string &snapshotPath,
        std::string *error
    ) {
        if (error != nullptr) {
            error->clear();
        }

        const std::filesystem::path stagingRoot = stagingPath;
        const std::filesystem::path hostRoot = hostPath;

        std::error_code ec;
        std::filesystem::create_directories(hostRoot, ec);
        if (ec) {
            if (error != nullptr) {
                *error = ec.message();
            }
            return false;
        }

        std::set<std::string> previousFiles;
        std::set<std::string> currentDeviceFiles;
        if (!LoadSharedFolderSnapshot(snapshotPath, &previousFiles, error)) {
            return false;
        }
        if (!CollectRelativeFilePaths(stagingRoot, &currentDeviceFiles, error)) {
            return false;
        }
        if (!RemoveHostFilesDeletedOnDevice(hostRoot, previousFiles, currentDeviceFiles, error)) {
            return false;
        }

        return MergePulledSharedFolder(stagingPath, hostPath, error);
    }

    bool EnsureSharedFolderHostPath(const std::string &avdName, std::string *error) {
        if (error != nullptr) {
            error->clear();
        }

        const std::string folderPath = GetSharedFolderHostPath(avdName);
        if (folderPath.empty()) {
            if (error != nullptr) {
                *error = "Could not determine the shared folder location.";
            }
            return false;
        }

        std::error_code ec;
        std::filesystem::create_directories(folderPath, ec);
        if (ec) {
            if (error != nullptr) {
                *error = ec.message();
            }
            return false;
        }

        return std::filesystem::is_directory(folderPath, ec) && !ec;
    }

    bool EnsureSharedFolderHostPath(std::string *error) {
        return EnsureSharedFolderHostPath("", error);
    }

    bool OpenSharedFolderHostPath(const std::string &avdName, std::string *error) {
        if (!EnsureSharedFolderHostPath(avdName, error)) {
            return false;
        }

        const OpenFolderCommand command = BuildOpenFolderCommand(GetSharedFolderHostPath(avdName));
        return StreamCommandArgsWithEnvCancelable(command.Executable, command.Args, "", {}, nullptr, {});
    }

    bool OpenSharedFolderHostPath(std::string *error) {
        return OpenSharedFolderHostPath("", error);
    }

    SharedFolderSyncResult SyncSharedFolder(
        const SdkInfo &sdk,
        const std::string &serial,
        const std::string &avdName,
        const SharedFolderSyncMode mode,
        const std::function<bool()> &shouldCancel
    ) {
        if (serial.empty()) {
            return Failure("No device selected.");
        }

        auto pulled = PullSharedFolderFromDevice(sdk, serial, avdName, shouldCancel);
        if (!pulled.Success) {
            return pulled;
        }

        const std::string snapshotPath = BuildSnapshotPath(avdName).string();
        std::string error;
        auto cleanupStaging = [&] {
            std::error_code ec;
            std::filesystem::remove_all(pulled.StagingPath, ec);
        };

        if (!ReconcilePulledSharedFolder(pulled.StagingPath.string(), pulled.HostPath, snapshotPath, &error)) {
            cleanupStaging();
            return Failure(error.empty() ? "Could not sync shared folder." : error);
        }

        if (mode == SharedFolderSyncMode::DeviceToHost) {
            if (!SaveSharedFolderSnapshot(snapshotPath, pulled.StagingPath.string(), &error)) {
                cleanupStaging();
                return Failure(error.empty() ? "Could not update shared folder sync state." : error);
            }
            cleanupStaging();
            return pulled;
        }

        auto pushed = PushSharedFolderToDevice(sdk, serial, avdName, shouldCancel);
        if (!pushed.Success) {
            cleanupStaging();
            if (!pulled.Output.empty()) {
                pushed.Output = pulled.Output + pushed.Output;
            }
            return pushed;
        }
        if (!SaveSharedFolderSnapshot(snapshotPath, pulled.HostPath, &error)) {
            cleanupStaging();
            return Failure(error.empty() ? "Could not update shared folder sync state." : error);
        }
        cleanupStaging();
        if (!pulled.Output.empty()) {
            pushed.Output = pulled.Output + pushed.Output;
        }
        return pushed;
    }
}
