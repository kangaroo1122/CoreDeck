#ifndef COREDECK_FONTS_H
#define COREDECK_FONTS_H

#include <string>
#include <vector>

namespace CoreDeck {
    std::vector<std::string> FindBundledFontPaths();

    std::vector<std::string> FindSystemCjkFontPaths();

    std::string FindSystemCjkFontPath();

    std::vector<std::string> FindFontCandidatePaths();

    std::string FontPathDisplayName(const std::string &path);

    bool IsSupportedFontPath(const std::string &path);
}

#endif // COREDECK_FONTS_H
