#pragma once

#include <string>
#include <vector>
#include <cstdint>

namespace snippy {

struct AppSettings {
    bool altModifier   = true;
    bool ctrlModifier  = false;
    bool shiftModifier = false;
    bool winModifier   = false;
    std::wstring keyName = L"S";
    uint32_t vk = 'S';

    std::wstring theme = L"System";
    bool enableVoiceActivation = true;
    bool enableDebugLogger = false;
    bool runAtStartup = false;

    std::wstring accentColor = L"";
    int dimmingOpacity = 50;
    int selectionBorderThickness = 2;
    std::wstring toolbarPosition = L"Top";
    std::vector<std::wstring> hiddenTools;
};

std::string SerializeSettingsJson(const AppSettings& s);
bool DeserializeSettingsJson(const std::string& json, AppSettings& out);

} // namespace snippy
