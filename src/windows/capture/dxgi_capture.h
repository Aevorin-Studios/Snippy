#pragma once

//
// dxgi_capture.h — DXGI Desktop Duplication capture engine.
//
// One DxgiCapture instance per monitor, all pre-initialized at startup.
// AcquireFrame() is the ONLY call on the hot path — everything else is pre-warmed.
//
// Memory model:
//   m_stagingTexture is allocated at max monitor resolution at startup.
//   AcquireFrame() never allocates — it reuses the staging texture.
//   The FrameBuffer returned is a view over the mapped staging texture memory.
//   Caller MUST call ReleaseFrame() before the next AcquireFrame().
//

#include <windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <wrl/client.h>
#include <vector>
#include "core/frame_buffer.h"

using Microsoft::WRL::ComPtr;

namespace snippy {

enum class CaptureResult {
    Ok,
    Timeout,       // No new frame ready (DXGI_ERROR_WAIT_TIMEOUT)
    AccessLost,    // Monitor mode changed, need to reinitialize
    Error,
};

struct MonitorInfo {
    HMONITOR handle      = nullptr;
    RECT     monitorRect = {};   // Virtual desktop coordinates
    UINT     adapterIdx  = 0;
    UINT     outputIdx   = 0;
    WCHAR    deviceName[CCHDEVICENAME] = {};
};

class DxgiCapture {
public:
    // Pre-warm: call once per monitor at startup.
    // d3dDevice must be the same device used for D2D rendering.
    bool Initialize(ID3D11Device* d3dDevice, const MonitorInfo& monitor) noexcept;

    // Called at hotkey time.
    // timeoutMs=0: return immediately if no new frame (DXGI_ERROR_WAIT_TIMEOUT → Timeout).
    // timeoutMs>0: wait up to N ms for a new frame.
    //
    // On Ok: StagingBuffer() is valid until ReleaseFrame() is called.
    // Zero dynamic allocations.
    [[nodiscard]] CaptureResult AcquireFrame(UINT timeoutMs = 100) noexcept;

    // Must be called after AcquireFrame()==Ok before calling AcquireFrame again.
    void ReleaseFrame() noexcept;

    // Valid only between AcquireFrame(Ok) and ReleaseFrame().
    [[nodiscard]] const FrameBuffer& StagingBuffer() const noexcept { return m_frameBuffer; }
    [[nodiscard]] const MonitorInfo& Monitor()       const noexcept { return m_monitor; }
    [[nodiscard]] bool IsInitialized()               const noexcept { return m_initialized; }

    // Re-create duplication interface (called after AccessLost).
    bool Reinitialize() noexcept;

    void Shutdown() noexcept;

private:
    bool CreateDuplication() noexcept;
    bool CreateStagingTexture(UINT width, UINT height) noexcept;

    ComPtr<ID3D11Device>             m_d3dDevice;
    ComPtr<IDXGIOutput1>             m_dxgiOutput;
    ComPtr<IDXGIOutputDuplication>   m_duplication;
    ComPtr<ID3D11Texture2D>          m_stagingTexture;  // pre-allocated, CPU-readable

    HDC                              m_hdcMem = nullptr;
    HBITMAP                          m_hBitmap = nullptr;
    void*                            m_dibBits = nullptr;
    HGDIOBJ                          m_oldBitmap = nullptr;

    MonitorInfo                      m_monitor;
    FrameBuffer                      m_frameBuffer;     // view into mapped staging texture
    D3D11_MAPPED_SUBRESOURCE         m_mapped{};
    bool                             m_frameMapped  = false;
    bool                             m_frameAcquired = false;
    bool                             m_initialized  = false;

    UINT                             m_stagingWidth  = 0;
    UINT                             m_stagingHeight = 0;
};

// ── Multi-monitor capture manager ────────────────────────────────────────────

class CaptureManager {
public:
    bool Initialize(ID3D11Device* d3dDevice) noexcept;
    void Shutdown() noexcept;

    // Capture the monitor containing the cursor
    DxgiCapture* CaptureMonitorUnderCursor() noexcept;
    DxgiCapture* CaptureForMonitor(HMONITOR hmon) noexcept;

    [[nodiscard]] size_t MonitorCount() const noexcept { return m_captures.size(); }

private:
    static BOOL CALLBACK MonitorEnumProc(HMONITOR, HDC, LPRECT, LPARAM) noexcept;

    std::vector<MonitorInfo>  m_monitors;
    std::vector<DxgiCapture>  m_captures;
    ID3D11Device*             m_d3dDevice = nullptr;
};

extern CaptureManager g_capture;

} // namespace snippy
