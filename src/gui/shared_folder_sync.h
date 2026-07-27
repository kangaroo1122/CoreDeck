#ifndef COREDECK_SHARED_FOLDER_SYNC_H
#define COREDECK_SHARED_FOLDER_SYNC_H

#include <string>

#include "context.h"

namespace CoreDeck {
    bool IsSharedFolderStopPending(const Context &context, const std::string &avdName);

    void RequestSharedFolderSync(Context &context, const std::string &avdName, const std::string &serial);

    void RequestAvdStopWithSharedFolderSync(Context &context, const std::string &avdName);

    void DriveSharedFolderSync(Context &context);

    void PullRunningSharedFoldersBeforeShutdown(Context &context);
}

#endif // COREDECK_SHARED_FOLDER_SYNC_H
