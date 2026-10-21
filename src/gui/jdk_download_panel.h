//
// Created by kangaroo. on 26/07/2026.
//

#ifndef COREDECK_JDK_DOWNLOAD_PANEL_H
#define COREDECK_JDK_DOWNLOAD_PANEL_H

#include <cstddef>

#include "../core/jdk.h"
#include "context.h"

namespace CoreDeck {
    // Call after cancellation has been requested for all modules.
    void ShutdownJdkDownloadWork(Context &context);

    bool IsJdkDownloadBusy(const Context &context);

    bool ShouldOfferManagedJdkDownload(const JavaHomeStatus &versionState);

    void DrawJdkDownloadPanel(
        Context &context,
        char *javaHomeBuffer,
        std::size_t javaHomeBufferSize,
        JavaHomeStatus &versionState,
        float width,
        bool enabled = true
    );
}

#endif // COREDECK_JDK_DOWNLOAD_PANEL_H
