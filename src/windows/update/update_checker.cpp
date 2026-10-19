#include "update_checker.h"
#include "../render/d2d_context.h"
#include "../render/dwrite_context.h"
#include "../theme/theme_manager.h"
#include "../settings/settings_manager.h"
#include "../tray/tray_icon.h"
#include "../overlay/overlay_window.h"
#include "../utils/logger.h"

#include <wininet.h>
#include <dwmapi.h>
#include <shellapi.h>
#include <windowsx.h>
#include <wrl/client.h>
#include <sstream>
#include <vector>
#include <atomic>
#include <thread>
#include <mutex>
#include <algorithm>
#include <cmath>

#pragma comment(lib, "wininet.lib")

#ifndef DWMWA_WINDOW_CORNER_PREFERENCE
#define DWMWA_WINDOW_CORNER_PREFERENCE 33
#define DWMWCP_ROUND 2
#endif

#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif

using Microsoft::WRL::ComPtr;

namespace snippy {

static HINSTANCE s_hInstance = nullptr;
static std::atomic<bool> s_isChecking{ false };
static std::atomic<bool> s_periodicStop{ false };
static std::thread s_periodicThread;
static std::atomic<bool> s_hasAvailableUpdate{ false };
static std::wstring s_availableVersion;
static std::wstring s_availableDownloadUrl;
static std::wstring s_lastNotifiedVersion;
static std::mutex s_updateMutex;

bool UpdateChecker::IsRemoteVersionNewer(const std::wstring& localVer, int localBuild,
                                        const std::wstring& remoteVer, int remoteBuild) noexcept {
    return snippy::IsRemoteVersionNewer(localVer, localBuild, remoteVer, remoteBuild);
}

// ── HTTP & JSON Helpers ───────────────────────────────────────────────────────

static bool FetchUrlUtf8(const std::wstring& url, std::string& outBody) {
    HINTERNET hNet = InternetOpenW(L"Snippy-UpdateChecker", INTERNET_OPEN_TYPE_PRECONFIG, nullptr, nullptr, 0);
    if (!hNet) return false;

    DWORD flags = INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE | INTERNET_FLAG_SECURE;
    HINTERNET hUrl = InternetOpenUrlW(hNet, url.c_str(), nullptr, 0, flags, 0);
    if (!hUrl) {
        InternetCloseHandle(hNet);
        return false;
    }

    char buffer[8192];
    DWORD bytesRead = 0;
    outBody.clear();

    while (InternetReadFile(hUrl, buffer, sizeof(buffer), &bytesRead) && bytesRead > 0) {
        outBody.append(buffer, bytesRead);
    }

    InternetCloseHandle(hUrl);
    InternetCloseHandle(hNet);
    return !outBody.empty();
}

static std::wstring ExtractJsonString(const std::string& json, const std::string& key) {
    std::string needle = "\"" + key + "\"";
    size_t pos = json.find(needle);
    if (pos == std::string::npos) return L"";
    pos = json.find(':', pos + needle.length());
    if (pos == std::string::npos) return L"";
    pos = json.find('"', pos);
    if (pos == std::string::npos) return L"";
    size_t start = pos + 1;
    std::string result;
    bool escape = false;
    for (size_t i = start; i < json.length(); ++i) {
        char c = json[i];
        if (escape) {
            if (c == 'n') result += '\n';
            else if (c == 'r') result += '\r';
            else if (c == 't') result += '\t';
            else if (c == '\\') result += '\\';
            else if (c == '"') result += '"';
            else result += c;
            escape = false;
        } else if (c == '\\') {
            escape = true;
        } else if (c == '"') {
            break;
        } else {
            result += c;
        }
    }
    int wlen = MultiByteToWideChar(CP_UTF8, 0, result.c_str(), -1, nullptr, 0);
    if (wlen <= 1) return L"";
    std::wstring wres(wlen - 1, 0);
    MultiByteToWideChar(CP_UTF8, 0, result.c_str(), -1, &wres[0], wlen);
    return wres;
}

static int ExtractJsonInt(const std::string& json, const std::string& key) {
    std::string needle = "\"" + key + "\"";
    size_t pos = json.find(needle);
    if (pos == std::string::npos) return 0;
    pos = json.find(':', pos + needle.length());
    if (pos == std::string::npos) return 0;
    pos++;
    while (pos < json.length() && (json[pos] == ' ' || json[pos] == '\t' || json[pos] == '\r' || json[pos] == '\n' || json[pos] == '"')) {
        pos++;
    }
    size_t endPos = pos;
    while (endPos < json.length() && isdigit(static_cast<unsigned char>(json[endPos]))) {
        endPos++;
    }
    if (endPos > pos) {
        try {
            return std::stoi(json.substr(pos, endPos - pos));
        } catch (...) {}
    }
    return 0;
}

static bool ParseVersionJson(const std::string& json, VersionInfo& outInfo) {
    outInfo.version      = ExtractJsonString(json, "version");
    outInfo.build        = ExtractJsonInt(json, "build");
    outInfo.downloadUrl  = ExtractJsonString(json, "downloadUrl");
    return !outInfo.version.empty() && !outInfo.downloadUrl.empty();
}

// ── Silent Update Downloader ──────────────────────────────────────────────────

class SilentUpdateDownloader {
private:
    static inline std::atomic<bool> s_isUpdating{ false };

public:
    static void DownloadAndInstall(const std::wstring& targetVersion, const std::wstring& downloadUrl) {
        if (s_isUpdating.exchange(true)) {
            return;
        }

        std::thread([=]() {
            WCHAR tempPath[MAX_PATH]{};
            GetTempPathW(MAX_PATH, tempPath);
            std::wstring tempExe = std::wstring(tempPath) + L"Snippy_new.exe";
            std::wstring batchScript = std::wstring(tempPath) + L"update_Snippy.bat";

            WCHAR currentExe[MAX_PATH]{};
            GetModuleFileNameW(nullptr, currentExe, MAX_PATH);

            HINTERNET hNet = InternetOpenW(L"Snippy-UpdateChecker", INTERNET_OPEN_TYPE_PRECONFIG, nullptr, nullptr, 0);
            if (!hNet) {
                s_isUpdating.store(false);
                return;
            }

            DWORD flags = INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE | INTERNET_FLAG_SECURE;
            HINTERNET hUrl = InternetOpenUrlW(hNet, downloadUrl.c_str(), nullptr, 0, flags, 0);
            if (!hUrl) {
                InternetCloseHandle(hNet);
                s_isUpdating.store(false);
                return;
            }

            HANDLE hFile = CreateFileW(tempExe.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (hFile == INVALID_HANDLE_VALUE) {
                InternetCloseHandle(hUrl);
                InternetCloseHandle(hNet);
                s_isUpdating.store(false);
                return;
            }

            char buffer[8192];
            DWORD bytesRead = 0;
            while (InternetReadFile(hUrl, buffer, sizeof(buffer), &bytesRead) && bytesRead > 0) {
                DWORD written = 0;
                WriteFile(hFile, buffer, bytesRead, &written, nullptr);
            }

            CloseHandle(hFile);
            InternetCloseHandle(hUrl);
            InternetCloseHandle(hNet);

            // Delay installation until user dismisses any active overlay and is not in fullscreen/presentation
            while (true) {
                bool isSnippyActive = g_overlay.IsVisible();
                bool isUserGaming = false;

                QUERY_USER_NOTIFICATION_STATE userState{};
                if (SUCCEEDED(SHQueryUserNotificationState(&userState))) {
                    if (userState == QUNS_RUNNING_D3D_FULL_SCREEN || userState == QUNS_PRESENTATION_MODE || userState == QUNS_BUSY) {
                        isUserGaming = true;
                    }
                }

                if (!isSnippyActive && !isUserGaming) {
                    break;
                }

                Sleep(2000);
            }

            std::wstring batchContent =
                L"@echo off\r\n"
                L"timeout /t 2 /nobreak >nul\r\n"
                L":retry\r\n"
                L"copy /y \"" + tempExe + L"\" \"" + currentExe + L"\" >nul\r\n"
                L"if errorlevel 1 (\r\n"
                L"    timeout /t 1 /nobreak >nul\r\n"
                L"    goto retry\r\n"
                L")\r\n"
                L"if exist \"" + tempExe + L"\" del /f /q \"" + tempExe + L"\" >nul\r\n"
                L"start \"\" \"" + currentExe + L"\" --updated \"" + targetVersion + L"\"\r\n"
                L"del \"%~f0\"\r\n";

            HANDLE hBatch = CreateFileW(batchScript.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (hBatch != INVALID_HANDLE_VALUE) {
                int len = WideCharToMultiByte(CP_ACP, 0, batchContent.c_str(), -1, nullptr, 0, nullptr, nullptr);
                std::string ansi(len, 0);
                WideCharToMultiByte(CP_ACP, 0, batchContent.c_str(), -1, &ansi[0], len, nullptr, nullptr);
                DWORD written = 0;
                WriteFile(hBatch, ansi.c_str(), static_cast<DWORD>(ansi.size() - 1), &written, nullptr);
                CloseHandle(hBatch);

                ShellExecuteW(nullptr, L"open", batchScript.c_str(), nullptr, nullptr, SW_HIDE);
                ExitProcess(0);
            }
        }).detach();
    }
};

// ── UpdateChecker Public Implementation ───────────────────────────────────────

void UpdateChecker::Initialize(HINSTANCE hInstance) noexcept {
    s_hInstance = hInstance;
}

void UpdateChecker::CheckForUpdates(bool isManualCheck) noexcept {
    if (s_isChecking.exchange(true)) return;

    std::thread([isManualCheck]() {
        Logger::LogInfo("Checking for updates...");
        std::string json;
        if (!FetchUrlUtf8(kDefaultUpdateUrl, json)) {
            s_isChecking.store(false);
            if (isManualCheck) {
                MessageBoxW(nullptr,
                            L"Could not check for updates.\n\nPlease check your internet connection.",
                            L"Update Check Failed", MB_OK | MB_ICONWARNING);
            }
            return;
        }

        VersionInfo info;
        if (!ParseVersionJson(json, info)) {
            s_isChecking.store(false);
            if (isManualCheck) {
                MessageBoxW(nullptr,
                            L"Unable to retrieve valid version information from update server.",
                            L"Update Error", MB_OK | MB_ICONWARNING);
            }
            return;
        }

        if (IsRemoteVersionNewer(kCurrentVersion, kCurrentBuild, info.version, info.build)) {
            char verBuf[64]{};
            std::wstring displayVer = info.GetDisplayString();
            WideCharToMultiByte(CP_UTF8, 0, displayVer.c_str(), -1, verBuf, sizeof(verBuf), nullptr, nullptr);
            Logger::LogInfo("New version available: v" + std::string(verBuf));

            {
                std::lock_guard<std::mutex> lock(s_updateMutex);
                s_availableVersion = displayVer;
                s_availableDownloadUrl = info.downloadUrl;
            }
            s_hasAvailableUpdate.store(true);

            if (isManualCheck) {
                g_tray.ShowBalloon(L"Updating Snippy",
                                   L"A new update v" + displayVer + L" was found and is being downloaded silently in the background...");
                SilentUpdateDownloader::DownloadAndInstall(displayVer, info.downloadUrl);
            } else {
                QUERY_USER_NOTIFICATION_STATE userState{};
                bool isUserGaming = false;
                if (SUCCEEDED(SHQueryUserNotificationState(&userState))) {
                    if (userState == QUNS_RUNNING_D3D_FULL_SCREEN || userState == QUNS_PRESENTATION_MODE || userState == QUNS_BUSY) {
                        isUserGaming = true;
                    }
                }

                if (!isUserGaming) {
                    SilentUpdateDownloader::DownloadAndInstall(displayVer, info.downloadUrl);
                }
            }
        } else if (isManualCheck) {
            std::wstring msg = L"Snippy is up to date!\n\nYou are running the latest version v" +
                               GetCurrentVersionDisplay() + L".";
            MessageBoxW(nullptr, msg.c_str(), L"Snippy", MB_OK | MB_ICONINFORMATION);
        }

        s_isChecking.store(false);
    }).detach();
}

bool UpdateChecker::HasAvailableUpdate() noexcept {
    return s_hasAvailableUpdate.load();
}

std::wstring UpdateChecker::GetAvailableVersion() noexcept {
    std::lock_guard<std::mutex> lock(s_updateMutex);
    return s_availableVersion;
}

std::wstring UpdateChecker::GetCurrentVersionDisplay() noexcept {
    if (kCurrentBuild > 0) {
        return std::wstring(kCurrentVersion) + L" (Build " + std::to_wstring(kCurrentBuild) + L")";
    }
    return std::wstring(kCurrentVersion);
}

void UpdateChecker::StartPeriodicCheck(int intervalMinutes) noexcept {
    s_periodicStop.store(false);
    s_periodicThread = std::thread([intervalMinutes]() {
        CheckForUpdates(false);

        while (!s_periodicStop.load()) {
            for (int i = 0; i < intervalMinutes * 60 && !s_periodicStop.load(); ++i) {
                Sleep(1000);
            }
            if (!s_periodicStop.load()) {
                CheckForUpdates(false);
            }
        }
    });
}

void UpdateChecker::CheckAndNotifyIfUpdated(HWND hwnd, const std::wstring& hotkeyStr) noexcept {
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv) return;

    bool isUpdated = false;
    std::wstring updatedVer;

    for (int i = 0; i < argc; ++i) {
        if (_wcsicmp(argv[i], L"--updated") == 0) {
            isUpdated = true;
            if (i + 1 < argc && argv[i + 1][0] != L'-') {
                updatedVer = argv[i + 1];
            }
        }
    }
    LocalFree(argv);

    if (isUpdated) {
        // Synchronize Windows Installed Apps / Programs registry metadata
        if (!updatedVer.empty()) {
            HKEY hKey = nullptr;
            LPCWSTR subkey = L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\{9A8B7C12-3D4E-5F6A-7B8C-9D0E1F2A3B4C}_is1";
            if (RegOpenKeyExW(HKEY_CURRENT_USER, subkey, 0, KEY_SET_VALUE, &hKey) == ERROR_SUCCESS) {
                std::wstring nameVal = L"Snippy version " + updatedVer;
                RegSetValueExW(hKey, L"DisplayName", 0, REG_SZ,
                               reinterpret_cast<const BYTE*>(nameVal.c_str()),
                               static_cast<DWORD>((nameVal.size() + 1) * sizeof(WCHAR)));
                RegSetValueExW(hKey, L"DisplayVersion", 0, REG_SZ,
                               reinterpret_cast<const BYTE*>(updatedVer.c_str()),
                               static_cast<DWORD>((updatedVer.size() + 1) * sizeof(WCHAR)));
                RegCloseKey(hKey);
            }
        }
    }
}

void UpdateChecker::Shutdown() noexcept {
    s_periodicStop.store(true);
    if (s_periodicThread.joinable()) {
        s_periodicThread.join();
    }
}

} // namespace snippy
