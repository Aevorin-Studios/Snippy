#include "dxgi_capture.h"
#include <dxgi1_6.h>
#include <vector>
#include <algorithm>

namespace snippy {

CaptureManager g_capture;

// ── DxgiCapture ──────────────────────────────────────────────────────────────

bool DxgiCapture::Initialize(ID3D11Device* d3dDevice, const MonitorInfo& monitor) noexcept {
    m_d3dDevice = d3dDevice;
    m_monitor   = monitor;

    UINT w = static_cast<UINT>(monitor.monitorRect.right  - monitor.monitorRect.left);
    UINT h = static_cast<UINT>(monitor.monitorRect.bottom - monitor.monitorRect.top);
    if (w == 0 || h == 0) return false;

    HDC hdcScreen = GetDC(nullptr);
    if (!hdcScreen) return false;

    m_hdcMem = CreateCompatibleDC(hdcScreen);
    if (!m_hdcMem) {
        ReleaseDC(nullptr, hdcScreen);
        return false;
    }

    BITMAPINFO bmi{};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = static_cast<LONG>(w);
    bmi.bmiHeader.biHeight = -static_cast<LONG>(h); // top-down
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    m_hBitmap = CreateDIBSection(m_hdcMem, &bmi, DIB_RGB_COLORS, &m_dibBits, nullptr, 0);
    ReleaseDC(nullptr, hdcScreen);

    if (!m_hBitmap || !m_dibBits) {
        if (m_hdcMem) { DeleteDC(m_hdcMem); m_hdcMem = nullptr; }
        return false;
    }

    m_oldBitmap = SelectObject(m_hdcMem, m_hBitmap);
    m_stagingWidth  = w;
    m_stagingHeight = h;
    m_initialized = true;
    return true;
}

CaptureResult DxgiCapture::AcquireFrame(UINT /*timeoutMs*/) noexcept {
    if (!m_initialized || !m_hdcMem || !m_dibBits) return CaptureResult::Error;

    int x = m_monitor.monitorRect.left;
    int y = m_monitor.monitorRect.top;
    int w = m_monitor.monitorRect.right - m_monitor.monitorRect.left;
    int h = m_monitor.monitorRect.bottom - m_monitor.monitorRect.top;

    if (w <= 0 || h <= 0) return CaptureResult::Error;

    if (static_cast<UINT>(w) != m_stagingWidth || static_cast<UINT>(h) != m_stagingHeight) {
        Reinitialize();
        if (!m_initialized) return CaptureResult::Error;
    }

    HDC hdcScreen = GetDC(nullptr);
    if (!hdcScreen) return CaptureResult::Error;

    BOOL ok = BitBlt(m_hdcMem, 0, 0, w, h, hdcScreen, x, y, SRCCOPY | CAPTUREBLT);
    ReleaseDC(nullptr, hdcScreen);

    if (!ok) return CaptureResult::Error;

    m_frameAcquired = true;
    m_frameBuffer = FrameBuffer{
        .data   = static_cast<const uint8_t*>(m_dibBits),
        .stride = m_stagingWidth * 4,
        .width  = m_stagingWidth,
        .height = m_stagingHeight,
    };

    return CaptureResult::Ok;
}

void DxgiCapture::ReleaseFrame() noexcept {
    m_frameBuffer = {};
    m_frameAcquired = false;
}

bool DxgiCapture::Reinitialize() noexcept {
    Shutdown();
    return Initialize(m_d3dDevice.Get(), m_monitor);
}

void DxgiCapture::Shutdown() noexcept {
    m_frameBuffer = {};
    m_frameAcquired = false;
    if (m_hdcMem) {
        if (m_oldBitmap) SelectObject(m_hdcMem, m_oldBitmap);
        DeleteDC(m_hdcMem);
        m_hdcMem = nullptr;
    }
    if (m_hBitmap) {
        DeleteObject(m_hBitmap);
        m_hBitmap = nullptr;
    }
    m_oldBitmap = nullptr;
    m_dibBits = nullptr;
    m_d3dDevice.Reset();
    m_initialized = false;
}

// ── CaptureManager ───────────────────────────────────────────────────────────

struct EnumData {
    std::vector<MonitorInfo>* monitors;
};

BOOL CALLBACK CaptureManager::MonitorEnumProc(
    HMONITOR hmon, HDC, LPRECT lpRect, LPARAM lParam) noexcept
{
    auto* ed = reinterpret_cast<EnumData*>(lParam);

    MonitorInfo info{};
    info.handle      = hmon;
    info.monitorRect = *lpRect;

    MONITORINFOEXW mi{};
    mi.cbSize = sizeof(mi);
    GetMonitorInfoW(hmon, &mi);
    wcsncpy_s(info.deviceName, mi.szDevice, CCHDEVICENAME);

    ed->monitors->push_back(info);
    return TRUE;
}

bool CaptureManager::Initialize(ID3D11Device* d3dDevice) noexcept {
    m_d3dDevice = d3dDevice;

    EnumData ed{ &m_monitors };
    EnumDisplayMonitors(nullptr, nullptr, MonitorEnumProc, reinterpret_cast<LPARAM>(&ed));

    if (m_monitors.empty()) return false;

    m_captures.resize(m_monitors.size());
    for (size_t i = 0; i < m_monitors.size(); ++i) {
        if (!m_captures[i].Initialize(d3dDevice, m_monitors[i])) {
            // Non-fatal: some monitors may fail (e.g. secondary GPU)
            // Continue — at least the primary will succeed
        }
    }

    return true;
}

void CaptureManager::Shutdown() noexcept {
    for (auto& c : m_captures) c.Shutdown();
    m_captures.clear();
    m_monitors.clear();
}

DxgiCapture* CaptureManager::CaptureMonitorUnderCursor() noexcept {
    POINT cursor{};
    GetCursorPos(&cursor);

    HMONITOR hmon = MonitorFromPoint(cursor, MONITOR_DEFAULTTOPRIMARY);
    return CaptureForMonitor(hmon);
}

DxgiCapture* CaptureManager::CaptureForMonitor(HMONITOR hmon) noexcept {
    for (size_t i = 0; i < m_monitors.size(); ++i) {
        if (m_monitors[i].handle == hmon && m_captures[i].IsInitialized()) {
            return &m_captures[i];
        }
    }
    // Fallback: first initialized capture
    for (auto& c : m_captures) {
        if (c.IsInitialized()) return &c;
    }
    // If none initialized (e.g. after system monitor sleep/wake), re-initialize now
    if (m_d3dDevice) {
        Shutdown();
        Initialize(m_d3dDevice);
        for (size_t i = 0; i < m_monitors.size(); ++i) {
            if (m_monitors[i].handle == hmon && m_captures[i].IsInitialized()) {
                return &m_captures[i];
            }
        }
        for (auto& c : m_captures) {
            if (c.IsInitialized()) return &c;
        }
    }
    return nullptr;
}

} // namespace snippy
