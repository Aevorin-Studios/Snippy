#include "settings_manager.h"
#include <shlobj.h>
#include <cstdio>
#include <cwctype>
#include <algorithm>
#include <sstream>

namespace snippy {

AppSettings SettingsManager::s_settings;

static const WCHAR kRunKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
static const WCHAR kAppName[] = L"Snippy";
static const WCHAR kIniSection[] = L"Overlay";

std::wstring SettingsManager::GetSettingsDir() noexcept {
    WCHAR path[MAX_PATH]{};
    if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, 0, path))) {
        std::wstring dir = std::wstring(path) + L"\\Snippy";
        CreateDirectoryW(dir.c_str(), nullptr);
        return dir;
    }
    return L".";
}

std::wstring SettingsManager::GetJsonPath() noexcept {
    return GetSettingsDir() + L"\\settings.json";
}

std::wstring SettingsManager::GetIniPath() noexcept {
    return GetSettingsDir() + L"\\settings.ini";
}

// ── Windows Startup Registry ──────────────────────────────────────────────────

bool SettingsManager::IsAutoStartEnabled() noexcept {
    HKEY hKey = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        WCHAR value[MAX_PATH * 2]{};
        DWORD type = 0;
        DWORD size = sizeof(value);
        LONG res = RegQueryValueExW(hKey, kAppName, nullptr, &type, reinterpret_cast<BYTE*>(value), &size);
        RegCloseKey(hKey);
        return res == ERROR_SUCCESS;
    }
    return false;
}

void SettingsManager::SetAutoStart(bool enable) noexcept {
    HKEY hKey = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_WRITE, &hKey) == ERROR_SUCCESS) {
        if (enable) {
            WCHAR exePath[MAX_PATH]{};
            if (GetModuleFileNameW(nullptr, exePath, MAX_PATH) > 0) {
                std::wstring cmd = L"\"" + std::wstring(exePath) + L"\"";
                RegSetValueExW(hKey, kAppName, 0, REG_SZ,
                               reinterpret_cast<const BYTE*>(cmd.c_str()),
                               static_cast<DWORD>((cmd.length() + 1) * sizeof(WCHAR)));
            }
        } else {
            RegDeleteValueW(hKey, kAppName);
        }
        RegCloseKey(hKey);
    }
}

// ── Key Formatter & Mapping ───────────────────────────────────────────────────

std::wstring SettingsManager::FormatModifiers(bool alt, bool ctrl, bool shift, bool win) noexcept {
    std::wstring str;
    if (ctrl)  str += (str.empty() ? L"" : L" + ") + std::wstring(L"Ctrl");
    if (alt)   str += (str.empty() ? L"" : L" + ") + std::wstring(L"Alt");
    if (shift) str += (str.empty() ? L"" : L" + ") + std::wstring(L"Shift");
    if (win)   str += (str.empty() ? L"" : L" + ") + std::wstring(L"Win");
    return str;
}

std::wstring SettingsManager::FormatKey(UINT vk) noexcept {
    switch (vk) {
    case VK_SNAPSHOT: return L"PrintScreen";
    case VK_PRIOR:    return L"PageUp";
    case VK_NEXT:     return L"PageDown";
    case VK_HOME:     return L"Home";
    case VK_END:      return L"End";
    case VK_INSERT:   return L"Insert";
    case VK_DELETE:   return L"Delete";
    case VK_SPACE:    return L"Space";
    case VK_TAB:      return L"Tab";
    case VK_ESCAPE:   return L"Esc";
    case VK_OEM_3:    return L"`";
    case VK_OEM_MINUS:return L"-";
    case VK_OEM_PLUS: return L"=";
    case VK_OEM_4:    return L"[";
    case VK_OEM_6:    return L"]";
    case VK_OEM_7:    return L"'";
    case VK_OEM_1:    return L";";
    case VK_OEM_COMMA:return L",";
    case VK_OEM_PERIOD:return L".";
    case VK_OEM_2:    return L"/";
    case VK_OEM_5:    return L"\\";
    }

    if (vk >= VK_F1 && vk <= VK_F24) {
        return L"F" + std::to_wstring(vk - VK_F1 + 1);
    }
    if (vk >= '0' && vk <= '9') {
        return std::wstring(1, static_cast<WCHAR>(vk));
    }
    if (vk >= 'A' && vk <= 'Z') {
        return std::wstring(1, static_cast<WCHAR>(vk));
    }
    if (vk >= VK_NUMPAD0 && vk <= VK_NUMPAD9) {
        return L"Num " + std::to_wstring(vk - VK_NUMPAD0);
    }

    return NameFromVk(vk);
}

std::wstring SettingsManager::FormatHotkey(bool alt, bool ctrl, bool shift, bool win, UINT vk) noexcept {
    std::wstring mods = FormatModifiers(alt, ctrl, shift, win);
    std::wstring k = FormatKey(vk);
    if (mods.empty()) return k;
    return mods + L" + " + k;
}

UINT SettingsManager::VkFromName(const std::wstring& name) noexcept {
    if (name.empty()) return 'S';
    if (name.size() == 1) {
        WCHAR ch = static_cast<WCHAR>(std::towupper(name[0]));
        if ((ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9')) {
            return static_cast<UINT>(ch);
        }
        if (ch == L'`' || ch == L'~') return VK_OEM_3;
        if (ch == L'-') return VK_OEM_MINUS;
        if (ch == L'=' || ch == L'+') return VK_OEM_PLUS;
        if (ch == L'[') return VK_OEM_4;
        if (ch == L']') return VK_OEM_6;
        if (ch == L'\'') return VK_OEM_7;
        if (ch == L';') return VK_OEM_1;
        if (ch == L',') return VK_OEM_COMMA;
        if (ch == L'.') return VK_OEM_PERIOD;
        if (ch == L'/') return VK_OEM_2;
        if (ch == L'\\') return VK_OEM_5;
    }

    std::wstring lower;
    for (WCHAR c : name) lower += static_cast<WCHAR>(std::towlower(c));

    if (lower == L"snapshot" || lower == L"printscreen" || lower == L"prntscrn") return VK_SNAPSHOT;
    if (lower == L"pageup") return VK_PRIOR;
    if (lower == L"pagedown") return VK_NEXT;
    if (lower == L"home") return VK_HOME;
    if (lower == L"end") return VK_END;
    if (lower == L"insert") return VK_INSERT;
    if (lower == L"delete") return VK_DELETE;
    if (lower == L"space") return VK_SPACE;
    if (lower == L"tab") return VK_TAB;

    if (lower.rfind(L"f", 0) == 0 && lower.size() >= 2) {
        try {
            int num = std::stoi(lower.substr(1));
            if (num >= 1 && num <= 24) return static_cast<UINT>(VK_F1 + num - 1);
        } catch (...) {}
    }

    return 'S';
}

std::wstring SettingsManager::NameFromVk(UINT vk) noexcept {
    switch (vk) {
    case VK_SNAPSHOT:  return L"Snapshot";
    case VK_PRIOR:     return L"PageUp";
    case VK_NEXT:      return L"PageDown";
    case VK_HOME:      return L"Home";
    case VK_END:       return L"End";
    case VK_INSERT:    return L"Insert";
    case VK_DELETE:    return L"Delete";
    case VK_SPACE:     return L"Space";
    case VK_TAB:       return L"Tab";
    case VK_OEM_3:     return L"`";
    case VK_OEM_MINUS: return L"-";
    case VK_OEM_PLUS:  return L"=";
    case VK_OEM_4:     return L"[";
    case VK_OEM_6:     return L"]";
    case VK_OEM_7:     return L"'";
    case VK_OEM_1:     return L";";
    case VK_OEM_COMMA: return L",";
    case VK_OEM_PERIOD:return L".";
    case VK_OEM_2:     return L"/";
    case VK_OEM_5:     return L"\\";
    }

    if (vk >= VK_F1 && vk <= VK_F24) return L"F" + std::to_wstring(vk - VK_F1 + 1);
    if ((vk >= 'A' && vk <= 'Z') || (vk >= '0' && vk <= '9')) {
        return std::wstring(1, static_cast<WCHAR>(vk));
    }
    return L"S";
}

UINT SettingsManager::GetWin32Modifiers(bool alt, bool ctrl, bool shift, bool win) noexcept {
    UINT mod = MOD_NOREPEAT;
    if (alt)   mod |= MOD_ALT;
    if (ctrl)  mod |= MOD_CONTROL;
    if (shift) mod |= MOD_SHIFT;
    if (win)   mod |= MOD_WIN;
    return mod;
}

// ── Load & Save ───────────────────────────────────────────────────────────────

void SettingsManager::ResetToDefaults() noexcept {
    s_settings = AppSettings{};
    s_settings.runAtStartup = IsAutoStartEnabled();
    SaveSettings();
}

void SettingsManager::LoadSettings() noexcept {
    s_settings.runAtStartup = IsAutoStartEnabled();

    std::wstring jsonPath = GetJsonPath();
    HANDLE hFile = CreateFileW(jsonPath.c_str(), GENERIC_READ, FILE_SHARE_READ,
                               nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile != INVALID_HANDLE_VALUE) {
        DWORD size = GetFileSize(hFile, nullptr);
        if (size > 0 && size < 1048576) {
            std::string content(size, '\0');
            DWORD read = 0;
            if (ReadFile(hFile, content.data(), size, &read, nullptr) && read > 0) {
                DeserializeSettingsJson(content, s_settings);
                s_settings.vk = VkFromName(s_settings.keyName);
            }
        }
        CloseHandle(hFile);
    } else {
        // Fallback: check settings.ini
        std::wstring iniPath = GetIniPath();
        if (GetFileAttributesW(iniPath.c_str()) != INVALID_FILE_ATTRIBUTES) {
            WCHAR val[256]{};
            GetPrivateProfileStringW(kIniSection, L"Theme", L"System", val, 256, iniPath.c_str());
            s_settings.theme = val;
            s_settings.useAccentColor = (GetPrivateProfileIntW(kIniSection, L"UseAccentColor", 1, iniPath.c_str()) != 0);
            GetPrivateProfileStringW(kIniSection, L"AccentColor", L"", val, 256, iniPath.c_str());
            s_settings.accentColor = val;
            GetPrivateProfileStringW(kIniSection, L"ToolbarPosition", L"Top", val, 256, iniPath.c_str());
            s_settings.toolbarPosition = val;
            s_settings.dimmingOpacity = GetPrivateProfileIntW(kIniSection, L"DimmingOpacity", 50, iniPath.c_str());
            s_settings.selectionBorderThickness = GetPrivateProfileIntW(kIniSection, L"SelectionBorderThickness", 2, iniPath.c_str());
            WCHAR hTools[512]{};
            GetPrivateProfileStringW(kIniSection, L"HiddenTools", L"", hTools, 512, iniPath.c_str());
            if (hTools[0] != L'\0') {
                s_settings.hiddenTools.clear();
                std::wstring valStr = hTools;
                size_t start = 0;
                while (start < valStr.size()) {
                    size_t comma = valStr.find(L',', start);
                    std::wstring item = valStr.substr(start, comma == std::wstring::npos ? comma : comma - start);
                    if (!item.empty()) s_settings.hiddenTools.push_back(item);
                    if (comma == std::wstring::npos) break;
                    start = comma + 1;
                }
            }
        }
    }
}

void SettingsManager::SaveSettings() noexcept {
    std::wstring jsonPath = GetJsonPath();
    HANDLE hFile = CreateFileW(jsonPath.c_str(), GENERIC_WRITE, 0,
                               nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile != INVALID_HANDLE_VALUE) {
        std::string json = SerializeSettingsJson(s_settings);
        DWORD written = 0;
        WriteFile(hFile, json.data(), static_cast<DWORD>(json.size()), &written, nullptr);
        CloseHandle(hFile);
    }

    std::wstring iniPath = GetIniPath();
    WritePrivateProfileStringW(kIniSection, L"Theme", s_settings.theme.c_str(), iniPath.c_str());
    WritePrivateProfileStringW(kIniSection, L"UseAccentColor", s_settings.useAccentColor ? L"1" : L"0", iniPath.c_str());
    WritePrivateProfileStringW(kIniSection, L"AccentColor", s_settings.accentColor.c_str(), iniPath.c_str());
    WritePrivateProfileStringW(kIniSection, L"ToolbarPosition", s_settings.toolbarPosition.c_str(), iniPath.c_str());
    WritePrivateProfileStringW(kIniSection, L"DimmingOpacity", std::to_wstring(s_settings.dimmingOpacity).c_str(), iniPath.c_str());
    WritePrivateProfileStringW(kIniSection, L"SelectionBorderThickness", std::to_wstring(s_settings.selectionBorderThickness).c_str(), iniPath.c_str());

    std::wstring hiddenToolsStr;
    for (size_t i = 0; i < s_settings.hiddenTools.size(); ++i) {
        hiddenToolsStr += s_settings.hiddenTools[i] + (i + 1 < s_settings.hiddenTools.size() ? L"," : L"");
    }
    WritePrivateProfileStringW(kIniSection, L"HiddenTools", hiddenToolsStr.c_str(), iniPath.c_str());

    SetAutoStart(s_settings.runAtStartup);
}

} // namespace snippy
