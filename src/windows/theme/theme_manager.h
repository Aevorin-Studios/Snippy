#pragma once

#include <windows.h>
#include <d2d1.h>
#include <string>

namespace snippy {

struct ThemePalette {
    D2D1_COLOR_F windowBg;
    D2D1_COLOR_F cardBg;
    D2D1_COLOR_F cardBorder;
    D2D1_COLOR_F inputBg;
    D2D1_COLOR_F inputBorder;
    D2D1_COLOR_F textPrimary;
    D2D1_COLOR_F textSecondary;
    D2D1_COLOR_F subtleBg;
    D2D1_COLOR_F subtleBorder;
    D2D1_COLOR_F btnSecondaryBg;
    D2D1_COLOR_F btnSecondaryBorder;
    D2D1_COLOR_F btnSecondaryHover;
    D2D1_COLOR_F dropdownBg;
    D2D1_COLOR_F dropdownHover;
    D2D1_COLOR_F scrollThumb;
    D2D1_COLOR_F sliderTrack;
    D2D1_COLOR_F accent;
    D2D1_COLOR_F accentHover;
    bool         isLight;
};

class ThemeManager {
public:
    static bool ParseHexColor(const std::wstring& value, D2D1_COLOR_F& outColor) noexcept;
    static D2D1_COLOR_F GetWindowsAccentColor() noexcept;
    static bool IsSystemInLightTheme() noexcept;
    static bool IsEffectiveThemeLight(const std::wstring& themeSetting) noexcept;
    static ThemePalette GetPalette(const std::wstring& themeSetting, const std::wstring& customAccent = L"", bool useAccentColor = true) noexcept;
};

} // namespace snippy
