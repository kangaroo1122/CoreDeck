#include "shared_folder.h"

#include <cctype>
#include <filesystem>
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

        std::filesystem::path BuildStagingPath(const std::string &avdName) {
            std::string folderName = SanitizeFolderName(avdName);
            if (folderName.empty()) {
                folderName = "default";
            }
            return Paths::JoinPaths({Paths::GetAppConfigPath("shared_staging"), folderName});
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

        SharedFolderSyncResult PullSharedFolderFromDevice(
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

            const std::filesystem::path stagingPath = BuildStagingPath(avdName);
            std::error_code ec;
            std::filesystem::remove_all(stagingPath, ec);
            ec.clear();
            std::filesystem::create_directories(stagingPath, ec);
            if (ec) {
                return Failure(ec.message());
            }

            auto pulled = RunSharedFolderAdbCommand(
                sdk,
                BuildPullSharedFolderFromDeviceArgs(serial, devicePath, stagingPath.string()),
                shouldCancel
            );
            if (!pulled.Success) {
                std::filesystem::remove_all(stagingPath, ec);
                return pulled;
            }

            if (!MergePulledSharedFolder(stagingPath.string(), hostPath, &error)) {
                std::filesystem::remove_all(stagingPath, ec);
                return Failure(error.empty() ? "Could not sync shared folder." : error);
            }

            std::filesystem::remove_all(stagingPath, ec);
            if (!created.Output.empty()) {
                pulled.Output = created.Output + pulled.Output;
            }
            return pulled;
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
        if (!pulled.Success || mode == SharedFolderSyncMode::DeviceToHost) {
            return pulled;
        }

        auto pushed = PushSharedFolderToDevice(sdk, serial, avdName, shouldCancel);
        if (!pulled.Output.empty()) {
            pushed.Output = pulled.Output + pushed.Output;
        }
        return pushed;
    }
}
