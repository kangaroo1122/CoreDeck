//
// Created by CoreDeck contributors on 02/08/2026.
//

#ifndef COREDECK_ARCHIVE_H
#define COREDECK_ARCHIVE_H

#include <functional>
#include <string>

namespace CoreDeck {
    struct ExtractOptions {
        bool StripTopLevelDir = false;
    };

    using ExtractProgressFn = std::function<bool(float)>;

    bool ExtractZip(
        const std::string &zipPath,
        const std::string &destDir,
        const ExtractOptions &options,
        const ExtractProgressFn &onProgress,
        std::string &error
    );
}

#endif // COREDECK_ARCHIVE_H
