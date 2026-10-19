#pragma once

//
// d2d_context.h — Direct2D + Direct3D11 device singleton.
//
// Initialized once at startup. All rendering subsystems share these objects.
// Never recreated during normal operation (only on device lost).
//

#include <windows.h>
#include <d3d11.h>
#include <d2d1_3.h>
#include <d2d1_3helper.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

using Microsoft::WRL::ComPtr;

namespace snippy {

class D2DContext {
public:
    bool Initialize() noexcept;

    // Recreate after device lost (IDXGIDevice error).
    bool Reinitialize() noexcept;

    void Shutdown() noexcept;

    // Purges transient driver caches and intermediate surface allocations
    void Trim() noexcept;

    // ── Accessors ─────────────────────────────────────────────────────────────
    [[nodiscard]] ID3D11Device*          D3DDevice()       const noexcept { return m_d3dDevice.Get(); }
    [[nodiscard]] ID3D11DeviceContext*   D3DContext()      const noexcept { return m_d3dContext.Get(); }
    [[nodiscard]] IDXGIFactory2*         DXGIFactory()     const noexcept { return m_dxgiFactory.Get(); }
    [[nodiscard]] IDXGIAdapter*          DXGIAdapter()     const noexcept { return m_dxgiAdapter.Get(); }
    [[nodiscard]] ID2D1Factory3*         D2DFactory()      const noexcept { return m_d2dFactory.Get(); }
    [[nodiscard]] ID2D1Device2*          D2DDevice()       const noexcept { return m_d2dDevice.Get(); }
    [[nodiscard]] ID2D1DeviceContext2*   D2DDevCtx()       const noexcept { return m_d2dDevCtx.Get(); }
    [[nodiscard]] IDXGIDevice*           DXGIDevice()      const noexcept { return m_dxgiDevice.Get(); }

    [[nodiscard]] bool IsInitialized() const noexcept { return m_initialized; }
    [[nodiscard]] float DpiScale()     const noexcept { return m_dpiScale; }

private:
    bool CreateD3DDevice() noexcept;
    bool CreateD2DResources() noexcept;

    ComPtr<ID3D11Device>         m_d3dDevice;
    ComPtr<ID3D11DeviceContext>  m_d3dContext;
    ComPtr<IDXGIDevice>          m_dxgiDevice;
    ComPtr<IDXGIAdapter>         m_dxgiAdapter;
    ComPtr<IDXGIFactory2>        m_dxgiFactory;
    ComPtr<ID2D1Factory3>        m_d2dFactory;
    ComPtr<ID2D1Device2>         m_d2dDevice;
    ComPtr<ID2D1DeviceContext2>  m_d2dDevCtx;
    float                        m_dpiScale   = 1.0f;
    bool                         m_initialized = false;
};

extern D2DContext g_d2d;

} // namespace snippy
