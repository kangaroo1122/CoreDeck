#ifndef COREDECK_DEVICE_EXPLORER_H
#define COREDECK_DEVICE_EXPLORER_H

#include <string>

#include "../context.h"

namespace CoreDeck {
    bool HasSelectedRunningAvd(const Context &context);

    void OpenDeviceExplorer(
        Context &context,
        const std::string &preferredSerial = "",
        const std::string &preferredAvdName = "",
        const std::string &preferredPath = ""
    );

    void OpenSharedFolderInEmulator(Context &context);

    void OpenSharedFolderOnHost(Context &context);

    void CancelDeviceExplorerWork(Context &context);

    void BuildDeviceExplorerWindow(Context &context);
}

#endif // COREDECK_DEVICE_EXPLORER_H
