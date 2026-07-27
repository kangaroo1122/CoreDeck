//
// Created by AbdulMuaz Aqeel on 14/04/2026.
//

#ifndef COREDECK_THEME_H
#define COREDECK_THEME_H

#include <cstdint>

#include "imgui.h"

namespace CoreDeck {
    enum class ThemeMode : uint8_t {
        Dark,
        Light,
    };

    namespace Icons {
        constexpr const char *PLAY = "\xef\x81\x8b";
        constexpr const char *STOP = "\xef\x81\x8d";
        constexpr const char *REFRESH = "\xef\x80\xa1";
        constexpr const char *TRASH = "\xef\x87\xb8";
        constexpr const char *CIRCLE = "\xef\x84\x91";
        constexpr const char *DESKTOP = "\xef\x84\x88";
        constexpr const char *GEAR = "\xef\x80\x93";
        constexpr const char *TERMINAL = "\xef\x84\xa0";
        constexpr const char *JAVA = "\xef\x83\xb4";
        constexpr const char *INFO = "\xef\x81\x9a";
        constexpr const char *SEARCH = "\xef\x80\x82";
        constexpr const char *PLUS = "\xef\x81\xa7";
        constexpr const char *SORT_UP = "\xef\x83\x9e";
        constexpr const char *SORT_DOWN = "\xef\x83\x9d";
        constexpr const char *SORT = "\xef\x83\x9c";
        constexpr const char *TIMES = "\xef\x80\x8d";
        constexpr const char *MOBILE = "\xef\x8f\x8d";
        constexpr const char *TABLET = "\xef\x8f\xba";
        constexpr const char *TV = "\xef\x89\xac";
        constexpr const char *WATCH = "\xef\x80\x97";
        constexpr const char *CAR = "\xef\x86\xb9";
        constexpr const char *COPY = "\xef\x83\x85";
        constexpr const char *CHEVRON_LEFT = "\xef\x81\x93";
        constexpr const char *CHEVRON_RIGHT = "\xef\x81\x94";
        constexpr const char *PENCIL = "\xef\x8c\x83";
        constexpr const char *FOLDER = "\xef\x81\xbb";
        constexpr const char *FOLDER_PLUS = "\xef\x99\x9e";
        constexpr const char *FILE = "\xef\x85\x9b";
        constexpr const char *UPLOAD = "\xef\x82\x93";
        constexpr const char *DOWNLOAD = "\xef\x80\x99";
        constexpr const char *ARROW_UP = "\xef\x81\xa2";
        constexpr const char *HOME = "\xef\x80\x95";
    }

    namespace Colors {
        extern const char *WHITE;
        extern const char *POSITIVE;
        extern const char *POSITIVE_FILL;
        extern const char *NEGATIVE;
        extern const char *NEGATIVE_STRONG;
        extern const char *WARNING;
        extern const char *WARNING_STRONG;

        extern const char *ACCENT_PHONE;
        extern const char *ACCENT_TABLET;
        extern const char *ACCENT_WEAR;
        extern const char *ACCENT_TV;
        extern const char *ACCENT_INFO;
        extern const char *ACCENT_INFO_SOFT;

        extern const char *STORAGE_AVD;
        extern const char *STORAGE_SYSTEM_IMAGE;

        extern const char *TEXT_PRIMARY;
        extern const char *TEXT_MUTED;
        extern const char *TEXT_SUBTLE;
        extern const char *TEXT_ON_DARK;
        extern const char *TEXT_ON_BRIGHT;
        extern const char *TEXT_HINT;

        extern const char *SHADOW;
        extern const char *SURFACE0;
        extern const char *SURFACE1;
        extern const char *SURFACE2;
        extern const char *SURFACE3;
        extern const char *SURFACE4;

        extern const char *BORDER_SUBTLE;
        extern const char *BORDER;
        extern const char *BORDER_STRONG;
        extern const char *BORDER_HOVER;
    }

    void ApplyCustomImGuiTheme(ThemeMode themeMode = ThemeMode::Dark, float dpiScale = 1.0F);

    ImVec4 GetAppClearColor();

    float GetDpiScale();

    constexpr ImVec4 HexColor(const char *hex, float alpha = 1.0F) {
        auto hexToByte = [](const char hi, const char lo) -> float {
            auto charVal = [](const char c) -> int {
                if (c >= '0' && c <= '9') {
                    return c - '0';
                }
                if (c >= 'a' && c <= 'f') {
                    return 10 + c - 'a';
                }
                if (c >= 'A' && c <= 'F') {
                    return 10 + c - 'A';
                }
                return 0;
            };
            return static_cast<float>((charVal(hi) * 16) + charVal(lo)) / 255.0F;
        };

        if (hex[0] == '#') {
            hex++;
        }

        return {
            hexToByte(hex[0], hex[1]),
            hexToByte(hex[2], hex[3]),
            hexToByte(hex[4], hex[5]),
            alpha
        };
    }
}

#endif // COREDECK_THEME_H
