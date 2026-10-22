#include "app.h"
#include "render/d2d_context.h"
#include "render/dwrite_context.h"
#include "capture/dxgi_capture.h"
#include "overlay/overlay_window.h"
#include "clipboard/clipboard.h"
#include "hotkey/hotkey_manager.h"
#include "tray/tray_icon.h"
#include "perf/perf_counter.h"
#include "settings/settings_manager.h"
#include "settings/settings_window.h"
#include "theme/theme_manager.h"
#include "utils/logger.h"
#include "update/update_checker.h"
#include "resources/resource.h"
#include "voice/voice_activation.h"
#include <cstring>
#include <wchar.h>
#include <malloc.h>
#include <thread>

namespace snippy {

App g_app;

// Saved selection for post-overlay processing
static RECT s_capturedRegion{};
static DxgiCapture* s_activeCapture = nullptr;

// ── Initialize ────────────────────────────────────────────────────────────────

bool App::Initialize(HINSTANCE hInstance) noexcept {
    m_hInstance = hInstance;

    // ── 0. Load User Settings ─────────────────────────────────────────────────
    SettingsManager::LoadSettings();
    const auto& settings = SettingsManager::Current();

    // ── Debug Logger (Console + File) ─────────────────────────────────────────
    Logger::Init(settings.enableDebugLogger);
    Logger::LogInfo("Snippy Core Engine initialized");

    // ── 1. Performance counter (QPC frequency, zero-cost in release) ──────────
    PERF_INIT();

    // ── 2. COM (multi-threaded apartment — D2D, WIC, DirectComposition) ───────
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);

    // ── 3. Process DPI awareness & priority ──────────────────────────────────
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    SetPriorityClass(GetCurrentProcess(), ABOVE_NORMAL_PRIORITY_CLASS);
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);

    // ── 4. D3D11 + D2D + DXGI ─────────────────────────────────────────────────
    if (!g_d2d.Initialize()) { MessageBoxW(nullptr, L"D2D Init Failed", L"Error", MB_OK); return false; }

    // ── 5. DirectWrite + pre-load fonts ───────────────────────────────────────
    if (!g_dwrite.Initialize()) { MessageBoxW(nullptr, L"DWrite Init Failed", L"Error", MB_OK); return false; }

    // ── 6. DXGI capture — pre-warm all monitors ───────────────────────────────
    if (!g_capture.Initialize(g_d2d.D3DDevice())) { MessageBoxW(nullptr, L"DXGI Capture Init Failed", L"Error", MB_OK); return false; }

    // ── 7. Message-only HWND (for tray messages and WM_COMMAND) ──────────────
    // Note: HWND_MESSAGE fails on some GPU driver configs (GLE=1400).
    // A regular hidden popup window works identically for message dispatch.
    WNDCLASSEXW wc{};
    wc.cbSize        = sizeof(wc);
    wc.lpfnWndProc   = MsgWndProc;
    wc.hInstance     = hInstance;
    wc.lpszClassName = L"SnippyMsg";
    if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        WCHAR msg[128];
        swprintf_s(msg, L"RegisterClassExW (SnippyMsg) failed. GLE=%lu", GetLastError());
        MessageBoxW(nullptr, msg, L"Snippy Init Error", MB_OK | MB_ICONERROR);
        return false;
    }

    m_msgHwnd = CreateWindowExW(
        WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW,
        L"SnippyMsg", nullptr,
        WS_POPUP,
        0, 0, 1, 1,
        nullptr, nullptr,
        hInstance, this
    );
    if (!m_msgHwnd) {
        WCHAR msg[128];
        swprintf_s(msg, L"CreateWindowExW (msg HWND) failed. GLE=%lu", GetLastError());
        MessageBoxW(nullptr, msg, L"Snippy Init Error", MB_OK | MB_ICONERROR);
        return false;
    }
    // Keep it hidden — it only receives messages
    ShowWindow(m_msgHwnd, SW_HIDE);

    // ── 8. Load icon ──────────────────────────────────────────────────────────
    m_hIcon = LoadIconW(hInstance, MAKEINTRESOURCEW(IDI_SNIPPY_ICON));
    if (!m_hIcon) {
        // Fallback to system default icon
        m_hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    }

    // ── 9. Overlay window (pre-created, hidden) ───────────────────────────────
    if (!g_overlay.Initialize(hInstance)) { MessageBoxW(nullptr, L"Overlay Init Failed", L"Error", MB_OK); return false; }

    g_overlay.OnConfirm = [this](RECT sel) {
        s_capturedRegion = sel;
        // Post to main message loop — avoid processing on overlay's message context
        PostMessageW(m_msgHwnd, WM_SNIPPY_CONFIRM, 0, 0);
    };
    g_overlay.OnCancel = [this]() {
        OnSelectionCancelled();
    };
    g_overlay.OnRequestNewSnip = [this]() {
        DoScreenshot();
    };

    // ── 10. System tray icon ──────────────────────────────────────────────────
    g_tray.OnScreenshot   = [this]() { OnHotkey(); };
    g_tray.OnSettings     = [this]() { OpenSettings(); };
    g_tray.OnCheckUpdates = []()     { UpdateChecker::CheckForUpdates(true); };
    g_tray.OnExit         = [this]() { PostQuitMessage(0); };
    g_tray.Initialize(hInstance, m_msgHwnd, m_hIcon);

    std::wstring hkStr = SettingsManager::FormatHotkey(
        settings.altModifier, settings.ctrlModifier,
        settings.shiftModifier, settings.winModifier, settings.vk
    );
    g_tray.UpdateTooltip(hkStr);

    // ── 11. Settings window (pre-created, hidden) ─────────────────────────────
    g_settingsWindow.Initialize(hInstance);
    g_settingsWindow.OnSettingsChanged = [this]() { ApplySettings(); };

    // ── 12. Global hotkey — last step, all resources ready ────────────────────
    UINT modFlags = SettingsManager::GetWin32Modifiers(
        settings.altModifier, settings.ctrlModifier,
        settings.shiftModifier, settings.winModifier
    );
    if (!g_hotkey.Initialize(modFlags, settings.vk, [this]() { OnHotkey(); })) {
        OutputDebugStringA("[Snippy] WARNING: Failed to register hotkey (may already be in use)\n");
    }

    // ── 13. Update checker startup check & periodic background timer ──────────
    UpdateChecker::Initialize(hInstance);
    UpdateChecker::CheckAndNotifyIfUpdated(m_msgHwnd, hkStr);
    UpdateChecker::StartPeriodicCheck(5);

    g_voiceActivation.SetKeyword(settings.voiceKeyword);
    g_voiceActivation.Initialize([this]() { OnHotkey(); });
    if (settings.enableVoiceActivation) {
        g_voiceActivation.Start();
    }

    // Initial trim: flush pre-warming caches so Snippy starts extremely lean in tray (~2-4 MB)
    ScheduleTrimMemory(1000);

    return true;
}

// ── Hotkey dispatch ───────────────────────────────────────────────────────────

void App::OnHotkey() noexcept {
    // This runs on the hotkey thread — must be fast.
    // We can't call D2D or Win32 UI from here directly on the hotkey thread.
    // Post to main thread message loop.
    if (m_screenshotPending.exchange(true, std::memory_order_acquire)) return;
    PostMessageW(m_msgHwnd, WM_SNIPPY_HOTKEY, 0, 0);
}

// ── Screenshot flow ───────────────────────────────────────────────────────────

void App::DoScreenshot() noexcept {
    m_screenshotPending.store(false, std::memory_order_release);

    static uint64_t s_lastShotTime = 0;
    uint64_t now = GetTickCount64();
    if (now - s_lastShotTime < 50) return;
    s_lastShotTime = now;

    if (g_overlay.IsVisible()) {
        g_overlay.Hide();
        // Give DWM desktop window manager a moment to update composition
        Sleep(20);
    }

    if (s_activeCapture) {
        s_activeCapture->ReleaseFrame();
        s_activeCapture = nullptr;
    }

    PERF_MARK(t_capture_begin);

    // Find capture for the monitor under the cursor
    s_activeCapture = g_capture.CaptureMonitorUnderCursor();
    if (!s_activeCapture) return;

    // Acquire frame — may retry on TIMEOUT (screen hasn't changed)
    CaptureResult result = s_activeCapture->AcquireFrame(200);
    if (result == CaptureResult::AccessLost) {
        s_activeCapture->Reinitialize();
        result = s_activeCapture->AcquireFrame(200);
    }
    if (result != CaptureResult::Ok) return;

    PERF_MARK(t_capture_done);

    const FrameBuffer& fb     = s_activeCapture->StagingBuffer();
    const MonitorInfo& monitor = s_activeCapture->Monitor();

    // Show overlay — ShowWindow + upload desktop bitmap + first render
    g_overlay.Show(fb, monitor);
    // NOTE: ReleaseFrame() is called AFTER clipboard copy in OnSelectionConfirmed
}

void App::OnSelectionConfirmed(RECT sel) noexcept {
    if (!s_activeCapture) return;

    const FrameBuffer& fb = s_activeCapture->StagingBuffer();

    PERF_MARK(t_crop_done);

    // ── CF_DIB clipboard (synchronous, zero-encode) ───────────────────────────
    Clipboard::WriteDIB(fb, sel, m_msgHwnd);
    PERF_MARK(t_clipboard_done);

    // Make a private copy of the cropped region for the editor
    FrameBuffer view = fb.SubRegion(sel);
    std::vector<uint8_t> pixelCopy;
    if (view.IsValid()) {
        pixelCopy.resize(static_cast<size_t>(view.width) * view.height * 4);
        for (UINT row = 0; row < view.height; ++row) {
            std::memcpy(
                pixelCopy.data() + static_cast<size_t>(row) * view.width * 4,
                view.RowPtr(row),
                static_cast<size_t>(view.width) * 4
            );
        }
    }

    // Release the DXGI frame NOW (staging texture freed for next capture)
    s_activeCapture->ReleaseFrame();
    s_activeCapture = nullptr;

    // ── CF_PNG async encode (non-blocking) ────────────────────────────────────
    if (!pixelCopy.empty()) {
        FrameBuffer tempFb{
            pixelCopy.data(),
            view.width * 4,
            view.width,
            view.height
        };
        RECT full{ 0, 0, static_cast<LONG>(view.width), static_cast<LONG>(view.height) };
        Clipboard::WritePNGAsync(tempFb, full);
    }

    // Schedule working set trim after PNG async encode finishes returning to idle
    ScheduleTrimMemory(600);
}

void App::OnSelectionCancelled() noexcept {
    if (s_activeCapture) {
        s_activeCapture->ReleaseFrame();
        s_activeCapture = nullptr;
    }
    m_screenshotPending.store(false, std::memory_order_release);
    ScheduleTrimMemory(300);
}

// ── Message loop ──────────────────────────────────────────────────────────────

int App::Run() noexcept {
    MSG msg{};
    while (GetMessage(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    return static_cast<int>(msg.wParam);
}

// ── Message-only window procedure ─────────────────────────────────────────────

LRESULT CALLBACK App::MsgWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) noexcept {
    App* self = nullptr;
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = static_cast<App*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    } else {
        self = reinterpret_cast<App*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }
    if (self) return self->HandleMsgWindow(hwnd, msg, wParam, lParam);
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

void App::OpenSettings() noexcept {
    g_settingsWindow.Show();
}

void App::ApplySettings() noexcept {
    const auto& s = SettingsManager::Current();

    Logger::ApplySettings(s.enableDebugLogger);
    Logger::LogInfo("Settings applied from Settings window");

    UINT modFlags = SettingsManager::GetWin32Modifiers(
        s.altModifier, s.ctrlModifier, s.shiftModifier, s.winModifier
    );
    g_hotkey.UpdateHotkey(modFlags, s.vk);

    std::wstring hkStr = SettingsManager::FormatHotkey(
        s.altModifier, s.ctrlModifier, s.shiftModifier, s.winModifier, s.vk
    );
    g_tray.UpdateTooltip(hkStr);

    g_voiceActivation.SetKeyword(s.voiceKeyword);
    if (s.enableVoiceActivation != g_voiceActivation.IsRunning()) {
        if (s.enableVoiceActivation) {
            g_voiceActivation.Start();
        } else {
            g_voiceActivation.Stop();
        }
    }

    g_overlay.ReloadSettings();
}

LRESULT App::HandleMsgWindow(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) noexcept {
    switch (msg) {
    case WM_SNIPPY_HOTKEY:
        Logger::LogEvent("Snipping hotkey triggered");
        DoScreenshot();
        return 0;

    case WM_SNIPPY_CONFIRM:
        Logger::LogEvent("Selection confirmed -> copied to clipboard");
        OnSelectionConfirmed(s_capturedRegion);
        return 0;

    case TrayIcon::WM_TRAYICON:
        g_tray.HandleTrayMessage(wParam, lParam);
        return 0;

    case WM_DISPLAYCHANGE:
        Logger::LogInfo("Display configuration changed, refreshing capture devices in background");
        g_capture.Shutdown();
        g_capture.Initialize(g_d2d.D3DDevice());
        return 0;

    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case TrayIcon::IDM_SCREENSHOT:
            Logger::LogEvent("Tray menu screenshot requested");
            OnHotkey();
            break;
        case TrayIcon::IDM_SETTINGS:
            Logger::LogInfo("Opening Settings Window from tray");
            OpenSettings();
            break;
        case TrayIcon::IDM_EXIT:
            Logger::LogInfo("Tray menu exit requested");
            PostQuitMessage(0);
            break;
        }
        return 0;

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

// ── Shutdown ──────────────────────────────────────────────────────────────────

void App::Shutdown() noexcept {
    Logger::LogInfo("Snippy shutting down...");
    g_voiceActivation.Shutdown();
    UpdateChecker::Shutdown();
    g_settingsWindow.Shutdown();
    g_hotkey.Shutdown();
    g_tray.Shutdown();
    g_overlay.Shutdown();
    g_capture.Shutdown();
    g_dwrite.Shutdown();
    g_d2d.Shutdown();
    Logger::Shutdown();
    CoUninitialize();
}

// ── Memory Management ─────────────────────────────────────────────────────────

void App::TrimMemory() noexcept {
    // If the overlay is currently visible or a capture is pending, never trim
    if (g_overlay.IsVisible() || g_app.IsScreenshotPending()) return;

    // Release uncommitted C++ CRT heap memory back to the operating system
    _heapmin();

    // Release inactive working set pages to drop RAM usage in Task Manager down to ~2-4 MB.
    // Because we keep thread priority TIME_CRITICAL, eliminate disk I/O, and keep GPU shaders warm,
    // subsequent wakeup is instant (soft page fault from standby memory takes < 0.001 ms).
    SetProcessWorkingSetSize(GetCurrentProcess(), static_cast<SIZE_T>(-1), static_cast<SIZE_T>(-1));
}

void App::ScheduleTrimMemory(DWORD delayMs) noexcept {
    std::thread([delayMs]() {
        Sleep(delayMs);
        TrimMemory();
    }).detach();
}

} // namespace snippy
