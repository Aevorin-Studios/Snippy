#pragma once

#include "core/settings.h"
#include <windows.h>
#include <string>
#include <vector>

namespace snippy {

class SettingsManager {
public:
    static void LoadSettings() noexcept;
    static void SaveSettings() noexcept;
    static void ResetToDefaults() noexcept;

    static AppSettings& Current() noexcept { return s_settings; }

    static bool IsAutoStartEnabled() noexcept;
    static void SetAutoStart(bool enable) noexcept;

    static std::wstring FormatModifiers(bool alt, bool ctrl, bool shift, bool win) noexcept;
    static std::wstring FormatKey(UINT vk) noexcept;
    static std::wstring FormatHotkey(bool alt, bool ctrl, bool shift, bool win, UINT vk) noexcept;
    static UINT VkFromName(const std::wstring& name) noexcept;
    static std::wstring NameFromVk(UINT vk) noexcept;
    static UINT GetWin32Modifiers(bool alt, bool ctrl, bool shift, bool win) noexcept;

private:
    static std::wstring GetSettingsDir() noexcept;
    static std::wstring GetJsonPath() noexcept;
    static std::wstring GetIniPath() noexcept;

    static AppSettings s_settings;
};

} // namespace snippy
