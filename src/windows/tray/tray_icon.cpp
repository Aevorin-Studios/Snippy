#include "tray_icon.h"
#include "../update/update_checker.h"
#include <string>

namespace snippy {

TrayIcon g_tray;

bool TrayIcon::Initialize(HINSTANCE hInstance, HWND msgHwnd, HICON hIcon) noexcept {
    m_nid.cbSize           = sizeof(m_nid);
    m_nid.hWnd             = msgHwnd;
    m_nid.uID              = TRAY_UID;
    m_nid.uFlags           = NIF_ICON | NIF_MESSAGE | NIF_TIP | NIF_SHOWTIP;
    m_nid.uCallbackMessage = WM_TRAYICON;
    m_nid.hIcon            = hIcon;
    wcscpy_s(m_nid.szTip, L"Snippy - Press Alt+S to capture");

    // Use NOTIFYICON_VERSION_4 for improved balloon/tooltip behavior
    Shell_NotifyIconW(NIM_ADD, &m_nid);
    m_nid.uVersion = NOTIFYICON_VERSION_4;
    Shell_NotifyIconW(NIM_SETVERSION, &m_nid);

    m_added = true;
    return true;
}

void TrayIcon::Shutdown() noexcept {
    if (m_added) {
        Shell_NotifyIconW(NIM_DELETE, &m_nid);
        m_added = false;
    }
}

void TrayIcon::UpdateTooltip(const std::wstring& hotkeyStr) noexcept {
    if (!m_added) return;
    m_currentHotkeyStr = hotkeyStr;
    std::wstring tip = L"Snippy - Press " + hotkeyStr + L" to Snip";
    wcsncpy_s(m_nid.szTip, _countof(m_nid.szTip), tip.c_str(), _TRUNCATE);
    m_nid.uFlags = NIF_TIP | NIF_SHOWTIP;
    Shell_NotifyIconW(NIM_MODIFY, &m_nid);
}

void TrayIcon::ShowBalloon(const std::wstring& title, const std::wstring& msg) noexcept {
    if (!m_added) return;
    m_nid.uFlags |= NIF_INFO;
    m_nid.dwInfoFlags = NIIF_INFO;
    wcsncpy_s(m_nid.szInfoTitle, _countof(m_nid.szInfoTitle), title.c_str(), _TRUNCATE);
    wcsncpy_s(m_nid.szInfo, _countof(m_nid.szInfo), msg.c_str(), _TRUNCATE);
    m_nid.uTimeout = 4000;
    Shell_NotifyIconW(NIM_MODIFY, &m_nid);
}

void TrayIcon::ShowContextMenu(HWND hwnd) noexcept {
    POINT pt{};
    GetCursorPos(&pt);

    HMENU hMenu = CreatePopupMenu();
    if (!hMenu) return;

    std::wstring snipLabel = L"New Snip (" + m_currentHotkeyStr + L")";
    AppendMenuW(hMenu, MF_STRING, IDM_SCREENSHOT,    snipLabel.c_str());
    AppendMenuW(hMenu, MF_STRING, IDM_SETTINGS,      L"Settings...");
    if (UpdateChecker::HasAvailableUpdate()) {
        std::wstring updateLabel = L"Update to v" + UpdateChecker::GetAvailableVersion() + L"...";
        AppendMenuW(hMenu, MF_STRING, IDM_CHECK_UPDATES, updateLabel.c_str());
    } else {
        AppendMenuW(hMenu, MF_STRING, IDM_CHECK_UPDATES, L"Check for Updates...");
    }
    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(hMenu, MF_STRING, IDM_EXIT,          L"Exit");

    // Required: set foreground window so menu dismisses on click-away
    SetForegroundWindow(hwnd);
    UINT cmd = TrackPopupMenu(hMenu, TPM_RIGHTALIGN | TPM_BOTTOMALIGN | TPM_RIGHTBUTTON | TPM_RETURNCMD,
                              pt.x, pt.y, 0, hwnd, nullptr);
    PostMessageW(hwnd, WM_NULL, 0, 0); // Standard Win32 fix for tray menu dismissal
    DestroyMenu(hMenu);

    if (cmd == IDM_SCREENSHOT) {
        if (OnScreenshot) OnScreenshot();
    } else if (cmd == IDM_SETTINGS) {
        if (OnSettings) OnSettings();
    } else if (cmd == IDM_CHECK_UPDATES) {
        if (OnCheckUpdates) OnCheckUpdates();
    } else if (cmd == IDM_EXIT) {
        if (OnExit) OnExit();
    }
}

LRESULT TrayIcon::HandleTrayMessage(WPARAM wParam, LPARAM lParam) noexcept {
    UINT event = LOWORD(lParam);
    switch (event) {
    case WM_RBUTTONUP:
    case WM_CONTEXTMENU:
    case NIN_KEYSELECT:
        ShowContextMenu(m_nid.hWnd);
        break;
    case WM_LBUTTONUP:
    case WM_LBUTTONDBLCLK:
    case NIN_SELECT:
        if (OnScreenshot) OnScreenshot();
        break;
    case NIN_BALLOONUSERCLICK:
        if (OnCheckUpdates) OnCheckUpdates();
        break;
    }
    return 0;
}

} // namespace snippy
