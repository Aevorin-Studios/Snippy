#include "logger.h"
#include <cstdio>
#include <vector>

namespace snippy {

bool Logger::s_initialized = false;
bool Logger::s_consoleEnabled = false;
CRITICAL_SECTION Logger::s_cs{};

std::wstring Logger::GetLogFilePath() noexcept {
    DWORD len = GetEnvironmentVariableW(L"APPDATA", nullptr, 0);
    if (len > 1) {
        std::vector<WCHAR> buf(len);
        if (GetEnvironmentVariableW(L"APPDATA", buf.data(), len) > 0) {
            std::wstring dir = std::wstring(buf.data()) + L"\\Snippy";
            CreateDirectoryW(dir.c_str(), nullptr);
            return dir + L"\\snippy_debug.log";
        }
    }
    return L"snippy_debug.log";
}

void Logger::EnableConsoleWindow() noexcept {
    if (s_consoleEnabled) return;

    if (AllocConsole()) {
        s_consoleEnabled = true;
        SetConsoleTitleW(L"Snippy - Debug Log");
        SetConsoleOutputCP(CP_UTF8);

        FILE* fp = nullptr;
        freopen_s(&fp, "CONOUT$", "w", stdout);
        freopen_s(&fp, "CONOUT$", "w", stderr);
        freopen_s(&fp, "CONIN$", "r", stdin);

        HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
        if (hOut != INVALID_HANDLE_VALUE) {
            DWORD mode = 0;
            if (GetConsoleMode(hOut, &mode)) {
                SetConsoleMode(hOut, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
            }
        }

        // Disable console window Close button to prevent killing whole app
        HWND hConsole = GetConsoleWindow();
        if (hConsole) {
            HMENU hMenu = GetSystemMenu(hConsole, FALSE);
            if (hMenu) {
                EnableMenuItem(hMenu, SC_CLOSE, MF_BYCOMMAND | MF_GRAYED);
            }
        }
    }
}

void Logger::DisableConsoleWindow() noexcept {
    if (!s_consoleEnabled) return;
    s_consoleEnabled = false;

    HWND hConsole = GetConsoleWindow();
    if (hConsole) {
        ShowWindow(hConsole, SW_HIDE);
    }

    FILE* fp = nullptr;
    freopen_s(&fp, "NUL:", "w", stdout);
    freopen_s(&fp, "NUL:", "w", stderr);
    freopen_s(&fp, "NUL:", "r", stdin);

    FreeConsole();

    if (hConsole) {
        PostMessageW(hConsole, WM_CLOSE, 0, 0);
    }
}

void Logger::Init(bool enableConsole) noexcept {
    if (s_initialized) return;
    s_initialized = true;
    InitializeCriticalSection(&s_cs);

    if (enableConsole) {
        EnableConsoleWindow();
    }

    LogInfo("Logger initialized");
    SYSTEMTIME st{};
    GetLocalTime(&st);
    char buf[64];
    sprintf_s(buf, "Snippy started at %04d-%02d-%02d %02d:%02d:%02d",
              st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    LogInfo(buf);
}

void Logger::ApplySettings(bool enableConsole) noexcept {
    if (enableConsole && !s_consoleEnabled) {
        EnableConsoleWindow();
        LogInfo("Debug Console Enabled from Settings");
    } else if (!enableConsole && s_consoleEnabled) {
        LogInfo("Disabling Debug Console...");
        DisableConsoleWindow();
    }
}

void Logger::Shutdown() noexcept {
    if (!s_initialized) return;
    if (s_consoleEnabled) {
        DisableConsoleWindow();
    }
    DeleteCriticalSection(&s_cs);
    s_initialized = false;
}

void Logger::Log(const char* level, const std::string& message) noexcept {
    if (!s_initialized) return;

    EnterCriticalSection(&s_cs);

    SYSTEMTIME st{};
    GetLocalTime(&st);
    char timeBuf[32];
    sprintf_s(timeBuf, "%02d:%02d:%02d.%03d", st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);

    std::string line = std::string("[") + timeBuf + "] [" + level + "] " + message + "\n";

    std::wstring logPath = GetLogFilePath();
    FILE* f = nullptr;
    if (_wfopen_s(&f, logPath.c_str(), L"ab") == 0 && f) {
        fputs(line.c_str(), f);
        fclose(f);
    }

    if (s_consoleEnabled) {
        HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
        if (hOut != INVALID_HANDLE_VALUE) {
            WORD color = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE;
            if (strcmp(level, "ERROR") == 0) {
                color = FOREGROUND_RED | FOREGROUND_INTENSITY;
            } else if (strcmp(level, "WARN ") == 0) {
                color = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_INTENSITY;
            } else if (strcmp(level, "INFO ") == 0) {
                color = FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY;
            } else if (strcmp(level, "EVENT") == 0) {
                color = FOREGROUND_GREEN | FOREGROUND_INTENSITY;
            }

            SetConsoleTextAttribute(hOut, FOREGROUND_INTENSITY);
            std::string prefix = std::string("[") + timeBuf + "] ";
            WriteConsoleA(hOut, prefix.c_str(), static_cast<DWORD>(prefix.size()), nullptr, nullptr);

            SetConsoleTextAttribute(hOut, color);
            std::string lvlStr = std::string("[") + level + "] ";
            WriteConsoleA(hOut, lvlStr.c_str(), static_cast<DWORD>(lvlStr.size()), nullptr, nullptr);

            SetConsoleTextAttribute(hOut, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY);
            std::string msgStr = message + "\n";
            WriteConsoleA(hOut, msgStr.c_str(), static_cast<DWORD>(msgStr.size()), nullptr, nullptr);
        }
    }

    OutputDebugStringA(line.c_str());

    LeaveCriticalSection(&s_cs);
}

void Logger::Log(const char* level, const std::wstring& message) noexcept {
    int size = WideCharToMultiByte(CP_UTF8, 0, message.data(), static_cast<int>(message.size()), nullptr, 0, nullptr, nullptr);
    if (size > 0) {
        std::string narrow(size, '\0');
        WideCharToMultiByte(CP_UTF8, 0, message.data(), static_cast<int>(message.size()), narrow.data(), size, nullptr, nullptr);
        Log(level, narrow);
    }
}

} // namespace snippy
