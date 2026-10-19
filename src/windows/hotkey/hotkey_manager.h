#pragma once

//
// hotkey_manager.h — Global hotkey registration on a dedicated thread.
//
// The dedicated thread runs its own message loop and does nothing except
// receive WM_HOTKEY messages. All actual work is delegated to the caller's
// callback, which is dispatched synchronously in the hotkey thread
// (but should itself dispatch to a worker thread to avoid blocking).
//

#include <windows.h>
#include <functional>
#include <thread>
#include <atomic>

namespace snippy {

using HotkeyCallback = std::function<void()>;

class HotkeyManager {
public:
    // Register a global hotkey and start the dedicated message pump thread.
    // callback: invoked on the hotkey thread when the hotkey fires.
    //           Must be fast (signal another thread, PostMessage, SetEvent).
    bool Initialize(UINT modifiers, UINT vk, HotkeyCallback callback) noexcept;
    bool UpdateHotkey(UINT modifiers, UINT vk) noexcept;

    void Shutdown() noexcept;

    [[nodiscard]] bool IsRegistered() const noexcept { return m_registered.load(std::memory_order_relaxed); }

private:
    static DWORD WINAPI ThreadProc(LPVOID param) noexcept;
    void RunMessageLoop() noexcept;

    HotkeyCallback      m_callback;
    std::thread         m_thread;
    HWND                m_msgHwnd   = nullptr;
    std::atomic<bool>   m_registered{ false };
    std::atomic<bool>   m_shutdown  { false };

    UINT m_modifiers = 0;
    UINT m_vk        = 0;

    static constexpr int kHotkeyId = 1;
    static constexpr int kPrintScreenHotkeyId = 2;
    static constexpr UINT WM_SNIPPY_SHUTDOWN      = WM_USER + 100;
    static constexpr UINT WM_SNIPPY_UPDATE_HOTKEY = WM_USER + 101;
};

extern HotkeyManager g_hotkey;

} // namespace snippy
