#ifndef COREDECK_SHARED_FOLDER_H
#define COREDECK_SHARED_FOLDER_H

#include <functional>
#include <string>
#include <vector>

#include "sdk.h"

namespace CoreDeck {
    enum class SharedFolderSyncMode {
        Bidirectional,
        DeviceToHost,
    };

    struct OpenFolderCommand {
        std::string Executable;
        std::vector<std::string> Args;
    };

    struct SharedFolderSyncResult {
        bool Success = false;
        std::string Output;
    };

    std::string GetSharedFolderHostPath(const std::string &avdName = "");

    std::string GetSharedFolderDevicePath();

    const char *GetOpenSharedFolderHostLabel();

    OpenFolderCommand BuildOpenFolderCommand(const std::string &folderPath);

    std::string BuildSharedFolderContentPath(const std::string &folderPath);

    std::string BuildSharedFolderDeviceContentPath(const std::string &devicePath);

    std::vector<std::string> BuildPushSharedFolderToDeviceArgs(
        const std::string &serial,
        const std::string &hostPath,
        const std::string &devicePath
    );

    std::vector<std::string> BuildPullSharedFolderFromDeviceArgs(
        const std::string &serial,
        const std::string &devicePath,
        const std::string &hostPath
    );

    bool MergePulledSharedFolder(
        const std::string &stagingPath,
        const std::string &hostPath,
        std::string *error = nullptr
    );

    bool ReconcilePulledSharedFolder(
        const std::string &stagingPath,
        const std::string &hostPath,
        const std::string &snapshotPath,
        std::string *error = nullptr
    );

    bool EnsureSharedFolderHostPath(std::string *error = nullptr);

    bool EnsureSharedFolderHostPath(const std::string &avdName, std::string *error = nullptr);

    bool OpenSharedFolderHostPath(std::string *error = nullptr);

    bool OpenSharedFolderHostPath(const std::string &avdName, std::string *error = nullptr);

    SharedFolderSyncResult SyncSharedFolder(
        const SdkInfo &sdk,
        const std::string &serial,
        const std::string &avdName,
        SharedFolderSyncMode mode,
        const std::function<bool()> &shouldCancel = {}
    );
}

#endif // COREDECK_SHARED_FOLDER_H
