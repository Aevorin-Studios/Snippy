#pragma once

#include <windows.h>
#include <string>

namespace snippy {

struct VersionInfo {
    std::wstring version;
    int build = 0;
    std::wstring downloadUrl;

    std::wstring GetDisplayString() const {
        if (build > 0) {
            return version + L" (Build " + std::to_wstring(build) + L")";
        }
        return version;
    }
};

class UpdateChecker {
public:
    static constexpr const WCHAR* kCurrentVersion   = L"1.0.0";
    static constexpr int          kCurrentBuild     = 5;
    static constexpr const WCHAR* kDefaultUpdateUrl = L"https://raw.githubusercontent.com/Aevorin-Studios/Snippy-for-Windows/main/version.json";

    static void Initialize(HINSTANCE hInstance) noexcept;

    static void CheckForUpdates(bool isManualCheck = false) noexcept;
    static void StartPeriodicCheck(int intervalMinutes = 5) noexcept;
    static void CheckAndNotifyIfUpdated(HWND hwnd, const std::wstring& hotkeyStr) noexcept;
    static bool IsRemoteVersionNewer(const std::wstring& localVer, int localBuild,
                                     const std::wstring& remoteVer, int remoteBuild) noexcept;
    static void Shutdown() noexcept;

    static bool HasAvailableUpdate() noexcept;
    static std::wstring GetAvailableVersion() noexcept;
    static std::wstring GetCurrentVersionDisplay() noexcept;
};

} // namespace snippy
