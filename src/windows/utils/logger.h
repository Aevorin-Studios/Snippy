#pragma once

#include <windows.h>
#include <string>

namespace snippy {

class Logger {
public:
    static void Init(bool enableConsole) noexcept;
    static void ApplySettings(bool enableConsole) noexcept;
    static void Shutdown() noexcept;

    static void Log(const char* level, const std::string& message) noexcept;
    static void Log(const char* level, const std::wstring& message) noexcept;

    static void LogInfo(const std::string& msg) noexcept   { Log("INFO ", msg); }
    static void LogInfo(const std::wstring& msg) noexcept  { Log("INFO ", msg); }
    static void LogEvent(const std::string& msg) noexcept  { Log("EVENT", msg); }
    static void LogEvent(const std::wstring& msg) noexcept { Log("EVENT", msg); }
    static void LogWarn(const std::string& msg) noexcept   { Log("WARN ", msg); }
    static void LogWarn(const std::wstring& msg) noexcept  { Log("WARN ", msg); }
    static void LogError(const std::string& msg) noexcept  { Log("ERROR", msg); }
    static void LogError(const std::wstring& msg) noexcept { Log("ERROR", msg); }

    static bool IsConsoleEnabled() noexcept { return s_consoleEnabled; }

private:
    static void EnableConsoleWindow() noexcept;
    static void DisableConsoleWindow() noexcept;
    static std::wstring GetLogFilePath() noexcept;

    static bool s_initialized;
    static bool s_consoleEnabled;
    static CRITICAL_SECTION s_cs;
};

} // namespace snippy
