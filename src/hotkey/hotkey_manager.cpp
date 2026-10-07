#include "hotkey_manager.h"
#include "../perf/perf_counter.h"

namespace snippy {

HotkeyManager g_hotkey;

bool HotkeyManager::Initialize(UINT modifiers, UINT vk, HotkeyCallback callback) noexcept {
    m_modifiers = modifiers;
    m_vk        = vk;
    m_callback  = std::move(callback);

    m_thread = std::thread([this]() { RunMessageLoop(); });

    // Wait briefly for thread to register (it signals via m_registered)
    // Spin-wait with yield — typically < 1 ms
    for (int i = 0; i < 1000 && !m_registered.load(std::memory_order_acquire); ++i) {
        Sleep(1);
    }

    return m_registered.load(std::memory_order_acquire);
}

void HotkeyManager::RunMessageLoop() noexcept {
    // Set thread priority — hotkey response is latency-critical
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);

    // nullptr hwnd = posts to thread queue, which we pump below
    BOOL ok = RegisterHotKey(
        nullptr,            // nullptr = thread-queue based (no HWND needed)
        kHotkeyId,
        m_modifiers,
        m_vk
    );
    RegisterHotKey(nullptr, kPrintScreenHotkeyId, 0, VK_SNAPSHOT);

    m_registered.store(ok, std::memory_order_release);

    if (!ok) return;

    MSG msg{};
    while (!m_shutdown.load(std::memory_order_relaxed)) {
        BOOL ret = GetMessage(&msg, nullptr, 0, 0);
        if (ret == 0 || ret == -1) break;

        if (msg.message == WM_HOTKEY && (msg.wParam == kHotkeyId || msg.wParam == kPrintScreenHotkeyId)) {
            PERF_RESET();
            PERF_MARK(t_hotkey);
            // Invoke callback immediately — must be fast
            if (m_callback) m_callback();
        }
        else if (msg.message == WM_SNIPPY_UPDATE_HOTKEY) {
            UnregisterHotKey(nullptr, kHotkeyId);
            UINT newMods = static_cast<UINT>(msg.wParam);
            UINT newVk   = static_cast<UINT>(msg.lParam);
            BOOL updated = RegisterHotKey(nullptr, kHotkeyId, newMods, newVk);
            m_modifiers  = newMods;
            m_vk         = newVk;
            m_registered.store(updated != FALSE, std::memory_order_release);
        }
        if (msg.message == WM_QUIT) break;

        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    UnregisterHotKey(nullptr, kHotkeyId);
    UnregisterHotKey(nullptr, kPrintScreenHotkeyId);
}

bool HotkeyManager::UpdateHotkey(UINT modifiers, UINT vk) noexcept {
    if (!m_thread.joinable()) return false;
    m_registered.store(false, std::memory_order_release);
    PostThreadMessageW(GetThreadId(m_thread.native_handle()), WM_SNIPPY_UPDATE_HOTKEY, modifiers, vk);
    for (int i = 0; i < 50; ++i) {
        if (m_registered.load(std::memory_order_acquire)) return true;
        Sleep(2);
    }
    return m_registered.load(std::memory_order_acquire);
}

void HotkeyManager::Shutdown() noexcept {
    m_shutdown.store(true, std::memory_order_relaxed);
    // Post WM_QUIT to the hotkey thread to unblock GetMessage
    if (m_thread.joinable()) {
        PostThreadMessageW(GetThreadId(m_thread.native_handle()), WM_QUIT, 0, 0);
        m_thread.join();
    }
    m_registered.store(false, std::memory_order_relaxed);
}

} // namespace snippy
