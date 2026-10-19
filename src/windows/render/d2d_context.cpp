#include "d2d_context.h"
#include <cassert>
#include <wchar.h>

namespace snippy {

D2DContext g_d2d;

bool D2DContext::Initialize() noexcept {
    if (m_initialized) return true;
    if (!CreateD3DDevice()) return false;
    if (!CreateD2DResources()) return false;
    m_initialized = true;
    return true;
}

bool D2DContext::Reinitialize() noexcept {
    Shutdown();
    return Initialize();
}

void D2DContext::Shutdown() noexcept {
    m_d2dDevCtx.Reset();
    m_d2dDevice.Reset();
    m_d2dFactory.Reset();
    m_dxgiFactory.Reset();
    m_dxgiAdapter.Reset();
    m_dxgiDevice.Reset();
    m_d3dContext.Reset();
    m_d3dDevice.Reset();
    m_initialized = false;
}

void D2DContext::Trim() noexcept {
    if (m_dxgiDevice) {
        ComPtr<IDXGIDevice3> dxgi3;
        if (SUCCEEDED(m_dxgiDevice.As(&dxgi3))) {
            dxgi3->Trim();
        }
    }
}

bool D2DContext::CreateD3DDevice() noexcept {
    // Feature levels — we require 11.0 for full D2D 1.3 interop
    static const D3D_FEATURE_LEVEL kFeatureLevels[] = {
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0,
    };

    UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;  // Required for D2D interop
#if defined(_DEBUG)
    flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

    D3D_FEATURE_LEVEL achieved{};
    HRESULT hr = D3D11CreateDevice(
        nullptr,                   // default adapter
        D3D_DRIVER_TYPE_HARDWARE,
        nullptr,
        flags,
        kFeatureLevels, ARRAYSIZE(kFeatureLevels),
        D3D11_SDK_VERSION,
        &m_d3dDevice,
        &achieved,
        &m_d3dContext
    );

    if (FAILED(hr)) {
        // Fallback: WARP software renderer (rare, guarantees app works on virtual machines)
        hr = D3D11CreateDevice(
            nullptr,
            D3D_DRIVER_TYPE_WARP,
            nullptr,
            D3D11_CREATE_DEVICE_BGRA_SUPPORT,
            kFeatureLevels, ARRAYSIZE(kFeatureLevels),
            D3D11_SDK_VERSION,
            &m_d3dDevice,
            &achieved,
            &m_d3dContext
        );
        if (FAILED(hr)) {
            WCHAR msg[128];
            swprintf_s(msg, L"D3D11CreateDevice failed (WARP fallback). HRESULT=0x%08X", (unsigned)hr);
            MessageBoxW(nullptr, msg, L"Snippy Init Error", MB_OK | MB_ICONERROR);
            return false;
        }
    }

    hr = m_d3dDevice.As(&m_dxgiDevice);
    if (FAILED(hr)) {
        MessageBoxW(nullptr, L"QI for IDXGIDevice failed", L"Snippy Init Error", MB_OK | MB_ICONERROR);
        return false;
    }

    hr = m_dxgiDevice->GetAdapter(&m_dxgiAdapter);
    if (FAILED(hr)) {
        MessageBoxW(nullptr, L"GetAdapter failed", L"Snippy Init Error", MB_OK | MB_ICONERROR);
        return false;
    }

    hr = m_dxgiAdapter->GetParent(IID_PPV_ARGS(&m_dxgiFactory));
    if (FAILED(hr)) {
        MessageBoxW(nullptr, L"GetParent (DXGIFactory) failed", L"Snippy Init Error", MB_OK | MB_ICONERROR);
        return false;
    }

    // Max frame latency: 1 — minimize queued frames, reduce latency
    ComPtr<IDXGIDevice1> dxgiDevice1;
    if (SUCCEEDED(m_dxgiDevice.As(&dxgiDevice1))) {
        dxgiDevice1->SetMaximumFrameLatency(1);
    }

    return true;
}

bool D2DContext::CreateD2DResources() noexcept {
    D2D1_FACTORY_OPTIONS opts{};
#if defined(_DEBUG)
    opts.debugLevel = D2D1_DEBUG_LEVEL_WARNING;
#endif
    HRESULT hr = D2D1CreateFactory(
        D2D1_FACTORY_TYPE_MULTI_THREADED,
        __uuidof(ID2D1Factory3),
        &opts,
        reinterpret_cast<void**>(m_d2dFactory.GetAddressOf())
    );
    if (FAILED(hr)) {
        WCHAR msg[128];
        swprintf_s(msg, L"D2D1CreateFactory failed. HRESULT=0x%08X", (unsigned)hr);
        MessageBoxW(nullptr, msg, L"Snippy Init Error", MB_OK | MB_ICONERROR);
        return false;
    }

    hr = m_d2dFactory->CreateDevice(m_dxgiDevice.Get(), &m_d2dDevice);
    if (FAILED(hr)) {
        WCHAR msg[128];
        swprintf_s(msg, L"D2D CreateDevice failed. HRESULT=0x%08X", (unsigned)hr);
        MessageBoxW(nullptr, msg, L"Snippy Init Error", MB_OK | MB_ICONERROR);
        return false;
    }

    hr = m_d2dDevice->CreateDeviceContext(
        D2D1_DEVICE_CONTEXT_OPTIONS_ENABLE_MULTITHREADED_OPTIMIZATIONS,
        &m_d2dDevCtx
    );
    if (FAILED(hr)) {
        WCHAR msg[128];
        swprintf_s(msg, L"CreateDeviceContext failed. HRESULT=0x%08X", (unsigned)hr);
        MessageBoxW(nullptr, msg, L"Snippy Init Error", MB_OK | MB_ICONERROR);
        return false;
    }

    UINT dpi = GetDpiForSystem();
    m_dpiScale = static_cast<FLOAT>(dpi) / 96.0f;

    return true;
}

} // namespace snippy
