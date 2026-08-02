#ifndef COREDECK_SDK_REPOSITORY_H
#define COREDECK_SDK_REPOSITORY_H

#include <cstdint>
#include <optional>
#include <string>

namespace CoreDeck {
    struct CommandLineToolsPackage {
        std::string DownloadUrl;
        std::string Sha1;
        std::string Sha256;
        std::uintmax_t SizeBytes = 0;
    };

    std::optional<CommandLineToolsPackage> ParseCommandLineToolsRepository(
        const std::string &xml,
        const std::string &hostOs,
        const std::string &hostArch = ""
    );
}

#endif // COREDECK_SDK_REPOSITORY_H
