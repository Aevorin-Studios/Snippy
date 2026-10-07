#pragma once

//
// tray_icon.h — System tray integration.
//
// Registers a notification area icon and context menu.
//

#include <windows.h>
#include <shellapi.h>
#include <functional>
#include <string>

namespace snippy {

class TrayIcon {
public:
    bool Initialize(HINSTANCE hInstance, HWND msgHwnd, HICON hIcon) noexcept;
    void Shutdown() noexcept;
    void ShowContextMenu(HWND hwnd) noexcept;

    // Callbacks
    std::function<void()> OnScreenshot;
    std::function<void()> OnSettings;
    std::function<void()> OnCheckUpdates;
    std::function<void()> OnExit;

    void UpdateTooltip(const std::wstring& hotkeyStr) noexcept;
    void ShowBalloon(const std::wstring& title, const std::wstring& msg) noexcept;

    LRESULT HandleTrayMessage(WPARAM wParam, LPARAM lParam) noexcept;

    static constexpr UINT WM_TRAYICON       = WM_USER + 10;
    static constexpr UINT TRAY_UID          = 1;
    static constexpr UINT IDM_SCREENSHOT    = 1001;
    static constexpr UINT IDM_SETTINGS      = 1002;
    static constexpr UINT IDM_CHECK_UPDATES = 1003;
    static constexpr UINT IDM_EXIT          = 1004;

private:
    NOTIFYICONDATAW m_nid{};
    std::wstring    m_currentHotkeyStr = L"Alt + S";
    bool            m_added = false;
};

extern TrayIcon g_tray;

} // namespace snippy
