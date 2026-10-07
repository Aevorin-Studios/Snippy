#include "theme_manager.h"
#include <dwmapi.h>
#include <algorithm>
#include <cstdio>

namespace snippy {

static bool ParseHexColor(const std::wstring& value, D2D1_COLOR_F& outColor) noexcept {
    if (value.size() != 7 || value[0] != L'#') return false;
    unsigned int rgb = 0;
    if (swscanf_s(value.c_str() + 1, L"%06x", &rgb) != 1) return false;
    outColor = D2D1::ColorF(((rgb >> 16) & 0xff) / 255.f,
                            ((rgb >> 8) & 0xff) / 255.f,
                            (rgb & 0xff) / 255.f, 1.f);
    return true;
}

D2D1_COLOR_F ThemeManager::GetWindowsAccentColor() noexcept {
    // Method 1: HKEY_CURRENT_USER\Software\Microsoft\Windows\DWM -> AccentColor (ABGR DWORD)
    HKEY key = nullptr;
    DWORD accentDword = 0;
    DWORD valSize = sizeof(accentDword);
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\DWM", 0, KEY_QUERY_VALUE, &key) == ERROR_SUCCESS) {
        if (RegQueryValueExW(key, L"AccentColor", nullptr, nullptr,
                             reinterpret_cast<BYTE*>(&accentDword), &valSize) == ERROR_SUCCESS && accentDword != 0) {
            RegCloseKey(key);
            float r = (accentDword & 0xFF) / 255.0f;
            float g = ((accentDword >> 8) & 0xFF) / 255.0f;
            float b = ((accentDword >> 16) & 0xFF) / 255.0f;
            return D2D1::ColorF(r, g, b, 1.0f);
        }
        RegCloseKey(key);
    }

    // Method 2: HKEY_CURRENT_USER\Software\Microsoft\Windows\CurrentVersion\Explorer\Accent -> AccentColorMenu (ABGR DWORD)
    key = nullptr;
    valSize = sizeof(accentDword);
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Accent", 0, KEY_QUERY_VALUE, &key) == ERROR_SUCCESS) {
        if (RegQueryValueExW(key, L"AccentColorMenu", nullptr, nullptr,
                             reinterpret_cast<BYTE*>(&accentDword), &valSize) == ERROR_SUCCESS && accentDword != 0) {
            RegCloseKey(key);
            float r = (accentDword & 0xFF) / 255.0f;
            float g = ((accentDword >> 8) & 0xFF) / 255.0f;
            float b = ((accentDword >> 16) & 0xFF) / 255.0f;
            return D2D1::ColorF(r, g, b, 1.0f);
        }
        RegCloseKey(key);
    }

    // Method 3: DWM Colorization Color (ARGB DWORD)
    DWORD colorization = 0;
    BOOL opaque = FALSE;
    if (SUCCEEDED(DwmGetColorizationColor(&colorization, &opaque)) && colorization != 0) {
        float r = ((colorization >> 16) & 0xFF) / 255.0f;
        float g = ((colorization >> 8) & 0xFF) / 255.0f;
        float b = (colorization & 0xFF) / 255.0f;
        return D2D1::ColorF(r, g, b, 1.0f);
    }

    // Default Fluent Blue (#0078D4)
    return D2D1::ColorF(0.0f, 0.471f, 0.831f, 1.0f);
}

bool ThemeManager::IsSystemInLightTheme() noexcept {
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER,
                      L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                      0, KEY_QUERY_VALUE, &key) == ERROR_SUCCESS) {
        DWORD appsUseLightTheme = 0;
        DWORD size = sizeof(appsUseLightTheme);
        if (RegQueryValueExW(key, L"AppsUseLightTheme", nullptr, nullptr,
                             reinterpret_cast<BYTE*>(&appsUseLightTheme), &size) == ERROR_SUCCESS) {
            RegCloseKey(key);
            return appsUseLightTheme == 1;
        }

        DWORD systemUsesLightTheme = 0;
        size = sizeof(systemUsesLightTheme);
        if (RegQueryValueExW(key, L"SystemUsesLightTheme", nullptr, nullptr,
                             reinterpret_cast<BYTE*>(&systemUsesLightTheme), &size) == ERROR_SUCCESS) {
            RegCloseKey(key);
            return systemUsesLightTheme == 1;
        }
        RegCloseKey(key);
    }
    return false;
}

bool ThemeManager::IsEffectiveThemeLight(const std::wstring& themeSetting) noexcept {
    if (_wcsicmp(themeSetting.c_str(), L"Light") == 0) return true;
    if (_wcsicmp(themeSetting.c_str(), L"Dark") == 0) return false;
    return IsSystemInLightTheme();
}

ThemePalette ThemeManager::GetPalette(const std::wstring& themeSetting, const std::wstring& customAccent) noexcept {
    ThemePalette p{};
    p.isLight = IsEffectiveThemeLight(themeSetting);

    // Accent color
    if (customAccent.empty() || !ParseHexColor(customAccent, p.accent)) {
        p.accent = GetWindowsAccentColor();
    }

    // Hover state is slightly lighter / brighter
    p.accentHover = D2D1::ColorF(
        std::min(1.0f, p.accent.r + 0.08f),
        std::min(1.0f, p.accent.g + 0.08f),
        std::min(1.0f, p.accent.b + 0.08f),
        1.0f
    );

    if (p.isLight) {
        // Fluent Light Theme Palette
        p.windowBg            = D2D1::ColorF(0.976f, 0.976f, 0.976f, 1.0f); // #F9F9F9
        p.cardBg              = D2D1::ColorF(1.0f, 1.0f, 1.0f, 1.0f);       // #FFFFFF
        p.cardBorder          = D2D1::ColorF(0.898f, 0.898f, 0.910f, 1.0f); // #E5E5E8
        p.inputBg             = D2D1::ColorF(1.0f, 1.0f, 1.0f, 1.0f);       // #FFFFFF
        p.inputBorder         = D2D1::ColorF(0.835f, 0.835f, 0.855f, 1.0f); // #D5D5DA
        p.textPrimary         = D2D1::ColorF(0.110f, 0.110f, 0.118f, 1.0f); // #1C1C1E
        p.textSecondary       = D2D1::ColorF(0.361f, 0.361f, 0.376f, 1.0f); // #5C5C60
        p.subtleBg            = D2D1::ColorF(0.953f, 0.953f, 0.961f, 1.0f); // #F3F3F5
        p.subtleBorder        = D2D1::ColorF(0.878f, 0.878f, 0.898f, 1.0f); // #E0E0E5
        p.btnSecondaryBg      = D2D1::ColorF(0.941f, 0.941f, 0.953f, 1.0f); // #F0F0F3
        p.btnSecondaryBorder  = D2D1::ColorF(0.835f, 0.835f, 0.855f, 1.0f); // #D5D5DA
        p.btnSecondaryHover   = D2D1::ColorF(0.886f, 0.886f, 0.902f, 1.0f); // #E2E2E6
        p.dropdownBg          = D2D1::ColorF(1.0f, 1.0f, 1.0f, 1.0f);       // #FFFFFF
        p.dropdownHover       = D2D1::ColorF(0.941f, 0.941f, 0.953f, 1.0f); // #F0F0F3
        p.scrollThumb         = D2D1::ColorF(0.753f, 0.753f, 0.776f, 0.8f); // #C0C0C6
        p.sliderTrack         = D2D1::ColorF(0.878f, 0.878f, 0.898f, 1.0f); // #E0E0E5
    } else {
        // Fluent Dark Theme Palette
        p.windowBg            = D2D1::ColorF(0.094f, 0.094f, 0.094f, 1.0f); // #181818
        p.cardBg              = D2D1::ColorF(0.141f, 0.141f, 0.157f, 1.0f); // #242428
        p.cardBorder          = D2D1::ColorF(0.208f, 0.208f, 0.227f, 1.0f); // #35353A
        p.inputBg             = D2D1::ColorF(0.071f, 0.071f, 0.078f, 1.0f); // #121214
        p.inputBorder         = D2D1::ColorF(0.208f, 0.208f, 0.227f, 1.0f); // #35353A
        p.textPrimary         = D2D1::ColorF(1.0f, 1.0f, 1.0f, 1.0f);       // #FFFFFF
        p.textSecondary       = D2D1::ColorF(0.627f, 0.627f, 0.627f, 1.0f); // #A0A0A0
        p.subtleBg            = D2D1::ColorF(0.094f, 0.094f, 0.102f, 1.0f); // #18181A
        p.subtleBorder        = D2D1::ColorF(0.208f, 0.208f, 0.227f, 1.0f); // #35353A
        p.btnSecondaryBg      = D2D1::ColorF(0.176f, 0.176f, 0.176f, 1.0f); // #2D2D2D
        p.btnSecondaryBorder  = D2D1::ColorF(0.243f, 0.243f, 0.259f, 1.0f); // #3E3E42
        p.btnSecondaryHover   = D2D1::ColorF(0.220f, 0.220f, 0.220f, 1.0f); // #383838
        p.dropdownBg          = D2D1::ColorF(0.118f, 0.118f, 0.133f, 1.0f); // #1E1E22
        p.dropdownHover       = D2D1::ColorF(0.176f, 0.176f, 0.208f, 1.0f); // #2D2D35
        p.scrollThumb         = D2D1::ColorF(0.290f, 0.290f, 0.322f, 0.8f); // #4A4A52
        p.sliderTrack         = D2D1::ColorF(0.227f, 0.227f, 0.251f, 1.0f); // #3A3A40
    }

    return p;
}

} // namespace snippy
