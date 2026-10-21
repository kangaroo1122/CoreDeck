#pragma once

#include <future>
#include "../core/log.h"
#include "../core/sdk_packages.h"

namespace CoreDeck {
    template <typename T>
    void ConsumeFuture(std::future<T> &future) {
        if (!future.valid()) {
            return;
        }
        try {
            (void)future.get();
        } catch (const std::exception &e) {
            Log::Error("Background task failed during shutdown: ", e.what());
        } catch (...) {
            Log::Error("Background task failed during shutdown with an unknown exception.");
        }
    }

    inline void RequestProgressCancel(const std::shared_ptr<SdkOperationProgress> &progress) {
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

}
