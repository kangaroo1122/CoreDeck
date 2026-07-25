#ifndef COREDECK_LOCALIZATION_H
#define COREDECK_LOCALIZATION_H

#include <cstdint>
#include <string>

namespace CoreDeck {
    enum class AppLanguage : uint8_t {
        English,
        SimplifiedChinese,
    };

    void SetCurrentLanguage(AppLanguage language);

    AppLanguage GetCurrentLanguage();

    const char *LanguageDisplayName(AppLanguage language);

    const char *Tr(const char *text);

    std::string TrLabel(const char *label);

    const char *SimplifiedChineseGlyphText();
}

#endif // COREDECK_LOCALIZATION_H
