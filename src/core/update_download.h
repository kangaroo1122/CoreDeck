#ifndef COREDECK_UPDATE_DOWNLOAD_H
#define COREDECK_UPDATE_DOWNLOAD_H

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>

#include "version_check.h"

namespace CoreDeck {
    struct UpdateDownloadProgress {
        std::atomic<bool> CancelRequested{false};
        mutable std::mutex Mutex;
        std::uint64_t DownloadedBytes = 0;
        std::uint64_t TotalBytes = 0;
        std::string Status;
    };

    struct UpdateDownloadResult {
        bool Succeeded = false;
        bool Cancelled = false;
        std::string PackagePath;
        std::string Error;
    };

    UpdateDownloadResult DownloadAndVerifyUpdate(
        const ReleaseAsset &package,
        const ReleaseAsset &checksum,
        const std::shared_ptr<UpdateDownloadProgress> &progress
    );

    namespace detail { // NOLINT(readability-identifier-naming)
        std::optional<std::string> ParseSha256Checksum(
            const std::string &text,
            const std::string &expectedFilename
        );

        std::string Sha256File(const std::string &path);
    }
}

#endif // COREDECK_UPDATE_DOWNLOAD_H
