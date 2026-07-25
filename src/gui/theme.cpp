//
// Created by AbdulMuaz Aqeel on 14/04/2026.
//

#include "imgui.h"

#include "theme.h"

namespace CoreDeck {
    namespace Colors {
        const char *WHITE = "#FFFFFF";
        const char *POSITIVE = "#33CC47";
        const char *POSITIVE_FILL = "#26B333";
        const char *NEGATIVE = "#E64D40";
        const char *NEGATIVE_STRONG = "#CC261F";
        const char *WARNING = "#D9B31A";
        const char *WARNING_STRONG = "#E6BF26";

        const char *ACCENT_PHONE = "#4FC3F7";
        const char *ACCENT_TABLET = "#22D3EE";
        const char *ACCENT_WEAR = "#F5A623";
        const char *ACCENT_TV = "#7E57C2";
        const char *ACCENT_INFO = "#4D9AFF";
        const char *ACCENT_INFO_SOFT = "#7AB8FF";

        const char *STORAGE_AVD = "#2980B9";
        const char *STORAGE_SYSTEM_IMAGE = "#27AE60";

        const char *TEXT_PRIMARY = "#F2F2F2";
        const char *TEXT_MUTED = "#66666B";
        const char *TEXT_SUBTLE = "#A7A7AD";
        const char *TEXT_ON_DARK = "#CCCCCC";
        const char *TEXT_ON_BRIGHT = "#969696";
        const char *TEXT_HINT = "#CFCFD4";

        const char *SHADOW = "#000000";
        const char *SURFACE0 = "#0F0F12";
        const char *SURFACE1 = "#141417";
        const char *SURFACE2 = "#1A1A1C";
        const char *SURFACE3 = "#29292B";
        const char *SURFACE4 = "#2E2E33";

        const char *BORDER_SUBTLE = "#3F3F42";
        const char *BORDER = "#47474A";
        const char *BORDER_STRONG = "#4D4D4F";
        const char *BORDER_HOVER = "#5C5C5E";
    }

    namespace {
        float ds = 1.0F;

        struct ThemePalette {
            const char *White;
            const char *Positive;
            const char *PositiveFill;
            const char *Negative;
            const char *NegativeStrong;
            const char *Warning;
            const char *WarningStrong;

            const char *AccentPhone;
            const char *AccentTablet;
            const char *AccentWear;
            const char *AccentTv;
            const char *AccentInfo;
            const char *AccentInfoSoft;

            const char *StorageAvd;
            const char *StorageSystemImage;

            const char *TextPrimary;
            const char *TextMuted;
            const char *TextSubtle;
            const char *TextOnDark;
            const char *TextOnBright;
            const char *TextHint;

            const char *Shadow;
            const char *Surface0;
            const char *Surface1;
            const char *Surface2;
            const char *Surface3;
            const char *Surface4;

            const char *BorderSubtle;
            const char *Border;
            const char *BorderStrong;
            const char *BorderHover;
        };

        constexpr ThemePalette DARK_PALETTE = {
            .White = "#FFFFFF",
            .Positive = "#33CC47",
            .PositiveFill = "#26B333",
            .Negative = "#E64D40",
            .NegativeStrong = "#CC261F",
            .Warning = "#D9B31A",
            .WarningStrong = "#E6BF26",
            .AccentPhone = "#4FC3F7",
            .AccentTablet = "#22D3EE",
            .AccentWear = "#F5A623",
            .AccentTv = "#7E57C2",
            .AccentInfo = "#4D9AFF",
            .AccentInfoSoft = "#7AB8FF",
            .StorageAvd = "#2980B9",
            .StorageSystemImage = "#27AE60",
            .TextPrimary = "#F2F2F2",
            .TextMuted = "#66666B",
            .TextSubtle = "#A7A7AD",
            .TextOnDark = "#CCCCCC",
            .TextOnBright = "#969696",
            .TextHint = "#CFCFD4",
            .Shadow = "#000000",
            .Surface0 = "#0F0F12",
            .Surface1 = "#141417",
            .Surface2 = "#1A1A1C",
            .Surface3 = "#29292B",
            .Surface4 = "#2E2E33",
            .BorderSubtle = "#3F3F42",
            .Border = "#47474A",
            .BorderStrong = "#4D4D4F",
            .BorderHover = "#5C5C5E",
        };

        constexpr ThemePalette LIGHT_PALETTE = {
            .White = "#FFFFFF",
            .Positive = "#1F8F38",
            .PositiveFill = "#2EAD4A",
            .Negative = "#C93A32",
            .NegativeStrong = "#A82620",
            .Warning = "#9A7412",
            .WarningStrong = "#7A5D0C",
            .AccentPhone = "#0277BD",
            .AccentTablet = "#0891B2",
            .AccentWear = "#B26A00",
            .AccentTv = "#673AB7",
            .AccentInfo = "#0B63CE",
            .AccentInfoSoft = "#2F80ED",
            .StorageAvd = "#1F6F9F",
            .StorageSystemImage = "#1E8F4D",
            .TextPrimary = "#202124",
            .TextMuted = "#6B7280",
            .TextSubtle = "#4B5563",
            .TextOnDark = "#F2F4F8",
            .TextOnBright = "#30343B",
            .TextHint = "#687385",
            .Shadow = "#000000",
            .Surface0 = "#F7F8FA",
            .Surface1 = "#FFFFFF",
            .Surface2 = "#EEF1F5",
            .Surface3 = "#E2E6EC",
            .Surface4 = "#D2D8E1",
            .BorderSubtle = "#D6DCE5",
            .Border = "#C6CED8",
            .BorderStrong = "#AEB7C4",
            .BorderHover = "#8F9BAD",
        };

        void ApplyColorPalette(const ThemePalette &palette) {
            Colors::WHITE = palette.White;
            Colors::POSITIVE = palette.Positive;
            Colors::POSITIVE_FILL = palette.PositiveFill;
            Colors::NEGATIVE = palette.Negative;
            Colors::NEGATIVE_STRONG = palette.NegativeStrong;
            Colors::WARNING = palette.Warning;
            Colors::WARNING_STRONG = palette.WarningStrong;

            Colors::ACCENT_PHONE = palette.AccentPhone;
            Colors::ACCENT_TABLET = palette.AccentTablet;
            Colors::ACCENT_WEAR = palette.AccentWear;
            Colors::ACCENT_TV = palette.AccentTv;
            Colors::ACCENT_INFO = palette.AccentInfo;
            Colors::ACCENT_INFO_SOFT = palette.AccentInfoSoft;

            Colors::STORAGE_AVD = palette.StorageAvd;
            Colors::STORAGE_SYSTEM_IMAGE = palette.StorageSystemImage;

            Colors::TEXT_PRIMARY = palette.TextPrimary;
            Colors::TEXT_MUTED = palette.TextMuted;
            Colors::TEXT_SUBTLE = palette.TextSubtle;
            Colors::TEXT_ON_DARK = palette.TextOnDark;
            Colors::TEXT_ON_BRIGHT = palette.TextOnBright;
            Colors::TEXT_HINT = palette.TextHint;

            Colors::SHADOW = palette.Shadow;
            Colors::SURFACE0 = palette.Surface0;
            Colors::SURFACE1 = palette.Surface1;
            Colors::SURFACE2 = palette.Surface2;
            Colors::SURFACE3 = palette.Surface3;
            Colors::SURFACE4 = palette.Surface4;

            Colors::BORDER_SUBTLE = palette.BorderSubtle;
            Colors::BORDER = palette.Border;
            Colors::BORDER_STRONG = palette.BorderStrong;
            Colors::BORDER_HOVER = palette.BorderHover;
        }
    }

    void ApplyCustomImGuiTheme(const ThemeMode themeMode, const float dpiScale) {
        ds = dpiScale;
        ApplyColorPalette(themeMode == ThemeMode::Light ? LIGHT_PALETTE : DARK_PALETTE);

        auto &style = ImGui::GetStyle();
        style = ImGuiStyle();

        if (themeMode == ThemeMode::Light) {
            ImGui::StyleColorsLight();
        } else {
            ImGui::StyleColorsDark();
        }

        const float s = dpiScale;

        style.WindowRounding = 6.0F * s;
        style.FrameRounding = 6.0F * s;
        style.GrabRounding = 6.0F * s;
        style.ScrollbarRounding = 6.0F * s;
        style.PopupRounding = 4.0F * s;
        style.FramePadding = ImVec2(8.0F * s, 8.0F * s);
        style.ItemSpacing = ImVec2(8.0F * s, 8.0F * s);
        style.ItemInnerSpacing = ImVec2(style.ItemInnerSpacing.x * s, style.ItemInnerSpacing.y * s);
        style.WindowPadding = ImVec2(8.0F * s, 8.0F * s);
        style.CellPadding = ImVec2(style.CellPadding.x * s, style.CellPadding.y * s);
        style.IndentSpacing = style.IndentSpacing * s;
        style.ScrollbarSize = 10.0F * s;
        style.GrabMinSize = style.GrabMinSize * s;
        style.FrameBorderSize = 0.6F;
        style.TabRounding = 0.0F;
        style.TabBarBorderSize = 0.0F;
        style.TabBorderSize = 0.0F;
        style.TabBarOverlineSize = 0.0F;

        auto &c = style.Colors;

        // Dock tabs — inactive
        c[ImGuiCol_Tab] = HexColor(Colors::SHADOW, 0.0F);
        c[ImGuiCol_TabHovered] = HexColor(Colors::SHADOW, 0.0F);
        c[ImGuiCol_TabSelected] = HexColor(Colors::SHADOW, 0.0F);
        c[ImGuiCol_TabSelectedOverline] = HexColor(Colors::SHADOW, 0.0F);

        // Dock tabs — unfocused window
        c[ImGuiCol_TabDimmed] = HexColor(Colors::SHADOW, 0.0F);
        c[ImGuiCol_TabDimmedSelected] = HexColor(Colors::SHADOW, 0.0F);
        c[ImGuiCol_TabDimmedSelectedOverline] = HexColor(Colors::SHADOW, 0.0F);

        // Docking preview overlay
        c[ImGuiCol_DockingPreview] = HexColor(Colors::TEXT_PRIMARY, 0.20F);
        c[ImGuiCol_DockingEmptyBg] = HexColor(Colors::SURFACE0);

        // Window
        c[ImGuiCol_WindowBg] = HexColor(Colors::SURFACE0);
        c[ImGuiCol_ChildBg] = HexColor(Colors::SURFACE0);
        c[ImGuiCol_PopupBg] = HexColor(Colors::SURFACE1, 0.98F);
        c[ImGuiCol_ModalWindowDimBg] = HexColor(Colors::SHADOW, 0.55F);

        // Borders
        c[ImGuiCol_Border] = HexColor(Colors::SURFACE4);
        c[ImGuiCol_BorderShadow] = HexColor(Colors::SHADOW, 0.0F);

        // Text
        c[ImGuiCol_Text] = HexColor(Colors::TEXT_PRIMARY);
        c[ImGuiCol_TextDisabled] = HexColor(Colors::TEXT_MUTED);

        // Headers
        c[ImGuiCol_Header] = HexColor(Colors::SURFACE3);
        c[ImGuiCol_HeaderHovered] = HexColor(Colors::SURFACE3);
        c[ImGuiCol_HeaderActive] = HexColor(Colors::SURFACE4);

        // Buttons
        c[ImGuiCol_Button] = HexColor(Colors::SURFACE2);
        c[ImGuiCol_ButtonHovered] = HexColor(Colors::SURFACE4);
        c[ImGuiCol_ButtonActive] = HexColor(Colors::SURFACE0);

        // Frame
        c[ImGuiCol_FrameBg] = HexColor(Colors::SURFACE1);
        c[ImGuiCol_FrameBgHovered] = HexColor(Colors::SURFACE3);
        c[ImGuiCol_FrameBgActive] = HexColor(Colors::SURFACE3);

        // Checkbox
        c[ImGuiCol_CheckMark] = HexColor(Colors::TEXT_PRIMARY);

        // Slider
        c[ImGuiCol_SliderGrab] = HexColor(Colors::TEXT_PRIMARY);
        c[ImGuiCol_SliderGrabActive] = HexColor(Colors::TEXT_ON_DARK);

        // Scrollbar
        c[ImGuiCol_ScrollbarBg] = HexColor(Colors::SURFACE0);
        c[ImGuiCol_ScrollbarGrab] = HexColor(Colors::SURFACE4);
        c[ImGuiCol_ScrollbarGrabHovered] = HexColor(Colors::BORDER);
        c[ImGuiCol_ScrollbarGrabActive] = HexColor(Colors::BORDER_HOVER);

        // Separator
        c[ImGuiCol_Separator] = HexColor(Colors::SURFACE2);
        c[ImGuiCol_SeparatorHovered] = HexColor(Colors::BORDER_STRONG);
        c[ImGuiCol_SeparatorActive] = HexColor(Colors::TEXT_MUTED);

        // Menu bar
        c[ImGuiCol_MenuBarBg] = HexColor(Colors::SURFACE0);

        // Title bar
        c[ImGuiCol_TitleBg] = HexColor(Colors::SURFACE0);
        c[ImGuiCol_TitleBgActive] = HexColor(Colors::SURFACE1);
        c[ImGuiCol_TitleBgCollapsed] = HexColor(Colors::SURFACE0);

        // Text selection
        c[ImGuiCol_TextSelectedBg] = HexColor(Colors::BORDER_SUBTLE, 0.60F);

        // Resize grip
        c[ImGuiCol_ResizeGrip] = HexColor(Colors::SURFACE4, 0.25F);
        c[ImGuiCol_ResizeGripHovered] = HexColor(Colors::BORDER_STRONG, 0.65F);
        c[ImGuiCol_ResizeGripActive] = HexColor(Colors::TEXT_MUTED, 0.95F);
    }

    ImVec4 GetAppClearColor() {
        return HexColor(Colors::SURFACE0);
    }

    float GetDpiScale() {
        return ds;
    }
}
