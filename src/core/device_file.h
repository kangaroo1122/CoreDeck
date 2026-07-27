#ifndef COREDECK_DEVICE_FILE_H
#define COREDECK_DEVICE_FILE_H

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "sdk.h"

namespace CoreDeck {
    enum class DeviceFileKind : uint8_t {
        File,
        Directory,
        Symlink,
        Other,
    };

    struct DeviceFileEntry {
        std::string Name;
        std::string Path;
        DeviceFileKind Kind = DeviceFileKind::Other;
        std::uintmax_t SizeBytes = 0;
        std::int64_t ModifiedEpochSeconds = 0;
        std::string Permissions;
    };

    struct DeviceFileListResult {
        bool Success = false;
        std::string Path;
        std::vector<DeviceFileEntry> Entries;
        std::string Error;
    };

    struct DeviceFileOperationResult {
        bool Success = false;
        std::string Output;
    };

    std::string JoinDevicePath(const std::string &parent, const std::string &name);

    std::string ParentDevicePath(const std::string &path);

    bool IsValidDevicePathSegment(const std::string &name);

    std::string NormalizeDevicePath(
        const std::string &path,
        const std::string &defaultDirectory = "/sdcard"
    );

    std::vector<DeviceFileEntry> ParseDeviceFileStatList(const std::string &output, const std::string &directory);

    DeviceFileListResult ListDeviceFiles(
        const SdkInfo &sdk,
        const std::string &serial,
        const std::string &path,
        const std::function<bool()> &shouldCancel = {}
    );

    DeviceFileOperationResult PullDevicePath(
        const SdkInfo &sdk,
        const std::string &serial,
        const std::string &remotePath,
        const std::string &localPath,
        const std::function<bool()> &shouldCancel = {}
    );

    DeviceFileOperationResult PushLocalPath(
        const SdkInfo &sdk,
        const std::string &serial,
        const std::string &localPath,
        const std::string &remoteDirectory,
        const std::function<bool()> &shouldCancel = {}
    );

    DeviceFileOperationResult DeleteDevicePath(
        const SdkInfo &sdk,
        const std::string &serial,
        const std::string &remotePath,
        const std::function<bool()> &shouldCancel = {}
    );

    DeviceFileOperationResult CreateDeviceDirectory(
        const SdkInfo &sdk,
        const std::string &serial,
        const std::string &remotePath,
        const std::function<bool()> &shouldCancel = {}
    );

    std::vector<std::string> BuildOpenDeviceDirectoryInEmulatorArgs(
        const std::string &serial,
        const std::string &remotePath
    );

    DeviceFileOperationResult OpenDeviceDirectoryInEmulator(
        const SdkInfo &sdk,
        const std::string &serial,
        const std::string &remotePath,
        const std::function<bool()> &shouldCancel = {}
    );
}

#endif // COREDECK_DEVICE_FILE_H
