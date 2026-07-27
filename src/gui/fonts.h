#ifndef COREDECK_FONTS_H
#define COREDECK_FONTS_H

#include <string>
#include <vector>

namespace CoreDeck {
    constexpr float DEFAULT_UI_FONT_SIZE = 16.0F;
    constexpr float MIN_UI_FONT_SIZE = 12.0F;
    constexpr float MAX_UI_FONT_SIZE = 36.0F;

    float NormalizeUiFontSize(float size);

    std::vector<std::string> FindBundledFontPaths();

    std::vector<std::string> FindSystemCjkFontPaths();

    std::string FindSystemCjkFontPath();

    std::vector<std::string> FindFontCandidatePaths();

    std::string FontPathDisplayName(const std::string &path);

    bool IsSupportedFontPath(const std::string &path);
}

#endif // COREDECK_FONTS_H
