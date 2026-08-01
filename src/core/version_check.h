//
// Created by AbdulMuaz Aqeel on 18/04/2026.
//

#ifndef COREDECK_VERSION_CHECK_H
#define COREDECK_VERSION_CHECK_H

#include <optional>
#include <string>
#include <vector>
#include <cstdint>

namespace CoreDeck {
    struct ReleaseAsset {
        std::string Name;
        std::string DownloadUrl;
        std::uint64_t Size = 0;
    };

    struct RemoteRelease {
        std::string Version;
        std::string Notes;
        bool IsPrerelease = false;
        std::vector<ReleaseAsset> Assets;
        std::optional<ReleaseAsset> Package;
        std::optional<ReleaseAsset> Checksum;
    };

    std::optional<RemoteRelease> QueryRemoteNewerVersion(bool includeBetaUpdates = false);

    namespace detail { // NOLINT(readability-identifier-naming)
        int CompareSemanticVersion(const std::string &newVersion, const std::string &currentVersion);

        std::optional<std::string> ParseLatestReleaseTag(const std::string &body);

        std::optional<RemoteRelease> ParseLatestRelease(const std::string &body);

        std::optional<ReleaseAsset> SelectReleaseAsset(
            const RemoteRelease &release,
            const std::string &platform,
            const std::string &architecture
        );

        std::optional<RemoteRelease> SelectNewestRelease(
            const std::string &body,
            bool includeBetaUpdates,
            const std::string &currentVersion
        );
    }
}

#endif // COREDECK_VERSION_CHECK_H
