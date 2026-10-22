#include "settings.h"
#include <sstream>
#include <algorithm>

namespace snippy {

static void ExtractJsonBool(const std::string& text, const char* key, bool& outVal) {
    size_t pos = text.find(key);
    if (pos == std::string::npos) return;
    pos = text.find(':', pos);
    if (pos == std::string::npos) return;
    pos++;
    while (pos < text.size() && (text[pos] == ' ' || text[pos] == '\t' || text[pos] == '\r' || text[pos] == '\n')) pos++;
    if (text.compare(pos, 4, "true") == 0) outVal = true;
    else if (text.compare(pos, 5, "false") == 0) outVal = false;
}

static void ExtractJsonInt(const std::string& text, const char* key, int& outVal) {
    size_t pos = text.find(key);
    if (pos == std::string::npos) return;
    pos = text.find(':', pos);
    if (pos == std::string::npos) return;
    pos++;
    while (pos < text.size() && (text[pos] == ' ' || text[pos] == '\t' || text[pos] == '\r' || text[pos] == '\n')) pos++;
    if (pos < text.size() && (isdigit(static_cast<unsigned char>(text[pos])) || text[pos] == '-')) {
        outVal = std::atoi(text.c_str() + pos);
    }
}

static void ExtractJsonString(const std::string& text, const char* key, std::wstring& outVal) {
    size_t pos = text.find(key);
    if (pos == std::string::npos) return;
    pos = text.find(':', pos);
    if (pos == std::string::npos) return;
    pos = text.find('\"', pos);
    if (pos == std::string::npos) return;
    size_t end = text.find('\"', pos + 1);
    if (end == std::string::npos) return;
    std::string s = text.substr(pos + 1, end - pos - 1);
    outVal = std::wstring(s.begin(), s.end());
}

static void ExtractJsonStringArray(const std::string& text, const char* key, std::vector<std::wstring>& outArray) {
    size_t pos = text.find(key);
    if (pos == std::string::npos) return;
    pos = text.find('[', pos);
    if (pos == std::string::npos) return;
    size_t end = text.find(']', pos);
    if (end == std::string::npos) return;
    outArray.clear();

    size_t cur = pos + 1;
    while (cur < end) {
        size_t q1 = text.find('\"', cur);
        if (q1 == std::string::npos || q1 >= end) break;
        size_t q2 = text.find('\"', q1 + 1);
        if (q2 == std::string::npos || q2 >= end) break;
        std::string item = text.substr(q1 + 1, q2 - q1 - 1);
        if (!item.empty()) {
            outArray.push_back(std::wstring(item.begin(), item.end()));
        }
        cur = q2 + 1;
    }
}

static std::string WideToNarrow(const std::wstring& w) {
    std::string s;
    s.reserve(w.size());
    for (wchar_t wc : w) {
        if (wc < 128) {
            s.push_back(static_cast<char>(wc));
        } else {
            s.push_back('?');
        }
    }
    return s;
}

std::string SerializeSettingsJson(const AppSettings& s) {
    std::string narrowKey     = WideToNarrow(s.keyName);
    std::string narrowTheme   = WideToNarrow(s.theme);
    std::string narrowPos     = WideToNarrow(s.toolbarPosition);
    std::string narrowAccent  = WideToNarrow(s.accentColor);
    std::string narrowVoiceKw = WideToNarrow(s.voiceKeyword.empty() ? L"Snippy snip it" : s.voiceKeyword);

    std::ostringstream ss;
    ss << "{\n";
    ss << "  \"AltModifier\": " << (s.altModifier ? "true" : "false") << ",\n";
    ss << "  \"CtrlModifier\": " << (s.ctrlModifier ? "true" : "false") << ",\n";
    ss << "  \"ShiftModifier\": " << (s.shiftModifier ? "true" : "false") << ",\n";
    ss << "  \"WinModifier\": " << (s.winModifier ? "true" : "false") << ",\n";
    ss << "  \"KeyName\": \"" << narrowKey << "\",\n";
    ss << "  \"Theme\": \"" << narrowTheme << "\",\n";
    ss << "  \"EnableVoiceActivation\": " << (s.enableVoiceActivation ? "true" : "false") << ",\n";
    ss << "  \"VoiceKeyword\": \"" << narrowVoiceKw << "\",\n";
    ss << "  \"EnableDebugLogger\": " << (s.enableDebugLogger ? "true" : "false") << ",\n";
    ss << "  \"AccentColor\": \"" << narrowAccent << "\",\n";
    ss << "  \"DimmingOpacity\": " << s.dimmingOpacity << ",\n";
    ss << "  \"ToolbarPosition\": \"" << narrowPos << "\",\n";
    ss << "  \"SelectionBorderThickness\": " << s.selectionBorderThickness << ",\n";
    ss << "  \"HiddenTools\": [";
    for (size_t i = 0; i < s.hiddenTools.size(); ++i) {
        std::string t = WideToNarrow(s.hiddenTools[i]);
        ss << "\"" << t << "\"" << (i + 1 < s.hiddenTools.size() ? ", " : "");
    }
    ss << "]\n";
    ss << "}\n";

    return ss.str();
}

bool DeserializeSettingsJson(const std::string& json, AppSettings& out) {
    if (json.empty()) return false;

    ExtractJsonBool(json, "\"AltModifier\"", out.altModifier);
    ExtractJsonBool(json, "\"CtrlModifier\"", out.ctrlModifier);
    ExtractJsonBool(json, "\"ShiftModifier\"", out.shiftModifier);
    ExtractJsonBool(json, "\"WinModifier\"", out.winModifier);
    ExtractJsonString(json, "\"KeyName\"", out.keyName);

    ExtractJsonString(json, "\"Theme\"", out.theme);
    ExtractJsonBool(json, "\"EnableVoiceActivation\"", out.enableVoiceActivation);
    ExtractJsonString(json, "\"VoiceKeyword\"", out.voiceKeyword);
    if (out.voiceKeyword.empty()) {
        out.voiceKeyword = L"Snippy snip it";
    }
    ExtractJsonBool(json, "\"EnableDebugLogger\"", out.enableDebugLogger);

    ExtractJsonString(json, "\"AccentColor\"", out.accentColor);
    ExtractJsonInt(json, "\"DimmingOpacity\"", out.dimmingOpacity);
    out.dimmingOpacity = std::max(0, std::min(out.dimmingOpacity, 100));

    ExtractJsonInt(json, "\"SelectionBorderThickness\"", out.selectionBorderThickness);
    out.selectionBorderThickness = std::max(1, std::min(out.selectionBorderThickness, 6));

    ExtractJsonString(json, "\"ToolbarPosition\"", out.toolbarPosition);
    ExtractJsonStringArray(json, "\"HiddenTools\"", out.hiddenTools);

    return true;
}

} // namespace snippy
