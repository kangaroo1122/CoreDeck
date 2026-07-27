#include "fonts.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <vector>

#include "../core/paths.h"

namespace CoreDeck {
    namespace {
        std::vector<std::string> BundledFontCandidates() {
            const std::string resourcesDir = Paths::GetResourcesDirectory();
            return {
                Paths::JoinPaths({resourcesDir, "assets", "fonts", "PingFang SC Heavy.ttf"}),
                Paths::JoinPaths({resourcesDir, "assets", "fonts", "JetBrainsMono-Regular.ttf"}),
                Paths::JoinPaths({resourcesDir, "assets", "fonts", "JetBrainsMono-Bold.ttf"}),
            };
        }

        std::vector<std::string> SystemCjkFontCandidates() {
#if defined(_WIN32)
            return {
                "C:\\Windows\\Fonts\\msyh.ttc",
                "C:\\Windows\\Fonts\\msyh.ttf",
                "C:\\Windows\\Fonts\\simhei.ttf",
                "C:\\Windows\\Fonts\\simsun.ttc",
            };
#elif defined(__APPLE__)
            return {
                "/System/Library/Fonts/PingFang.ttc",
                "/System/Library/Fonts/Hiragino Sans GB.ttc",
                "/System/Library/Fonts/STHeiti Light.ttc",
                "/System/Library/Fonts/STHeiti Medium.ttc",
                "/System/Library/Fonts/Supplemental/Songti.ttc",
                "/System/Library/Fonts/Supplemental/Arial Unicode.ttf",
            };
#else
            return {
                "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc",
                "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.otf",
                "/usr/share/fonts/truetype/noto/NotoSansCJK-Regular.ttc",
                "/usr/share/fonts/truetype/noto/NotoSansCJK-Regular.otf",
                "/usr/share/fonts/truetype/wqy/wqy-microhei.ttc",
                "/usr/share/fonts/truetype/wqy/wqy-zenhei.ttc",
            };
#endif
        }

        std::string LowerCopy(std::string value) {
            std::ranges::transform(value, value.begin(), [](const unsigned char c) {
                return static_cast<char>(std::tolower(c));
            });
            return value;
        }

        void AppendSupportedFonts(std::vector<std::string> &fonts, const std::vector<std::string> &candidates) {
            for (const auto &path: candidates) {
                if (IsSupportedFontPath(path) && std::ranges::find(fonts, path) == fonts.end()) {
                    fonts.push_back(path);
                }
            }
        }
    }

    std::vector<std::string> FindBundledFontPaths() {
        std::vector<std::string> fonts;
        AppendSupportedFonts(fonts, BundledFontCandidates());
        return fonts;
    }

    std::vector<std::string> FindSystemCjkFontPaths() {
        std::vector<std::string> fonts;
        AppendSupportedFonts(fonts, SystemCjkFontCandidates());
        return fonts;
    }

    std::string FindSystemCjkFontPath() {
        const auto fonts = FindSystemCjkFontPaths();
        if (!fonts.empty()) {
            return fonts.front();
        }
        return "";
    }

    std::vector<std::string> FindFontCandidatePaths() {
        std::vector<std::string> fonts;
        AppendSupportedFonts(fonts, BundledFontCandidates());
        AppendSupportedFonts(fonts, SystemCjkFontCandidates());
        return fonts;
    }

    std::string FontPathDisplayName(const std::string &path) {
        if (path.empty()) {
            return "";
        }
        return std::filesystem::path(path).filename().string();
    }

    bool IsSupportedFontPath(const std::string &path) {
        if (path.empty() || !std::filesystem::exists(path)) {
            return false;
        }

        const std::string ext = LowerCopy(std::filesystem::path(path).extension().string());
        return ext == ".ttf" || ext == ".otf" || ext == ".ttc";
    }
}
