#pragma once

//
// app.h — Application singleton. Owns the startup sequence and hot path dispatch.
//

#include <windows.h>
#include <atomic>

namespace snippy {

class App {
public:
    bool Initialize(HINSTANCE hInstance) noexcept;
    int  Run() noexcept;
    void Shutdown() noexcept;

    // Called from hotkey thread — fast signal to main loop
    void OnHotkey() noexcept;
    void OpenSettings() noexcept;

    HWND MessageHwnd() const noexcept { return m_msgHwnd; }
    bool IsScreenshotPending() const noexcept { return m_screenshotPending.load(std::memory_order_relaxed); }

    static void TrimMemory() noexcept;
    static void ScheduleTrimMemory(DWORD delayMs = 500) noexcept;

private:
    static LRESULT CALLBACK MsgWndProc(HWND, UINT, WPARAM, LPARAM) noexcept;
    LRESULT HandleMsgWindow(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) noexcept;

    void ApplySettings() noexcept;

    void DoScreenshot() noexcept;
    void OnSelectionConfirmed(RECT sel) noexcept;
    void OnSelectionCancelled() noexcept;

    HINSTANCE     m_hInstance = nullptr;
    HWND          m_msgHwnd   = nullptr;
    HICON         m_hIcon     = nullptr;

    std::atomic<bool> m_screenshotPending{ false };

    static constexpr UINT WM_SNIPPY_HOTKEY  = WM_USER + 1;
    static constexpr UINT WM_SNIPPY_CONFIRM = WM_USER + 2;
};

extern App g_app;

} // namespace snippy
