#include "overlay_window.h"
#include "../render/d2d_context.h"
#include "../render/dwrite_context.h"
#include "../clipboard/clipboard.h"
#include "../perf/perf_counter.h"
#include "../settings/settings_manager.h"
#include "../theme/theme_manager.h"
#include "../utils/logger.h"
#include "ocr_extract.h"
#include "../qr/qr_extract.h"
#include <shellapi.h>
#include <d2d1effects_2.h>
#include <cstdio>
#include <wchar.h>
#include <windowsx.h>
#include <cmath>
#include <thread>
#include <wincodec.h>
#include <shobjidl.h>
#include <shlobj.h>
#include <knownfolders.h>
#include <algorithm>
#include <cstring>
#include <cwctype>
#include <unordered_set>

namespace snippy {

OverlayWindow g_overlay;

static constexpr WCHAR kClassName[] = L"SnippyOverlay";
static constexpr WCHAR kSettingsSection[] = L"Overlay";

static std::wstring TrimSetting(std::wstring value) {
    const auto first = value.find_first_not_of(L" \t");
    if (first == std::wstring::npos) return {};
    const auto last = value.find_last_not_of(L" \t");
    value = value.substr(first, last - first + 1);
    for (auto& ch : value) ch = static_cast<WCHAR>(std::towlower(ch));
    return value;
}

static std::vector<std::wstring> ParseSettingList(const std::wstring& value) {
    std::vector<std::wstring> entries;
    size_t start = 0;
    while (start < value.size()) {
        const size_t end = value.find(L',', start);
        auto entry = TrimSetting(value.substr(start, end == std::wstring::npos ? end : end - start));
        if (!entry.empty()) entries.push_back(std::move(entry));
        if (end == std::wstring::npos) break;
        start = end + 1;
    }
    return entries;
}

static bool HasSetting(const std::vector<std::wstring>& values, const WCHAR* name) {
    if (!name || !*name) return false;
    const std::wstring normName = TrimSetting(name);
    for (const auto& v : values) {
        if (_wcsicmp(v.c_str(), name) == 0) return true;
        const std::wstring normV = TrimSetting(v);
        if (normV == normName) return true;
        if ((normName == L"pen" || normName == L"draw") &&
            (normV == L"pen" || normV == L"draw" || normV == L"draw tools" || normV == L"drawtools")) {
            return true;
        }
    }
    return false;
}

static bool ParseHexColor(const std::wstring& value, D2D1_COLOR_F& color) {
    if (value.size() != 7 || value[0] != L'#') return false;
    unsigned int rgb = 0;
    if (swscanf_s(value.c_str() + 1, L"%06x", &rgb) != 1) return false;
    color = D2D1::ColorF((rgb >> 16) / 255.f, ((rgb >> 8) & 0xff) / 255.f,
                         (rgb & 0xff) / 255.f, 1.f);
    return true;
}

void OverlayWindow::LoadToolbarSettings() noexcept {
    const auto& s = SettingsManager::Current();

    m_dimmingOpacity = std::clamp(s.dimmingOpacity / 100.0f, 0.0f, 1.0f);
    m_selectionBorderThickness = static_cast<float>(std::clamp(s.selectionBorderThickness, 1, 6));
    m_toolbarAtTop = (_wcsicmp(s.toolbarPosition.c_str(), L"Bottom") != 0);
    m_hiddenTools = s.hiddenTools;

    ThemePalette palette = ThemeManager::GetPalette(s.theme, s.accentColor, s.useAccentColor);
    m_toolbarAccent = palette.accent;
    m_toolbarDark = !palette.isLight;

    auto* dc = g_d2d.D2DDevCtx();
    if (!dc) return;

    if (m_dimBrush) {
        m_dimBrush->SetColor(D2D1::ColorF(0.0f, 0.0f, 0.0f, m_dimmingOpacity));
    }
    if (m_selBorderBrush) {
        m_selBorderBrush->SetColor(m_toolbarAccent);
    }
    if (m_handleBrush) {
        m_handleBrush->SetColor(m_toolbarAccent);
    }
    if (m_toolbarActiveBrush) {
        m_toolbarActiveBrush->SetColor(m_toolbarAccent);
    }
    if (m_toolbarBgBrush) {
        m_toolbarBgBrush->SetColor(m_toolbarDark
            ? D2D1::ColorF(0.110f, 0.110f, 0.110f, 0.980f)
            : D2D1::ColorF(0.992f, 0.992f, 0.992f, 0.961f));
    }
    if (m_toolbarBorderBrush) {
        m_toolbarBorderBrush->SetColor(m_toolbarDark
            ? D2D1::ColorF(0.247f, 0.247f, 0.275f, 1.f)
            : D2D1::ColorF(0.835f, 0.835f, 0.855f, 1.f));
    }
    if (m_toolbarHoverBrush) {
        m_toolbarHoverBrush->SetColor(m_toolbarDark
            ? D2D1::ColorF(1.f, 1.f, 1.f, 0.10f)
            : D2D1::ColorF(0.f, 0.f, 0.f, 0.06f));
    }
    if (m_toolbarIconBrush) {
        m_toolbarIconBrush->SetColor(m_toolbarDark
            ? D2D1::ColorF(0.816f, 0.816f, 0.816f, 1.f)
            : D2D1::ColorF(0.227f, 0.227f, 0.235f, 1.f));
    }
    if (m_toolbarCloseBrush) {
        m_toolbarCloseBrush->SetColor(m_toolbarDark
            ? D2D1::ColorF(0.627f, 0.627f, 0.627f, 1.f)
            : D2D1::ColorF(0.361f, 0.361f, 0.376f, 1.f));
    }
    if (m_toolbarCopyBrush) {
        m_toolbarCopyBrush->SetColor(m_toolbarDark
            ? D2D1::ColorF(1.f, 1.f, 1.f, 1.f)
            : D2D1::ColorF(0.110f, 0.110f, 0.118f, 1.f));
    }
    if (m_separatorBrush) {
        m_separatorBrush->SetColor(m_toolbarDark
            ? D2D1::ColorF(1.f, 1.f, 1.f, 0.16f)
            : D2D1::ColorF(0.f, 0.f, 0.f, 0.14f));
    }
}

void OverlayWindow::ReloadSettings() noexcept {
    LoadToolbarSettings();

    if (HasSetting(m_hiddenTools, L"Pen") &&
        (m_toolMode == ToolMode::FreeDraw || m_toolMode == ToolMode::Highlighter || m_toolMode == ToolMode::Eraser)) {
        m_toolMode = ToolMode::Select;
    }
    if (HasSetting(m_hiddenTools, L"Shapes") &&
        (m_toolMode == ToolMode::Rect || m_toolMode == ToolMode::Ellipse ||
         m_toolMode == ToolMode::Arrow || m_toolMode == ToolMode::Line || m_toolMode == ToolMode::FilledRect)) {
        m_toolMode = ToolMode::Select;
    }
    if (HasSetting(m_hiddenTools, L"Blur") && m_toolMode == ToolMode::Blur) {
        m_toolMode = ToolMode::Select;
    }
    if (HasSetting(m_hiddenTools, L"Text") && m_toolMode == ToolMode::Text) {
        m_toolMode = ToolMode::Select;
    }
    if (HasSetting(m_hiddenTools, L"Counter") && m_toolMode == ToolMode::Counter) {
        m_toolMode = ToolMode::Select;
    }

    if (m_hwnd && IsWindowVisible(m_hwnd)) {
        BuildToolbar();
        RenderFrame();
    }
}

static float Dist2(D2D1_POINT_2F a, D2D1_POINT_2F b) {
    float dx = a.x - b.x, dy = a.y - b.y;
    return dx * dx + dy * dy;
}

static float DistToSegment(D2D1_POINT_2F p, D2D1_POINT_2F a, D2D1_POINT_2F b) {
    float vx = b.x - a.x, vy = b.y - a.y;
    float wx = p.x - a.x, wy = p.y - a.y;
    float c1 = vx * wx + vy * wy;
    if (c1 <= 0.f) return sqrtf(Dist2(p, a));
    float c2 = vx * vx + vy * vy;
    if (c2 <= c1) return sqrtf(Dist2(p, b));
    float t = c1 / c2;
    return sqrtf(Dist2(p, D2D1::Point2F(a.x + t * vx, a.y + t * vy)));
}

// ── Initialize ────────────────────────────────────────────────────────────────

bool OverlayWindow::Initialize(HINSTANCE hInstance) noexcept {
    m_hInstance = hInstance;

    WNDCLASSEXW wc{};
    wc.cbSize        = sizeof(wc);
    wc.style         = CS_HREDRAW | CS_VREDRAW | CS_OWNDC | CS_DBLCLKS;
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInstance;
    wc.hCursor       = LoadCursorW(nullptr, IDC_CROSS);
    wc.lpszClassName = kClassName;
    wc.hbrBackground = nullptr;
    RegisterClassExW(&wc);

    int vx = GetSystemMetrics(SM_XVIRTUALSCREEN);
    int vy = GetSystemMetrics(SM_YVIRTUALSCREEN);
    int vw = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    int vh = GetSystemMetrics(SM_CYVIRTUALSCREEN);

    m_width  = static_cast<UINT>(vw);
    m_height = static_cast<UINT>(vh);

    m_hwnd = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_LAYERED,
        kClassName,
        L"Snippy",
        WS_POPUP,
        vx, vy, vw, vh,
        nullptr, nullptr,
        hInstance, this
    );
    if (!m_hwnd) return false;

    SetLayeredWindowAttributes(m_hwnd, 0, 255, LWA_ALPHA);

    if (!CreateSwapChain())    return false;
    if (!CreateRenderTarget()) return false;
    if (!CreateResources())    return false;

    m_initialized = true;
    return true;
}

bool OverlayWindow::CreateSwapChain() noexcept {
    WCHAR dbg[256];

    HRESULT hr = DCompositionCreateDevice(
        g_d2d.DXGIDevice(),
        IID_PPV_ARGS(&m_dcompDevice)
    );
    if (FAILED(hr)) {
        swprintf_s(dbg, L"DCompositionCreateDevice failed. HRESULT=0x%08X\nVirtual size: %ux%u", (unsigned)hr, m_width, m_height);
        MessageBoxW(nullptr, dbg, L"Snippy Overlay Error", MB_OK | MB_ICONERROR);
        return false;
    }

    hr = m_dcompDevice->CreateTargetForHwnd(m_hwnd, TRUE, &m_dcompTarget);
    if (FAILED(hr)) {
        swprintf_s(dbg, L"CreateTargetForHwnd failed. HRESULT=0x%08X", (unsigned)hr);
        MessageBoxW(nullptr, dbg, L"Snippy Overlay Error", MB_OK | MB_ICONERROR);
        return false;
    }

    hr = m_dcompDevice->CreateVisual(&m_dcompVisual);
    if (FAILED(hr)) {
        swprintf_s(dbg, L"CreateVisual failed. HRESULT=0x%08X", (unsigned)hr);
        MessageBoxW(nullptr, dbg, L"Snippy Overlay Error", MB_OK | MB_ICONERROR);
        return false;
    }

    DXGI_SWAP_CHAIN_DESC1 scDesc{};
    scDesc.Width              = m_width;
    scDesc.Height             = m_height;
    scDesc.Format             = DXGI_FORMAT_B8G8R8A8_UNORM;
    scDesc.Stereo             = FALSE;
    scDesc.SampleDesc.Count   = 1;
    scDesc.SampleDesc.Quality = 0;
    scDesc.BufferUsage        = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    scDesc.BufferCount        = kSwapBufferCount;
    scDesc.Scaling            = DXGI_SCALING_STRETCH;
    scDesc.SwapEffect         = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    scDesc.AlphaMode          = DXGI_ALPHA_MODE_PREMULTIPLIED;
    scDesc.Flags              = 0;

    hr = g_d2d.DXGIFactory()->CreateSwapChainForComposition(
        g_d2d.D3DDevice(),
        &scDesc,
        nullptr,
        &m_swapChain
    );
    if (FAILED(hr)) {
        swprintf_s(dbg, L"CreateSwapChainForComposition failed. HRESULT=0x%08X\nSize: %ux%u", (unsigned)hr, m_width, m_height);
        MessageBoxW(nullptr, dbg, L"Snippy Overlay Error", MB_OK | MB_ICONERROR);
        return false;
    }

    hr = m_dcompVisual->SetContent(m_swapChain.Get());
    if (FAILED(hr)) return false;

    hr = m_dcompTarget->SetRoot(m_dcompVisual.Get());
    if (FAILED(hr)) return false;

    hr = m_dcompDevice->Commit();
    if (FAILED(hr)) return false;

    return true;
}

bool OverlayWindow::CreateRenderTarget() noexcept {
    ComPtr<IDXGISurface> surface;
    HRESULT hr = m_swapChain->GetBuffer(0, IID_PPV_ARGS(&surface));
    if (FAILED(hr)) return false;

    D2D1_BITMAP_PROPERTIES1 bitmapProps = D2D1::BitmapProperties1(
        D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED)
    );
    hr = g_d2d.D2DDevCtx()->CreateBitmapFromDxgiSurface(surface.Get(), &bitmapProps, &m_d2dTarget);
    return SUCCEEDED(hr);
}

void OverlayWindow::EnsureIntermediateTexture(UINT width, UINT height) noexcept {
    if (m_blurIntermediateTex && m_blurIntermediateBitmap) {
        D3D11_TEXTURE2D_DESC d{};
        m_blurIntermediateTex->GetDesc(&d);
        if (d.Width == width && d.Height == height) {
            return;
        }
    }
    m_blurIntermediateBitmap.Reset();
    m_blurIntermediateTex.Reset();

    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = width;
    desc.Height = height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    if (FAILED(g_d2d.D3DDevice()->CreateTexture2D(&desc, nullptr, &m_blurIntermediateTex))) {
        return;
    }

    ComPtr<IDXGISurface> surf;
    if (SUCCEEDED(m_blurIntermediateTex.As(&surf))) {
        D2D1_BITMAP_PROPERTIES1 bp = D2D1::BitmapProperties1(
            D2D1_BITMAP_OPTIONS_NONE,
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED)
        );
        g_d2d.D2DDevCtx()->CreateBitmapFromDxgiSurface(surf.Get(), &bp, &m_blurIntermediateBitmap);
    }
}

bool OverlayWindow::CreateResources() noexcept {
    LoadToolbarSettings();
    auto* dc = g_d2d.D2DDevCtx();

    HRESULT hr{};
    hr = dc->CreateSolidColorBrush(D2D1::ColorF(0.0f, 0.0f, 0.0f, m_dimmingOpacity), &m_dimBrush);
    if (FAILED(hr)) return false;

    hr = dc->CreateSolidColorBrush(m_toolbarAccent, &m_selBorderBrush);
    if (FAILED(hr)) return false;

    hr = dc->CreateSolidColorBrush(m_toolbarAccent, &m_handleBrush);
    if (FAILED(hr)) return false;

    hr = dc->CreateSolidColorBrush(D2D1::ColorF(0.05f, 0.05f, 0.05f, 0.85f), &m_labelBgBrush);
    if (FAILED(hr)) return false;

    hr = dc->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::White, 1.0f), &m_labelFgBrush);
    if (FAILED(hr)) return false;

    hr = dc->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::White, 0.25f), &m_guideBrush);
    if (FAILED(hr)) return false;

    hr = dc->CreateSolidColorBrush(m_toolbarDark
        ? D2D1::ColorF(0.110f, 0.110f, 0.110f, 0.980f)
        : D2D1::ColorF(0.992f, 0.992f, 0.992f, 0.961f), &m_toolbarBgBrush);
    if (FAILED(hr)) return false;

    hr = dc->CreateSolidColorBrush(m_toolbarDark
        ? D2D1::ColorF(0.247f, 0.247f, 0.275f, 1.f)
        : D2D1::ColorF(0.835f, 0.835f, 0.855f, 1.f), &m_toolbarBorderBrush);
    if (FAILED(hr)) return false;

    hr = dc->CreateSolidColorBrush(D2D1::ColorF(0.f, 0.f, 0.f, 0.40f), &m_toolbarShadowBrush);
    if (FAILED(hr)) return false;

    hr = dc->CreateSolidColorBrush(m_toolbarAccent, &m_toolbarActiveBrush);
    if (FAILED(hr)) return false;

    hr = dc->CreateSolidColorBrush(m_toolbarDark
        ? D2D1::ColorF(1.f, 1.f, 1.f, 0.10f)
        : D2D1::ColorF(0.f, 0.f, 0.f, 0.06f), &m_toolbarHoverBrush);
    if (FAILED(hr)) return false;

    hr = dc->CreateSolidColorBrush(m_toolbarDark
        ? D2D1::ColorF(0.816f, 0.816f, 0.816f, 1.f)
        : D2D1::ColorF(0.227f, 0.227f, 0.235f, 1.f), &m_toolbarIconBrush);
    if (FAILED(hr)) return false;

    hr = dc->CreateSolidColorBrush(m_toolbarDark
        ? D2D1::ColorF(0.627f, 0.627f, 0.627f, 1.f)
        : D2D1::ColorF(0.361f, 0.361f, 0.376f, 1.f), &m_toolbarCloseBrush);
    if (FAILED(hr)) return false;

    hr = dc->CreateSolidColorBrush(D2D1::ColorF(1.f, 0.231f, 0.188f, 1.f), &m_toolbarCloseHoverBrush);
    if (FAILED(hr)) return false;

    hr = dc->CreateSolidColorBrush(m_toolbarDark
        ? D2D1::ColorF(1.f, 1.f, 1.f, 1.f)
        : D2D1::ColorF(0.110f, 0.110f, 0.118f, 1.f), &m_toolbarCopyBrush);
    if (FAILED(hr)) return false;

    hr = dc->CreateSolidColorBrush(m_toolbarDark
        ? D2D1::ColorF(1.f, 1.f, 1.f, 0.16f)
        : D2D1::ColorF(0.f, 0.f, 0.f, 0.14f), &m_separatorBrush);
    if (FAILED(hr)) return false;

    hr = dc->CreateSolidColorBrush(D2D1::ColorF(0.f, 0.f, 0.f, 1.0f), &m_swatchBrush);
    if (FAILED(hr)) return false;

    hr = dc->CreateSolidColorBrush(
        D2D1::ColorF(m_strokeColor.r, m_strokeColor.g, m_strokeColor.b, m_strokeColor.a),
        &m_strokeBrush);
    if (FAILED(hr)) return false;

    hr = dc->CreateSolidColorBrush(D2D1::ColorF(1.f, 0.92f, 0.22f, 0.45f), &m_highlightBrush);
    if (FAILED(hr)) return false;

    hr = dc->CreateSolidColorBrush(D2D1::ColorF(1.f, 1.f, 1.f, 0.5f), &m_previewBrush);
    if (FAILED(hr)) return false;

    ComPtr<ID2D1Factory> factory;
    dc->GetFactory(&factory);

    D2D1_STROKE_STYLE_PROPERTIES roundProps{};
    roundProps.startCap = D2D1_CAP_STYLE_ROUND;
    roundProps.endCap   = D2D1_CAP_STYLE_ROUND;
    roundProps.lineJoin = D2D1_LINE_JOIN_ROUND;
    hr = factory->CreateStrokeStyle(roundProps, nullptr, 0, &m_roundStroke);
    if (FAILED(hr)) return false;

    D2D1_STROKE_STYLE_PROPERTIES dashProps{};
    dashProps.dashStyle = D2D1_DASH_STYLE_DASH;
    dashProps.dashCap   = D2D1_CAP_STYLE_FLAT;
    hr = factory->CreateStrokeStyle(dashProps, nullptr, 0, &m_dashStroke);
    if (FAILED(hr)) return false;

    return true;
}

// ── Show / Hide ───────────────────────────────────────────────────────────────

void OverlayWindow::Show(const FrameBuffer& frameBuffer, const MonitorInfo& monitor) noexcept {
    ReloadSettings();
    m_frameBuffer  = &frameBuffer;
    m_monitorRect  = monitor.monitorRect;
    m_selection.Reset();
    m_annotations.clear();
    m_undoStack.clear();
    m_redoStack.clear();
    m_captureMode = CaptureMode::Region;
    m_toolMode    = ToolMode::Select;
    m_rectState  = {};
    m_arrowState = {};
    m_textState  = {};
    m_blurState  = {};
    m_freeDrawState = {};
    m_counterState = {};
    m_lassoPoints.clear();
    m_lassoDrawing = false;
    m_dragging   = false;
    m_erasing    = false;
    m_eraseUndoPushed = false;
    m_annotationDragUndoPushed = false;
    m_toolbar.clear();
    m_toolbarPopup = ToolbarPopup::None;
    m_fontDropdownOpen = false;
    m_fontSizeDragging = false;
    m_penSizeDragging = false;
    m_colorSpectrumDragging = false;
    m_colorHueDragging = false;
    m_qrDetected = false;
    m_qrHovered = false;
    m_qrTooltipHoverBtn = 0;
    m_qrHighlightRect = {};
    m_qrBadgeRect = {};
    m_qrTooltipRect = {};
    m_qrOpenBtnRect = {};
    m_qrCopyBtnRect = {};
    ++m_qrScanSeq;
    m_ocrHoverButton = -1;
    m_ocrQuickActionHoverBtn = 0;

    auto* dc = g_d2d.D2DDevCtx();

    D2D1_BITMAP_PROPERTIES bitmapProps{};
    bitmapProps.pixelFormat.format    = DXGI_FORMAT_B8G8R8A8_UNORM;
    bitmapProps.pixelFormat.alphaMode = D2D1_ALPHA_MODE_IGNORE;
    bitmapProps.dpiX = 96.0f;
    bitmapProps.dpiY = 96.0f;

    D2D1_SIZE_U bitmapSize{
        .width  = frameBuffer.width,
        .height = frameBuffer.height
    };

    bool needNewBitmap = true;
    if (m_desktopBitmap) {
        auto sz = m_desktopBitmap->GetPixelSize();
        needNewBitmap = (sz.width != bitmapSize.width || sz.height != bitmapSize.height);
    }

    if (needNewBitmap) {
        m_desktopBitmap.Reset();
        dc->CreateBitmap(bitmapSize, frameBuffer.data, frameBuffer.stride,
                         bitmapProps, &m_desktopBitmap);
    } else {
        m_desktopBitmap->CopyFromMemory(nullptr, frameBuffer.data, frameBuffer.stride);
    }

    SetWindowPos(m_hwnd, HWND_TOPMOST,
        m_monitorRect.left, m_monitorRect.top,
        m_monitorRect.right  - m_monitorRect.left,
        m_monitorRect.bottom - m_monitorRect.top,
        SWP_NOACTIVATE);

    m_visible = true;
    RenderFrame();

    ShowWindow(m_hwnd, SW_SHOW);
    SetForegroundWindow(m_hwnd);
    SetActiveWindow(m_hwnd);
    SetFocus(m_hwnd);
    SetCapture(m_hwnd);

    PERF_MARK(t_overlay_shown);
}

void OverlayWindow::Hide() noexcept {
    ReleaseCapture();
    if (m_hwnd) KillTimer(m_hwnd, 1001);
    ShowWindow(m_hwnd, SW_HIDE);
    m_frameBuffer = nullptr;
    m_desktopBitmap.Reset();
    m_blurIntermediateBitmap.Reset();
    m_blurIntermediateTex.Reset();
    m_activeRenderTexture = nullptr;
    m_visible = false;
    m_selection.Reset();
    m_annotations.clear();
    m_annotations.shrink_to_fit();
    m_undoStack.clear();
    m_undoStack.shrink_to_fit();
    m_redoStack.clear();
    m_redoStack.shrink_to_fit();
    m_selectedAnnotationIndex = -1;
    m_annotationResizeHandle = -1;
    m_resizeHandle = -1;
    m_lassoPoints.clear();
    m_lassoPoints.shrink_to_fit();
    m_lassoDrawing = false;
    m_dragging = false;
    m_erasing = false;
    m_eraseUndoPushed = false;
    m_annotationDragUndoPushed = false;
    m_rectState = {};
    m_arrowState = {};
    m_textState = {};
    m_blurState = {};
    m_freeDrawState = {};
    m_counterState = {};
    m_toolbar.clear();
    m_toolbar.shrink_to_fit();
    m_toolbarPopup = ToolbarPopup::None;
    m_fontDropdownOpen = false;
    m_fontSizeDragging = false;
    m_penSizeDragging = false;
    m_colorSpectrumDragging = false;
    m_colorHueDragging = false;
    m_ocrActive = false;
    m_ocrLoading = false;
    m_ocrResult = {};
    m_selectedOcrCharIndices.clear();
    m_selectedOcrCharIndices.shrink_to_fit();
    m_isOcrSelecting = false;
    m_ocrQuickActionVisible = false;
    m_ocrHoverButton = -1;
    m_ocrQuickActionHoverBtn = 0;
    m_qrDetected = false;
    m_qrHovered = false;
    m_qrTooltipHoverBtn = 0;
    m_qrHighlightRect = {};
    m_qrBadgeRect = {};
    m_qrTooltipRect = {};
    m_qrOpenBtnRect = {};
    m_qrCopyBtnRect = {};
    ++m_qrScanSeq;

    // Clear swapchain surface immediately so no old image/annotations remain in GPU buffer
    if (m_swapChain) {
        auto* dc = g_d2d.D2DDevCtx();
        if (dc && m_d2dTarget) {
            dc->SetTarget(m_d2dTarget.Get());
            dc->BeginDraw();
            dc->Clear(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.0f));
            dc->EndDraw();
            m_swapChain->Present(0, 0);
        }
    }
}

// ── Capture helpers ───────────────────────────────────────────────────────────

bool OverlayWindow::IsEditorOpen() const noexcept {
    return m_selection.phase == SelectionState::Phase::Editing &&
           (m_captureMode == CaptureMode::Region ||
            m_captureMode == CaptureMode::Fullscreen ||
            m_captureMode == CaptureMode::Freeform);
}

void OverlayWindow::ApplyFullscreenSelection() noexcept {
    m_selection.anchor  = { m_monitorRect.left,  m_monitorRect.top };
    m_selection.current = { m_monitorRect.right, m_monitorRect.bottom };
    m_selection.phase   = SelectionState::Phase::Editing;
    m_toolMode = HasSetting(m_hiddenTools, L"Pen") ? ToolMode::Select : ToolMode::FreeDraw;
    m_annotations.clear();
    m_undoStack.clear();
    m_redoStack.clear();
    StartAutoQrScan();
}

void OverlayWindow::CommitLassoSelection() noexcept {
    if (m_lassoPoints.size() < 3) {
        m_lassoPoints.clear();
        m_lassoDrawing = false;
        return;
    }
    float minX = m_lassoPoints[0].x, maxX = minX;
    float minY = m_lassoPoints[0].y, maxY = minY;
    for (const auto& p : m_lassoPoints) {
        minX = std::min(minX, p.x); maxX = std::max(maxX, p.x);
        minY = std::min(minY, p.y); maxY = std::max(maxY, p.y);
    }
    if (maxX - minX < 4.f || maxY - minY < 4.f) {
        m_lassoPoints.clear();
        m_lassoDrawing = false;
        return;
    }
    // Close the freeform polygon loop
    if (m_lassoPoints.front().x != m_lassoPoints.back().x ||
        m_lassoPoints.front().y != m_lassoPoints.back().y) {
        m_lassoPoints.push_back(m_lassoPoints.front());
    }
    m_selection.anchor  = { m_monitorRect.left + static_cast<LONG>(minX),
                            m_monitorRect.top  + static_cast<LONG>(minY) };
    m_selection.current = { m_monitorRect.left + static_cast<LONG>(maxX),
                            m_monitorRect.top  + static_cast<LONG>(maxY) };
    m_selection.phase   = SelectionState::Phase::Editing;
    m_toolMode = ToolMode::Select;
    m_lassoDrawing = false;
    m_annotations.clear();
    m_undoStack.clear();
    m_redoStack.clear();
    StartAutoQrScan();
}

bool OverlayWindow::IsPointInFreeform(D2D1_POINT_2F pt) const noexcept {
    if (m_lassoPoints.size() < 3) return false;
    bool inside = false;
    for (size_t i = 0, j = m_lassoPoints.size() - 1; i < m_lassoPoints.size(); j = i++) {
        if (((m_lassoPoints[i].y > pt.y) != (m_lassoPoints[j].y > pt.y)) &&
            (pt.x < (m_lassoPoints[j].x - m_lassoPoints[i].x) * (pt.y - m_lassoPoints[i].y) / (m_lassoPoints[j].y - m_lassoPoints[i].y) + m_lassoPoints[i].x)) {
            inside = !inside;
        }
    }
    return inside;
}

bool OverlayWindow::SamplePixel(POINT screenPt, uint8_t& r, uint8_t& g, uint8_t& b) const noexcept {
    if (!m_frameBuffer || !m_frameBuffer->IsValid()) return false;
    int x = screenPt.x - m_monitorRect.left;
    int y = screenPt.y - m_monitorRect.top;
    if (x < 0 || y < 0 || x >= static_cast<int>(m_frameBuffer->width) ||
        y >= static_cast<int>(m_frameBuffer->height))
        return false;
    const uint8_t* p = m_frameBuffer->PixelPtr(static_cast<uint32_t>(x), static_cast<uint32_t>(y));
    b = p[0]; g = p[1]; r = p[2];
    return true;
}

void OverlayWindow::PickColorAt(POINT screenPt) noexcept {
    uint8_t r, g, b;
    if (!SamplePixel(screenPt, r, g, b)) return;
    WCHAR hex[16];
    swprintf_s(hex, L"#%02X%02X%02X", r, g, b);
    Clipboard::WriteText(hex, m_hwnd);
    Hide();
    if (OnCancel) OnCancel();
}

void OverlayWindow::FinishTextExtract() noexcept {
    StartTextExtract();
}

void OverlayWindow::StartTextExtract() noexcept {
    if (!m_selection.IsValid() || !m_frameBuffer) {
        Hide();
        if (OnCancel) OnCancel();
        return;
    }
    RunOcr();
}

void OverlayWindow::RunOcr() noexcept {
    m_ocrActive = true;
    m_ocrLoading = true;
    m_ocrResult = {};
    m_selectedOcrCharIndices.clear();
    m_ocrQuickActionVisible = false;
    m_selection.phase = SelectionState::Phase::Editing;

    RECT buf = BufferRect(m_selection.NormalizedRect());
    FrameBuffer view = m_frameBuffer->SubRegion(buf);
    std::vector<uint8_t> packed;
    if (view.IsValid()) {
        packed.resize(static_cast<size_t>(view.width) * view.height * 4);
        for (UINT row = 0; row < view.height; ++row) {
            memcpy(packed.data() + static_cast<size_t>(row) * view.width * 4,
                   view.RowPtr(row), static_cast<size_t>(view.width) * 4);
        }
        HWND hwnd = m_hwnd;
        ExtractInlineTextAsync(std::move(packed), view.width, view.height,
            [hwnd](OcrInlineResult res) {
                auto* p = new OcrInlineResult(std::move(res));
                PostMessageW(hwnd, WM_USER + 105, 0, reinterpret_cast<LPARAM>(p));
            });
    }
    RenderFrame();
}

void OverlayWindow::OnOcrComplete(const OcrInlineResult& res) noexcept {
    m_ocrLoading = false;
    m_ocrResult = res;
    RenderFrame();
}

struct QrScanMsg {
    uint32_t seq = 0;
    QrResult result{};
};

void OverlayWindow::StartAutoQrScan() noexcept {
    m_qrDetected = false;
    m_qrHovered = false;
    m_qrTooltipHoverBtn = 0;
    m_qrHighlightRect = {};
    m_qrBadgeRect = {};
    m_qrTooltipRect = {};
    m_qrOpenBtnRect = {};
    m_qrCopyBtnRect = {};

    if (!m_frameBuffer || !m_frameBuffer->IsValid()) return;
    if (m_captureMode == CaptureMode::ColorPicker || m_captureMode == CaptureMode::TextExtract) return;

    bool hasSel = m_selection.IsValid();
    std::vector<uint8_t> selPixels;
    uint32_t selW = 0, selH = 0;
    float selLeft = 0.f, selTop = 0.f;
    if (hasSel) {
        RECT buf = BufferRect(m_selection.NormalizedRect());
        FrameBuffer view = m_frameBuffer->SubRegion(buf);
        if (view.IsValid()) {
            selW = view.width;
            selH = view.height;
            selLeft = static_cast<float>(m_selection.NormalizedRect().left - m_monitorRect.left);
            selTop  = static_cast<float>(m_selection.NormalizedRect().top  - m_monitorRect.top);
            selPixels.resize(static_cast<size_t>(selW) * selH * 4);
            for (UINT row = 0; row < selH; ++row) {
                memcpy(selPixels.data() + static_cast<size_t>(row) * selW * 4,
                       view.RowPtr(row), static_cast<size_t>(selW) * 4);
            }
        } else {
            hasSel = false;
        }
    }

    uint32_t fullW = m_frameBuffer->width;
    uint32_t fullH = m_frameBuffer->height;
    std::vector<uint8_t> fullPixels;
    fullPixels.resize(static_cast<size_t>(fullW) * fullH * 4);
    for (UINT row = 0; row < fullH; ++row) {
        memcpy(fullPixels.data() + static_cast<size_t>(row) * fullW * 4,
               m_frameBuffer->RowPtr(row), static_cast<size_t>(fullW) * 4);
    }

    const uint32_t seq = ++m_qrScanSeq;
    HWND hwnd = m_hwnd;

    std::thread([hasSel, selPixels = std::move(selPixels), selW, selH, selLeft, selTop,
                 fullPixels = std::move(fullPixels), fullW, fullH, hwnd, seq]() {
        QrResult res{};
        if (hasSel) {
            res = ScanQrFromBgra(selPixels.data(), selW, selH);
            if (res.success) {
                res.bounds.left   += selLeft;
                res.bounds.top    += selTop;
                res.bounds.right  += selLeft;
                res.bounds.bottom += selTop;
            }
        }
        if (!res.success) {
            res = ScanQrFromBgra(fullPixels.data(), fullW, fullH);
        }

        auto* qrMsg = new QrScanMsg{ seq, std::move(res) };
        PostMessageW(hwnd, WM_USER + 106, 0, reinterpret_cast<LPARAM>(qrMsg));
    }).detach();
}

void OverlayWindow::OnQrComplete(uint32_t seq, const QrResult& res) noexcept {
    if (seq != m_qrScanSeq || !IsEditorOpen()) return;
    if (!res.success) {
        m_qrDetected = false;
        RenderFrame();
        return;
    }

    m_qrDetected = true;
    m_qrResult = res;

    m_qrHighlightRect = res.bounds;

    float badgeW = res.isUrl ? 74.f : 62.f;
    float badgeH = 22.f;
    float bx = m_qrHighlightRect.left;
    float by = m_qrHighlightRect.top - badgeH - 3.f;
    if (by < 4.f) {
        by = m_qrHighlightRect.top + 4.f;
        bx = m_qrHighlightRect.left + 4.f;
    }
    m_qrBadgeRect = D2D1::RectF(bx, by, bx + badgeW, by + badgeH);

    RenderFrame();
}

void OverlayWindow::RenderQrHighlight(ID2D1DeviceContext* dc) noexcept {
    if (!m_qrDetected || !IsEditorOpen()) return;

    ComPtr<ID2D1SolidColorBrush> fillBrush;
    ComPtr<ID2D1SolidColorBrush> borderBrush;
    dc->CreateSolidColorBrush(D2D1::ColorF(m_toolbarAccent.r, m_toolbarAccent.g, m_toolbarAccent.b, m_qrHovered ? 0.22f : 0.12f), &fillBrush);
    dc->CreateSolidColorBrush(m_toolbarAccent, &borderBrush);

    D2D1_ROUNDED_RECT boxR = D2D1::RoundedRect(m_qrHighlightRect, 4.f, 4.f);
    dc->FillRoundedRectangle(boxR, fillBrush.Get());
    dc->DrawRoundedRectangle(boxR, borderBrush.Get(), m_qrHovered ? 2.0f : 1.5f);

    D2D1_ROUNDED_RECT badgeR = D2D1::RoundedRect(m_qrBadgeRect, 4.f, 4.f);
    dc->FillRoundedRectangle(badgeR, m_toolbarBgBrush.Get());
    dc->DrawRoundedRectangle(badgeR, m_toolbarBorderBrush.Get(), 1.0f);

    DrawFluentGlyph(dc, L'\uED14', D2D1::RectF(m_qrBadgeRect.left + 5.f, m_qrBadgeRect.top + 3.f,
                                               m_qrBadgeRect.left + 19.f, m_qrBadgeRect.bottom - 3.f),
                    m_toolbarActiveBrush.Get(), g_dwrite.IconFont14());
    const WCHAR* badgeText = m_qrResult.isUrl ? L"QR Link" : L"QR Code";
    dc->DrawTextW(badgeText, static_cast<UINT32>(wcslen(badgeText)), g_dwrite.UIFont(),
                  D2D1::RectF(m_qrBadgeRect.left + 22.f, m_qrBadgeRect.top + 2.f,
                              m_qrBadgeRect.right - 4.f, m_qrBadgeRect.bottom - 2.f), m_labelFgBrush.Get());

    if (!m_qrHovered) return;

    const float tipW = 340.f;
    const float tipH = m_qrResult.isUrl ? 74.f : 62.f;

    float tipX = (m_qrHighlightRect.left + m_qrHighlightRect.right) * 0.5f - tipW * 0.5f;
    float tipY = m_qrHighlightRect.bottom + 8.f;
    float monW = static_cast<float>(m_monitorRect.right - m_monitorRect.left);
    float monH = static_cast<float>(m_monitorRect.bottom - m_monitorRect.top);

    if (tipX < 12.f) tipX = 12.f;
    if (tipX + tipW > monW - 12.f) tipX = monW - 12.f - tipW;
    if (tipY + tipH > monH - 12.f) {
        tipY = m_qrHighlightRect.top - tipH - 8.f;
        if (tipY < 12.f) tipY = 12.f;
    }

    m_qrTooltipRect = D2D1::RectF(tipX, tipY, tipX + tipW, tipY + tipH);

    D2D1_ROUNDED_RECT shadowR = D2D1::RoundedRect(
        D2D1::RectF(tipX - 1.f, tipY + 2.f, tipX + tipW + 1.f, tipY + tipH + 4.f), 8.f, 8.f);
    dc->FillRoundedRectangle(shadowR, m_toolbarShadowBrush.Get());

    D2D1_ROUNDED_RECT tipR = D2D1::RoundedRect(m_qrTooltipRect, 8.f, 8.f);
    dc->FillRoundedRectangle(tipR, m_toolbarBgBrush.Get());
    dc->DrawRoundedRectangle(tipR, m_toolbarBorderBrush.Get(), 1.0f);

    const float row1Top = tipY + 8.f;
    const float row1Bottom = tipY + 34.f;

    WCHAR iconGlyph = m_qrResult.isUrl ? L'\uE71B' : L'\uED14';
    DrawFluentGlyph(dc, iconGlyph, D2D1::RectF(tipX + 10.f, row1Top, tipX + 26.f, row1Bottom),
                    m_toolbarActiveBrush.Get(), g_dwrite.IconFont14());

    const std::wstring& displayStr = m_qrResult.isUrl ? m_qrResult.url : m_qrResult.payload;
    D2D1_RECT_F textRect = D2D1::RectF(tipX + 30.f, row1Top, tipX + tipW - 12.f, row1Bottom);
    ComPtr<ID2D1SolidColorBrush> previewBrush;
    dc->CreateSolidColorBrush(D2D1::ColorF(1.f, 1.f, 1.f, 0.90f), &previewBrush);
    dc->DrawTextW(displayStr.c_str(), static_cast<UINT32>(displayStr.size()), g_dwrite.UIFont(), textRect, previewBrush.Get());

    if (m_qrResult.isUrl) {
        m_qrOpenBtnRect = D2D1::RectF(tipX + tipW - 146.f, tipY + tipH - 30.f, tipX + tipW - 76.f, tipY + tipH - 6.f);
        D2D1_ROUNDED_RECT openR = D2D1::RoundedRect(m_qrOpenBtnRect, 4.f, 4.f);
        dc->FillRoundedRectangle(openR, m_toolbarActiveBrush.Get());
        if (m_qrTooltipHoverBtn == 1) {
            ComPtr<ID2D1SolidColorBrush> hl;
            dc->CreateSolidColorBrush(D2D1::ColorF(1.f, 1.f, 1.f, 0.15f), &hl);
            dc->FillRoundedRectangle(openR, hl.Get());
        }
        DrawFluentGlyph(dc, L'\uE8A7', D2D1::RectF(m_qrOpenBtnRect.left + 6.f, m_qrOpenBtnRect.top,
                                                   m_qrOpenBtnRect.left + 22.f, m_qrOpenBtnRect.bottom),
                        m_labelFgBrush.Get(), g_dwrite.IconFont14());
        const WCHAR* openText = L"Open";
        dc->DrawTextW(openText, static_cast<UINT32>(wcslen(openText)), g_dwrite.UIFont(),
                      D2D1::RectF(m_qrOpenBtnRect.left + 24.f, m_qrOpenBtnRect.top,
                                  m_qrOpenBtnRect.right - 4.f, m_qrOpenBtnRect.bottom), m_labelFgBrush.Get());

        m_qrCopyBtnRect = D2D1::RectF(tipX + tipW - 70.f, tipY + tipH - 30.f, tipX + tipW - 8.f, tipY + tipH - 6.f);
        D2D1_ROUNDED_RECT copyR = D2D1::RoundedRect(m_qrCopyBtnRect, 4.f, 4.f);
        dc->FillRoundedRectangle(copyR, m_toolbarHoverBrush.Get());
        dc->DrawRoundedRectangle(copyR, m_toolbarBorderBrush.Get(), 1.0f);
        if (m_qrTooltipHoverBtn == 2) {
            ComPtr<ID2D1SolidColorBrush> hl;
            dc->CreateSolidColorBrush(D2D1::ColorF(1.f, 1.f, 1.f, 0.10f), &hl);
            dc->FillRoundedRectangle(copyR, hl.Get());
        }
        DrawFluentGlyph(dc, L'\uE8C8', D2D1::RectF(m_qrCopyBtnRect.left + 6.f, m_qrCopyBtnRect.top,
                                                   m_qrCopyBtnRect.left + 22.f, m_qrCopyBtnRect.bottom),
                        m_toolbarIconBrush.Get(), g_dwrite.IconFont14());
        const WCHAR* copyText = L"Copy";
        dc->DrawTextW(copyText, static_cast<UINT32>(wcslen(copyText)), g_dwrite.UIFont(),
                      D2D1::RectF(m_qrCopyBtnRect.left + 24.f, m_qrCopyBtnRect.top,
                                  m_qrCopyBtnRect.right - 4.f, m_qrCopyBtnRect.bottom), m_labelFgBrush.Get());
    } else {
        m_qrOpenBtnRect = {};
        m_qrCopyBtnRect = D2D1::RectF(tipX + tipW - 90.f, tipY + tipH - 30.f, tipX + tipW - 8.f, tipY + tipH - 6.f);
        D2D1_ROUNDED_RECT copyR = D2D1::RoundedRect(m_qrCopyBtnRect, 4.f, 4.f);
        dc->FillRoundedRectangle(copyR, m_toolbarActiveBrush.Get());
        if (m_qrTooltipHoverBtn == 2) {
            ComPtr<ID2D1SolidColorBrush> hl;
            dc->CreateSolidColorBrush(D2D1::ColorF(1.f, 1.f, 1.f, 0.15f), &hl);
            dc->FillRoundedRectangle(copyR, hl.Get());
        }
        DrawFluentGlyph(dc, L'\uE8C8', D2D1::RectF(m_qrCopyBtnRect.left + 8.f, m_qrCopyBtnRect.top,
                                                   m_qrCopyBtnRect.left + 24.f, m_qrCopyBtnRect.bottom),
                        m_labelFgBrush.Get(), g_dwrite.IconFont14());
        const WCHAR* copyText = L"Copy";
        dc->DrawTextW(copyText, static_cast<UINT32>(wcslen(copyText)), g_dwrite.UIFont(),
                      D2D1::RectF(m_qrCopyBtnRect.left + 26.f, m_qrCopyBtnRect.top,
                                  m_qrCopyBtnRect.right - 6.f, m_qrCopyBtnRect.bottom), m_labelFgBrush.Get());
    }
}

int OverlayWindow::GetClosestOcrCharIndex(D2D1_POINT_2F pt) const noexcept {
    if (m_ocrResult.chars.empty()) return -1;
    RECT sel = m_selection.NormalizedRect();
    float sx = static_cast<float>(sel.left - m_monitorRect.left);
    float sy = static_cast<float>(sel.top  - m_monitorRect.top);

    int bestIdx = 0;
    float minDistance = 1e9f;
    for (size_t i = 0; i < m_ocrResult.chars.size(); ++i) {
        const auto& c = m_ocrResult.chars[i];
        float cx = sx + c.bounds.left + (c.bounds.right - c.bounds.left) * 0.5f;
        float cy = sy + c.bounds.top  + (c.bounds.bottom - c.bounds.top) * 0.5f;
        float dx = pt.x - cx;
        float dy = pt.y - cy;
        // Weight vertical distance slightly higher than horizontal for natural line snapping
        float dist = (dx * dx) + (dy * dy * 4.0f);
        if (dist < minDistance) {
            minDistance = dist;
            bestIdx = static_cast<int>(i);
        }
    }
    return bestIdx;
}

std::wstring OverlayWindow::GetSelectedOcrText() const noexcept {
    if (m_selectedOcrCharIndices.empty()) {
        return m_ocrResult.fullText;
    }
    std::vector<int> sorted = m_selectedOcrCharIndices;
    std::sort(sorted.begin(), sorted.end());

    std::wstring result;
    int currentLineIdx = -1;
    int currentWordIdx = -1;
    std::wstring currentLine;

    for (int idx : sorted) {
        if (idx < 0 || idx >= static_cast<int>(m_ocrResult.chars.size())) continue;
        const auto& c = m_ocrResult.chars[idx];
        if (currentLineIdx != -1 && c.lineIndex != currentLineIdx) {
            if (!result.empty()) result += L"\r\n";
            result += currentLine;
            currentLine.clear();
            currentWordIdx = -1;
        }
        if (currentWordIdx != -1 && c.wordIndex != currentWordIdx) {
            if (!currentLine.empty() && currentLine.back() != L' ') {
                currentLine += L' ';
            }
        }
        currentLine += c.character;
        currentLineIdx = c.lineIndex;
        currentWordIdx = c.wordIndex;
    }
    if (!currentLine.empty()) {
        if (!result.empty()) result += L"\r\n";
        result += currentLine;
    }
    return result;
}

void OverlayWindow::RenderOcrOverlay(ID2D1DeviceContext* dc) noexcept {
    RECT sel = m_selection.NormalizedRect();
    float sx = static_cast<float>(sel.left - m_monitorRect.left);
    float sy = static_cast<float>(sel.top  - m_monitorRect.top);
    float sw = static_cast<float>(sel.right - sel.left);
    float sh = static_cast<float>(sel.bottom - sel.top);

    // 1. If loading: render a centered "Extracting text..." badge
    if (m_ocrLoading) {
        float pillW = 200.f, pillH = 36.f;
        float px = sx + (sw - pillW) * 0.5f;
        float py = sy + (sh - pillH) * 0.5f;
        if (py < 40.f) py = sy + 10.f;

        D2D1_ROUNDED_RECT loaderRect = D2D1::RoundedRect(D2D1::RectF(px, py, px + pillW, py + pillH), 18.f, 18.f);
        dc->FillRoundedRectangle(loaderRect, m_toolbarBgBrush.Get());
        dc->DrawRoundedRectangle(loaderRect, m_toolbarBorderBrush.Get(), 1.5f);

        DrawFluentGlyph(dc, L'\uF714', D2D1::RectF(px + 14.f, py + 8.f, px + 34.f, py + 28.f),
                        m_toolbarActiveBrush.Get());
        const WCHAR* loadText = L"Extracting text...";
        dc->DrawTextW(loadText, static_cast<UINT32>(wcslen(loadText)), g_dwrite.UIFont(),
                      D2D1::RectF(px + 40.f, py + 8.f, px + pillW - 10.f, py + 28.f), m_labelFgBrush.Get());
        return;
    }

    // Render distinct indicators over detected text lines
    ComPtr<ID2D1SolidColorBrush> lineFillBrush;
    ComPtr<ID2D1SolidColorBrush> lineBorderBrush;
    dc->CreateSolidColorBrush(D2D1::ColorF(m_toolbarAccent.r, m_toolbarAccent.g, m_toolbarAccent.b, 0.16f), &lineFillBrush);
    dc->CreateSolidColorBrush(D2D1::ColorF(m_toolbarAccent.r, m_toolbarAccent.g, m_toolbarAccent.b, 0.45f), &lineBorderBrush);

    for (const auto& line : m_ocrResult.lines) {
        D2D1_RECT_F lr = D2D1::RectF(sx + line.bounds.left, sy + line.bounds.top,
                                     sx + line.bounds.right, sy + line.bounds.bottom);
        dc->FillRoundedRectangle(D2D1::RoundedRect(lr, 3.f, 3.f), lineFillBrush.Get());
        dc->DrawRoundedRectangle(D2D1::RoundedRect(lr, 3.f, 3.f), lineBorderBrush.Get(), 1.0f);
    }

    // Render selection highlights for selected characters
    if (!m_selectedOcrCharIndices.empty()) {
        ComPtr<ID2D1SolidColorBrush> selFillBrush;
        ComPtr<ID2D1SolidColorBrush> selBorderBrush;
        dc->CreateSolidColorBrush(D2D1::ColorF(m_toolbarAccent.r, m_toolbarAccent.g, m_toolbarAccent.b, 0.52f), &selFillBrush);
        dc->CreateSolidColorBrush(m_toolbarAccent, &selBorderBrush);

        std::unordered_map<int, std::vector<D2D1_RECT_F>> lineChars;
        for (int idx : m_selectedOcrCharIndices) {
            if (idx >= 0 && idx < static_cast<int>(m_ocrResult.chars.size())) {
                const auto& c = m_ocrResult.chars[idx];
                lineChars[c.lineIndex].push_back(c.bounds);
            }
        }

        for (auto& [lIdx, boundsVec] : lineChars) {
            if (boundsVec.empty()) continue;
            std::sort(boundsVec.begin(), boundsVec.end(), [](const D2D1_RECT_F& a, const D2D1_RECT_F& b) {
                return a.left < b.left;
            });

            struct Cluster {
                float left, top, right, bottom;
            };
            std::vector<Cluster> clusters;
            for (const auto& b : boundsVec) {
                if (clusters.empty() || b.left > clusters.back().right + 8.0f) {
                    clusters.push_back({ b.left, b.top, b.right, b.bottom });
                } else {
                    clusters.back().right = std::max(clusters.back().right, b.right);
                    clusters.back().top = std::min(clusters.back().top, b.top);
                    clusters.back().bottom = std::max(clusters.back().bottom, b.bottom);
                }
            }

            for (const auto& cl : clusters) {
                D2D1_RECT_F selR = D2D1::RectF(sx + cl.left, sy + cl.top, sx + cl.right, sy + cl.bottom);
                dc->FillRoundedRectangle(D2D1::RoundedRect(selR, 2.f, 2.f), selFillBrush.Get());
                dc->DrawRoundedRectangle(D2D1::RoundedRect(selR, 2.f, 2.f), selBorderBrush.Get(), 1.0f);
            }
        }
    }

    // OCR Top Pill Bar
    float monW = static_cast<float>(m_monitorRect.right - m_monitorRect.left);

    const WCHAR* copyAllText = L"Copy all text";
    UINT32 copyAllLen = static_cast<UINT32>(wcslen(copyAllText));
    float copyTextW = 75.0f;
    if (g_dwrite.Factory() && g_dwrite.UIFont()) {
        ComPtr<IDWriteTextLayout> layout;
        if (SUCCEEDED(g_dwrite.Factory()->CreateTextLayout(
                copyAllText, copyAllLen, g_dwrite.UIFont(), 1000.0f, 100.0f, &layout))) {
            DWRITE_TEXT_METRICS tm{};
            if (SUCCEEDED(layout->GetMetrics(&tm))) {
                copyTextW = tm.widthIncludingTrailingWhitespace > 0.0f ? tm.widthIncludingTrailingWhitespace : tm.width;
            }
        }
    }

    float padLeft = 8.0f;
    float iconW = 18.0f;
    float gap = 5.0f;
    float copyBtnW = padLeft + iconW + gap + copyTextW + 10.0f;

    float pillW = 77.0f + copyBtnW;
    float pillH = 38.0f;
    float pillX = (monW - pillW) * 0.5f;
    float pillY = 24.0f;
    m_ocrTopPillRect = D2D1::RectF(pillX, pillY, pillX + pillW, pillY + pillH);

    D2D1_ROUNDED_RECT shadowR = D2D1::RoundedRect(
        D2D1::RectF(pillX - 2.f, pillY, pillX + pillW + 2.f, pillY + pillH + 4.f), 19.f, 19.f);
    dc->FillRoundedRectangle(shadowR, m_toolbarShadowBrush.Get());

    D2D1_ROUNDED_RECT pillR = D2D1::RoundedRect(m_ocrTopPillRect, 19.f, 19.f);
    dc->FillRoundedRectangle(pillR, m_toolbarBgBrush.Get());
    dc->DrawRoundedRectangle(pillR, m_toolbarBorderBrush.Get(), 1.5f);

    // Button 0: Back Arrow
    m_ocrBackBtnRect = D2D1::RectF(pillX + 4.f, pillY + 4.f, pillX + 34.f, pillY + 34.f);
    if (m_ocrHoverButton == 0) {
        dc->FillRoundedRectangle(D2D1::RoundedRect(m_ocrBackBtnRect, 15.f, 15.f), m_toolbarHoverBrush.Get());
    }
    DrawFluentGlyph(dc, L'\uE72B', D2D1::RectF(pillX + 4.f, pillY + 9.f, pillX + 34.f, pillY + 29.f),
                    m_labelFgBrush.Get());

    // Divider 1
    float div1X = pillX + 37.0f;
    dc->FillRectangle(D2D1::RectF(div1X, pillY + 10.f, div1X + 1.0f, pillY + 28.f), m_separatorBrush.Get());

    // Button 1: Copy all text
    float copyLeft = div1X + 4.0f;
    m_ocrCopyAllRect = D2D1::RectF(copyLeft, pillY + 4.f, copyLeft + copyBtnW, pillY + 34.f);
    if (m_ocrHoverButton == 1) {
        dc->FillRoundedRectangle(D2D1::RoundedRect(m_ocrCopyAllRect, 15.f, 15.f), m_toolbarHoverBrush.Get());
    }
    float icon1X = copyLeft + padLeft;
    DrawFluentGlyph(dc, L'\uE8C8', D2D1::RectF(icon1X, pillY + 10.f, icon1X + iconW, pillY + 28.f),
                    m_labelFgBrush.Get());
    float text1X = icon1X + iconW + gap;
    dc->DrawTextW(copyAllText, copyAllLen, g_dwrite.UIFont(),
                  D2D1::RectF(text1X, pillY + 9.f, text1X + copyTextW + 4.f, pillY + 29.f), m_labelFgBrush.Get());

    // Divider 2
    float div2X = m_ocrCopyAllRect.right + 4.0f;
    dc->FillRectangle(D2D1::RectF(div2X, pillY + 10.f, div2X + 1.0f, pillY + 28.f), m_separatorBrush.Get());

    // Button 3: Close
    float closeLeft = div2X + 4.0f;
    m_ocrCloseBtnRect = D2D1::RectF(closeLeft, pillY + 4.f, closeLeft + 30.f, pillY + 34.f);
    if (m_ocrHoverButton == 3) {
        dc->FillRoundedRectangle(D2D1::RoundedRect(m_ocrCloseBtnRect, 15.f, 15.f), m_toolbarCloseHoverBrush.Get());
    }
    DrawFluentGlyph(dc, L'\uE711', D2D1::RectF(closeLeft, pillY + 9.f, closeLeft + 30.f, pillY + 29.f),
                    m_labelFgBrush.Get());

    // Quick Floating Card
    if (m_ocrQuickActionVisible) {
        float qW = 96.f;
        float qH = 34.f;
        float qX = m_ocrQuickActionPos.x;
        float qY = m_ocrQuickActionPos.y;
        if (qX + qW > monW - 10.f) qX = monW - qW - 10.f;
        if (qX < 10.f) qX = 10.f;
        float monH = static_cast<float>(m_monitorRect.bottom - m_monitorRect.top);
        if (qY + qH > monH - 10.f) qY = monH - qH - 10.f;
        if (qY < 10.f) qY = 10.f;

        m_ocrQuickActionRect = D2D1::RectF(qX, qY, qX + qW, qY + qH);

        D2D1_ROUNDED_RECT qShadow = D2D1::RoundedRect(
            D2D1::RectF(qX - 2.f, qY, qX + qW + 2.f, qY + qH + 4.f), 17.f, 17.f);
        dc->FillRoundedRectangle(qShadow, m_toolbarShadowBrush.Get());

        D2D1_ROUNDED_RECT qCard = D2D1::RoundedRect(m_ocrQuickActionRect, 17.f, 17.f);
        dc->FillRoundedRectangle(qCard, m_toolbarBgBrush.Get());
        dc->DrawRoundedRectangle(qCard, m_toolbarBorderBrush.Get(), 1.5f);

        // Copy button
        D2D1_RECT_F copySubRect = D2D1::RectF(qX + 4.f, qY + 4.f, qX + qW - 4.f, qY + 30.f);
        if (m_ocrQuickActionHoverBtn == 1) {
            dc->FillRoundedRectangle(D2D1::RoundedRect(copySubRect, 13.f, 13.f), m_toolbarHoverBrush.Get());
        }
        DrawFluentGlyph(dc, L'\uE8C8', D2D1::RectF(qX + 8.f, qY + 8.f, qX + 26.f, qY + 26.f),
                        m_labelFgBrush.Get());
        dc->DrawTextW(L"Copy", 4, g_dwrite.UIFont(),
                      D2D1::RectF(qX + 30.f, qY + 8.f, qX + qW - 8.f, qY + 26.f), m_labelFgBrush.Get());
    }
}

RECT OverlayWindow::BufferRect(const RECT& screenSel) const noexcept {
    return RECT{
        screenSel.left   - m_monitorRect.left,
        screenSel.top    - m_monitorRect.top,
        screenSel.right  - m_monitorRect.left,
        screenSel.bottom - m_monitorRect.top
    };
}

// ── Rendering ─────────────────────────────────────────────────────────────────

void OverlayWindow::RenderFrame() noexcept {
    if (!m_desktopBitmap) return;

    auto* dc = g_d2d.D2DDevCtx();
    dc->SetTarget(m_d2dTarget.Get());
    dc->BeginDraw();
    dc->Clear(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.0f));

    RECT mr = m_monitorRect;
    D2D1_RECT_F destRect = D2D1::RectF(0.0f, 0.0f,
        static_cast<float>(mr.right - mr.left),
        static_cast<float>(mr.bottom - mr.top));
    dc->DrawBitmap(m_desktopBitmap.Get(), destRect, 1.0f,
                   D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR);

    if (m_captureMode != CaptureMode::ColorPicker) {
        DrawDimOverlay(dc);
    }

    if (m_captureMode == CaptureMode::Freeform && !IsEditorOpen())
        DrawLasso(dc);

    if (m_captureMode == CaptureMode::ColorPicker && !IsEditorOpen()) {
        POINT cursor{};
        GetCursorPos(&cursor);
        cursor.x -= mr.left;
        cursor.y -= mr.top;
        if (!PointInToolbar(cursor))
            DrawColorPickerHud(dc, cursor);
    }

    if (IsEditorOpen() || m_isReselecting || !m_annotations.empty()) {
        RenderAnnotations(dc);
        RenderPreview(dc);
    }

    DrawSelection(dc);

    if ((m_selection.phase == SelectionState::Phase::Dragging || m_resizeHandle != -1) && m_selection.IsValid() &&
        m_captureMode != CaptureMode::Freeform) {
        DrawSizeLabel(dc, m_selection.NormalizedRect());
    }

    if (m_ocrActive) {
        RenderOcrOverlay(dc);
    } else {
        RenderFloatingToolbar(dc);
        if (m_qrDetected && IsEditorOpen()) {
            RenderQrHighlight(dc);
        }
    }

    dc->EndDraw();
    m_swapChain->Present(0, 0);
}

void OverlayWindow::DrawDimOverlay(ID2D1DeviceContext* dc) noexcept {
    if (m_captureMode == CaptureMode::ColorPicker) return;

    float w = static_cast<float>(m_monitorRect.right  - m_monitorRect.left);
    float h = static_cast<float>(m_monitorRect.bottom - m_monitorRect.top);

    if (m_captureMode == CaptureMode::Freeform) {
        if (m_lassoPoints.size() >= 3 && !m_lassoDrawing) {
            ComPtr<ID2D1PathGeometry> path;
            ComPtr<ID2D1Factory> factory;
            dc->GetFactory(&factory);
            if (SUCCEEDED(factory->CreatePathGeometry(&path))) {
                ComPtr<ID2D1GeometrySink> sink;
                if (SUCCEEDED(path->Open(&sink))) {
                    sink->SetFillMode(D2D1_FILL_MODE_ALTERNATE);
                    sink->BeginFigure(D2D1::Point2F(0, 0), D2D1_FIGURE_BEGIN_FILLED);
                    sink->AddLine(D2D1::Point2F(w, 0));
                    sink->AddLine(D2D1::Point2F(w, h));
                    sink->AddLine(D2D1::Point2F(0, h));
                    sink->EndFigure(D2D1_FIGURE_END_CLOSED);

                    sink->BeginFigure(m_lassoPoints[0], D2D1_FIGURE_BEGIN_FILLED);
                    sink->AddLines(&m_lassoPoints[1], static_cast<UINT32>(m_lassoPoints.size() - 1));
                    sink->EndFigure(D2D1_FIGURE_END_CLOSED);

                    sink->Close();
                    dc->FillGeometry(path.Get(), m_dimBrush.Get());
                    return;
                }
            }
        }
        // While drawing lasso or before committing, the whole screen is uniformly dimmed
        dc->FillRectangle(D2D1::RectF(0, 0, w, h), m_dimBrush.Get());
        return;
    }

    RECT preview = {};
    bool hasPreview = false;
    if (m_selection.IsValid()) {
        preview = m_selection.NormalizedRect();
        hasPreview = true;
    }

    if (!hasPreview) {
        dc->FillRectangle(D2D1::RectF(0, 0, w, h), m_dimBrush.Get());
        return;
    }

    float sx = static_cast<float>(preview.left   - m_monitorRect.left);
    float sy = static_cast<float>(preview.top    - m_monitorRect.top);
    float sw = static_cast<float>(preview.right  - preview.left);
    float sh = static_cast<float>(preview.bottom - preview.top);

    if (sy > 0)
        dc->FillRectangle(D2D1::RectF(0, 0, w, sy), m_dimBrush.Get());
    if (sy + sh < h)
        dc->FillRectangle(D2D1::RectF(0, sy + sh, w, h), m_dimBrush.Get());
    if (sx > 0)
        dc->FillRectangle(D2D1::RectF(0, sy, sx, sy + sh), m_dimBrush.Get());
    if (sx + sw < w)
        dc->FillRectangle(D2D1::RectF(sx + sw, sy, w, sy + sh), m_dimBrush.Get());
}

void OverlayWindow::DrawLasso(ID2D1DeviceContext* dc) noexcept {
    if (m_lassoPoints.size() < 2) return;
    for (size_t i = 1; i < m_lassoPoints.size(); ++i)
        dc->DrawLine(m_lassoPoints[i - 1], m_lassoPoints[i], m_handleBrush.Get(), 2.0f, m_roundStroke.Get());
}

static const WCHAR* GetFriendlyColorName(uint8_t r, uint8_t g, uint8_t b) noexcept {
    uint8_t maxVal = std::max({ r, g, b });
    uint8_t minVal = std::min({ r, g, b });
    uint8_t delta = maxVal - minVal;

    if (maxVal < 35) return L"Black";
    if (minVal > 225) return L"White";
    if (delta < 18) {
        if (maxVal > 180) return L"Light Gray";
        if (maxVal > 100) return L"Gray";
        return L"Dark Gray";
    }

    double hue = 0.0;
    if (delta > 0) {
        if (maxVal == r) {
            hue = (static_cast<double>(g) - static_cast<double>(b)) / delta;
            hue = fmod(hue, 6.0);
        } else if (maxVal == g) {
            hue = ((static_cast<double>(b) - static_cast<double>(r)) / delta) + 2.0;
        } else {
            hue = ((static_cast<double>(r) - static_cast<double>(g)) / delta) + 4.0;
        }
        hue *= 60.0;
        if (hue < 0.0) hue += 360.0;
    }

    if (hue >= 345.0 || hue < 15.0) return L"Red";
    if (hue >= 15.0 && hue < 45.0) return L"Orange";
    if (hue >= 45.0 && hue < 70.0) return L"Yellow";
    if (hue >= 70.0 && hue < 165.0) return L"Green";
    if (hue >= 165.0 && hue < 200.0) return L"Cyan";
    if (hue >= 200.0 && hue < 260.0) return L"Blue";
    if (hue >= 260.0 && hue < 310.0) return L"Purple";
    if (hue >= 310.0 && hue < 345.0) return L"Pink";

    return L"Color";
}

void OverlayWindow::DrawColorPickerHud(ID2D1DeviceContext* dc, POINT local) noexcept {
    uint8_t r = m_hoverR, g = m_hoverG, b = m_hoverB;
    m_swatchBrush->SetColor(D2D1::ColorF(r / 255.f, g / 255.f, b / 255.f, 1.f));

    float cx = static_cast<float>(local.x);
    float cy = static_cast<float>(local.y);
    float x = cx + 18.f;
    float y = cy + 18.f;
    float w = static_cast<float>(m_monitorRect.right - m_monitorRect.left);
    float h = static_cast<float>(m_monitorRect.bottom - m_monitorRect.top);
    const float cardW = 124.f;
    const float cardH = 42.f;
    if (x + cardW > w) x = cx - (cardW + 18.f);
    if (y + cardH > h) y = cy - (cardH + 18.f);
    if (x < 0.f) x = 0.f;
    if (y < 0.f) y = 0.f;

    D2D1_ROUNDED_RECT card = D2D1::RoundedRect(D2D1::RectF(x, y, x + cardW, y + cardH), 6.f, 6.f);
    dc->FillRoundedRectangle(card, m_toolbarBgBrush.Get());
    dc->DrawRoundedRectangle(card, m_toolbarBorderBrush.Get(), 1.f);

    D2D1_ROUNDED_RECT swatch = D2D1::RoundedRect(D2D1::RectF(x + 8.f, y + 8.f, x + 34.f, y + 34.f), 4.f, 4.f);
    dc->FillRoundedRectangle(swatch, m_swatchBrush.Get());
    dc->DrawRoundedRectangle(swatch, m_toolbarBorderBrush.Get(), 1.f);

    WCHAR hex[32];
    swprintf_s(hex, L"#%02X%02X%02X", r, g, b);
    dc->DrawTextW(hex, static_cast<UINT32>(wcslen(hex)), g_dwrite.UIFontBold(),
                  D2D1::RectF(x + 42.f, y + 5.f, x + cardW - 8.f, y + 22.f), m_labelFgBrush.Get());

    const WCHAR* name = GetFriendlyColorName(r, g, b);
    dc->DrawTextW(name, static_cast<UINT32>(wcslen(name)), g_dwrite.UIFontSmall(),
                  D2D1::RectF(x + 42.f, y + 22.f, x + cardW - 8.f, y + 37.f), m_guideBrush.Get());
}

void OverlayWindow::DrawSelection(ID2D1DeviceContext* dc) noexcept {
    if (!m_selection.IsValid()) return;

    if (m_captureMode == CaptureMode::Freeform) {
        if (m_lassoPoints.size() >= 3) {
            ComPtr<ID2D1PathGeometry> path;
            ComPtr<ID2D1Factory> factory;
            dc->GetFactory(&factory);
            if (SUCCEEDED(factory->CreatePathGeometry(&path))) {
                ComPtr<ID2D1GeometrySink> sink;
                if (SUCCEEDED(path->Open(&sink))) {
                    sink->BeginFigure(m_lassoPoints[0], D2D1_FIGURE_BEGIN_HOLLOW);
                    sink->AddLines(&m_lassoPoints[1], static_cast<UINT32>(m_lassoPoints.size() - 1));
                    sink->EndFigure(D2D1_FIGURE_END_CLOSED);
                    sink->Close();
                    dc->DrawGeometry(path.Get(), m_selBorderBrush.Get(), m_selectionBorderThickness, m_dashStroke.Get());
                }
            }
        }
        return;
    }

    RECT sel = m_selection.NormalizedRect();
    float sx = static_cast<float>(sel.left   - m_monitorRect.left);
    float sy = static_cast<float>(sel.top    - m_monitorRect.top);
    float sw = static_cast<float>(sel.right  - sel.left);
    float sh = static_cast<float>(sel.bottom - sel.top);

    dc->DrawRectangle(D2D1::RectF(sx, sy, sx + sw, sy + sh),
                      m_selBorderBrush.Get(), m_selectionBorderThickness);

    auto handle = [&](float x, float y) {
        float half = kHandleSize * 0.5f;
        dc->FillRectangle(D2D1::RectF(x - half, y - half, x + half, y + half),
                          m_labelFgBrush.Get());
        dc->DrawRectangle(D2D1::RectF(x - half, y - half, x + half, y + half),
                          m_selBorderBrush.Get(), 1.5f);
    };
    handle(sx,        sy);
    handle(sx + sw,   sy);
    handle(sx,        sy + sh);
    handle(sx + sw,   sy + sh);
    handle(sx + sw / 2, sy);
    handle(sx + sw / 2, sy + sh);
    handle(sx,          sy + sh / 2);
    handle(sx + sw,     sy + sh / 2);
}

void OverlayWindow::DrawSizeLabel(ID2D1DeviceContext* dc, const RECT& sel) noexcept {
    int w = sel.right  - sel.left;
    int h = sel.bottom - sel.top;

    WCHAR label[32];
    ::swprintf_s(label, L"%d \u00D7 %d px", w, h);

    float sx = static_cast<float>(sel.left - m_monitorRect.left);
    float sy = static_cast<float>(sel.top  - m_monitorRect.top);

    ComPtr<IDWriteTextLayout> layout;
    g_dwrite.Factory()->CreateTextLayout(
        label, static_cast<UINT32>(wcslen(label)),
        g_dwrite.UIFontSmall(), 1000.0f, 100.0f, &layout);
    DWRITE_TEXT_METRICS tm{};
    if (layout) layout->GetMetrics(&tm);
    float textW = layout ? tm.width : 80.0f;
    float labelW = std::max(90.0f, textW + 16.0f);
    float labelH = 22.0f;

    float lx = sx;
    float ly = sy - labelH - 6.0f;
    if (ly < 4.0f) ly = sy + 6.0f;

    D2D1_ROUNDED_RECT pill = D2D1::RoundedRect(
        D2D1::RectF(lx, ly, lx + labelW, ly + labelH), 4.0f, 4.0f);

    dc->FillRoundedRectangle(pill, m_labelBgBrush.Get());
    if (layout) {
        float tx = lx + (labelW - textW) * 0.5f;
        float ty = ly + (labelH - tm.height) * 0.5f;
        dc->DrawTextLayout(D2D1::Point2F(tx, ty), layout.Get(), m_labelFgBrush.Get());
    } else {
        dc->DrawTextW(label, static_cast<UINT32>(wcslen(label)),
                      g_dwrite.UIFontSmall(),
                      D2D1::RectF(lx + kLabelPadX, ly + kLabelPadY,
                                  lx + labelW - kLabelPadX, ly + labelH),
                      m_labelFgBrush.Get());
    }
}

int OverlayWindow::HitTestSelectionHandle(POINT screenPt) const noexcept {
    if (m_selection.phase != SelectionState::Phase::Editing || !m_selection.IsValid())
        return -1;
    if (m_captureMode == CaptureMode::Freeform)
        return -1;

    RECT sel = m_selection.NormalizedRect();
    const POINT handles[] = {
        { sel.left, sel.top },
        { (sel.left + sel.right) / 2, sel.top },
        { sel.right, sel.top },
        { sel.right, (sel.top + sel.bottom) / 2 },
        { sel.right, sel.bottom },
        { (sel.left + sel.right) / 2, sel.bottom },
        { sel.left, sel.bottom },
        { sel.left, (sel.top + sel.bottom) / 2 }
    };

    constexpr long long hitRadiusSquared = 14LL * 14LL;
    long long nearestDistance = hitRadiusSquared + 1;
    int nearestHandle = -1;
    for (int i = 0; i < 8; ++i) {
        long long dx = static_cast<long long>(screenPt.x) - handles[i].x;
        long long dy = static_cast<long long>(screenPt.y) - handles[i].y;
        long long distance = dx * dx + dy * dy;
        if (distance < nearestDistance) {
            nearestDistance = distance;
            nearestHandle = i;
        }
    }
    return nearestHandle;
}

void OverlayWindow::ResizeSelection(POINT screenPt) noexcept {
    RECT sel = m_resizeStartRect;
    LONG x = std::max(m_monitorRect.left, std::min(screenPt.x, m_monitorRect.right));
    LONG y = std::max(m_monitorRect.top, std::min(screenPt.y, m_monitorRect.bottom));

    if (m_resizeHandle == 8) {
        LONG width = sel.right - sel.left;
        LONG height = sel.bottom - sel.top;
        LONG left = std::max(m_monitorRect.left,
            std::min(m_resizeStartRect.left + x - m_resizeStartPoint.x, m_monitorRect.right - width));
        LONG top = std::max(m_monitorRect.top,
            std::min(m_resizeStartRect.top + y - m_resizeStartPoint.y, m_monitorRect.bottom - height));
        LONG dx = left - m_selection.NormalizedRect().left;
        LONG dy = top - m_selection.NormalizedRect().top;
        if (m_captureMode == CaptureMode::Freeform && (dx != 0 || dy != 0)) {
            for (auto& pt : m_lassoPoints) {
                pt.x += static_cast<float>(dx);
                pt.y += static_cast<float>(dy);
            }
        }
        sel = { left, top, left + width, top + height };
    } else switch (m_resizeHandle) {
    case 0: sel.left = std::min(x, sel.right - 4); sel.top = std::min(y, sel.bottom - 4); break;
    case 1: sel.top = std::min(y, sel.bottom - 4); break;
    case 2: sel.right = std::max(x, sel.left + 4); sel.top = std::min(y, sel.bottom - 4); break;
    case 3: sel.right = std::max(x, sel.left + 4); break;
    case 4: sel.right = std::max(x, sel.left + 4); sel.bottom = std::max(y, sel.top + 4); break;
    case 5: sel.bottom = std::max(y, sel.top + 4); break;
    case 6: sel.left = std::min(x, sel.right - 4); sel.bottom = std::max(y, sel.top + 4); break;
    case 7: sel.left = std::min(x, sel.right - 4); break;
    default: return;
    }

    m_selection.anchor = { sel.left, sel.top };
    m_selection.current = { sel.right, sel.bottom };
}

// ── Input handling ────────────────────────────────────────────────────────────

void OverlayWindow::OnLButtonDown(POINT pt) noexcept {
    POINT local{ pt.x - m_monitorRect.left, pt.y - m_monitorRect.top };
    SetForegroundWindow(m_hwnd);
    SetActiveWindow(m_hwnd);
    SetFocus(m_hwnd);

    if (m_qrDetected && m_qrHovered) {
        if (m_qrResult.isUrl &&
            local.x >= m_qrOpenBtnRect.left && local.x <= m_qrOpenBtnRect.right &&
            local.y >= m_qrOpenBtnRect.top && local.y <= m_qrOpenBtnRect.bottom) {
            ShellExecuteW(nullptr, L"open", m_qrResult.url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            Clipboard::WriteText(m_qrResult.url.c_str(), m_hwnd);
            Hide();
            return;
        }
        if (local.x >= m_qrCopyBtnRect.left && local.x <= m_qrCopyBtnRect.right &&
            local.y >= m_qrCopyBtnRect.top && local.y <= m_qrCopyBtnRect.bottom) {
            const auto& str = m_qrResult.isUrl ? m_qrResult.url : m_qrResult.payload;
            Clipboard::WriteText(str.c_str(), m_hwnd);
            return;
        }
    }

    if (m_ocrActive) {
        // 1. Check click on OcrTopPillBar:
        if (local.x >= m_ocrTopPillRect.left && local.x <= m_ocrTopPillRect.right &&
            local.y >= m_ocrTopPillRect.top && local.y <= m_ocrTopPillRect.bottom) {
            float lx = static_cast<float>(local.x);
            float ly = static_cast<float>(local.y);

            if (lx >= m_ocrBackBtnRect.left && lx <= m_ocrBackBtnRect.right &&
                ly >= m_ocrBackBtnRect.top  && ly <= m_ocrBackBtnRect.bottom) {
                // Back button
                m_ocrActive = false;
                m_selection.Reset();
                m_ocrResult = {};
                m_selectedOcrCharIndices.clear();
                m_ocrQuickActionVisible = false;
                RenderFrame();
                return;
            } else if (lx >= m_ocrCopyAllRect.left && lx <= m_ocrCopyAllRect.right &&
                       ly >= m_ocrCopyAllRect.top  && ly <= m_ocrCopyAllRect.bottom) {
                // Copy all text
                Clipboard::WriteText(m_ocrResult.fullText.c_str(), m_hwnd);
                Hide();
                if (OnCancel) OnCancel();
                return;
            } else if (lx >= m_ocrCloseBtnRect.left && lx <= m_ocrCloseBtnRect.right &&
                       ly >= m_ocrCloseBtnRect.top  && ly <= m_ocrCloseBtnRect.bottom) {
                // Close button
                Hide();
                if (OnCancel) OnCancel();
                return;
            }
            return;
        }

        // 2. Check click on OcrQuickActionBar:
        if (m_ocrQuickActionVisible &&
            local.x >= m_ocrQuickActionRect.left && local.x <= m_ocrQuickActionRect.right &&
            local.y >= m_ocrQuickActionRect.top && local.y <= m_ocrQuickActionRect.bottom) {
            std::wstring txt = GetSelectedOcrText();
            Clipboard::WriteText(txt.c_str(), m_hwnd);
            Hide();
            if (OnCancel) OnCancel();
            return;
        }

        // 2b. Check click on selection resize handles:
        int resizeHandle = HitTestSelectionHandle(pt);
        if (resizeHandle != -1) {
            m_resizeHandle = resizeHandle;
            m_resizeStartRect = m_selection.NormalizedRect();
            m_resizeStartPoint = pt;
            m_isOcrSelecting = false;
            m_ocrQuickActionVisible = false;
            return;
        }

        // 3. Inside snippet selection
        RECT sel = m_selection.NormalizedRect();
        bool inside = pt.x >= sel.left && pt.x < sel.right && pt.y >= sel.top && pt.y < sel.bottom;
        if (inside) {
            m_isOcrSelecting = true;
            m_ocrDragStartPos = local;
            m_selectedOcrCharIndices.clear();
            m_ocrQuickActionVisible = false;
            RenderFrame();
            return;
        } else {
            m_ocrQuickActionVisible = false;
            RenderFrame();
            return;
        }
    }

    if (HandleToolbarPopupClick(local)) {
        RenderFrame();
        return;
    }
    ToolbarPopup prevPopup = m_toolbarPopup;
    if (m_toolbarPopup != ToolbarPopup::None) {
        m_toolbarPopup = ToolbarPopup::None;
        m_fontDropdownOpen = false;
        m_fontSizeDragging = false;
        m_penSizeDragging = false;
        m_colorSpectrumDragging = false;
        m_colorHueDragging = false;
    }

    if (PointInToolbar(local)) {
        int btnId = HitTestToolbar(local);
        if (btnId != -1) {
            const auto id = static_cast<ToolbarButton>(btnId);
            auto found = std::find_if(m_toolbar.begin(), m_toolbar.end(),
                [&](const TBButton& button) { return button.id == id; });
            bool openPopup = false;
            ToolbarPopup popup = ToolbarPopup::None;
            if (found != m_toolbar.end() && found->hasDropdown) {
                const bool onChevron = local.x >= found->rect.right - 23.f;

                // Dropdown popup is only triggered by clicking the chevron
                // (or AnnotationColor which is dedicated to color popup)
                if (id == ToolbarButton::AnnotationColor || onChevron) {
                    switch (id) {
                    case ToolbarButton::Pen: popup = ToolbarPopup::Pen; break;
                    case ToolbarButton::Shapes: popup = ToolbarPopup::Shapes; break;
                    case ToolbarButton::Blur: popup = ToolbarPopup::Blur; break;
                    case ToolbarButton::Text: popup = ToolbarPopup::Text; break;
                    case ToolbarButton::AnnotationColor: popup = ToolbarPopup::Color; break;
                    default: break;
                    }
                    openPopup = (popup != ToolbarPopup::None);
                }
            }
            if (openPopup) {
                const bool isSamePopup = (prevPopup == popup) ||
                    (popup == ToolbarPopup::Color && prevPopup == ToolbarPopup::CustomColorPicker);
                m_toolbarPopup = isSamePopup ? ToolbarPopup::None : popup;
                RenderFrame();
            } else {
                OnToolbarClick(btnId);
            }
        }
        return;
    }

    if (prevPopup != ToolbarPopup::None) {
        RenderFrame();
        return;
    }

    if (m_captureMode == CaptureMode::ColorPicker && !IsEditorOpen()) {
        PickColorAt(pt);
        return;
    }

    int resizeHandle = HitTestSelectionHandle(pt);
    if (resizeHandle != -1) {
        m_resizeHandle = resizeHandle;
        m_resizeStartRect = m_selection.NormalizedRect();
        return;
    }

    if (IsEditorOpen()) {
        D2D1_POINT_2F cpt = { static_cast<float>(local.x), static_cast<float>(local.y) };
        RECT sel = m_selection.NormalizedRect();
        bool inside = (m_captureMode == CaptureMode::Freeform)
            ? IsPointInFreeform(cpt)
            : (pt.x >= sel.left && pt.x < sel.right && pt.y >= sel.top && pt.y < sel.bottom);

        if (m_toolMode == ToolMode::Eraser) {
            m_erasing = true;
            m_eraseUndoPushed = false;
            m_selectedAnnotationIndex = -1;
            EraseAt(cpt);
            RenderFrame();
            return;
        }

        if (m_toolMode == ToolMode::Select) {
            int annHandle = HitTestSelectedAnnotationHandle(cpt);
            if (annHandle != -1) {
                m_annotationResizeHandle = annHandle;
                m_annotationDragUndoPushed = false;
                m_dragStartPoint = cpt;
                m_dragStartAnnotation = m_annotations[m_selectedAnnotationIndex];
                m_annotationStartBounds = GetAnnotationBounds(m_dragStartAnnotation);
                return;
            }

            // If an annotation is already selected, clicking anywhere inside its selection box drags it
            if (m_selectedAnnotationIndex >= 0 && m_selectedAnnotationIndex < static_cast<int>(m_annotations.size())) {
                D2D1_RECT_F selB = GetAnnotationBounds(m_annotations[m_selectedAnnotationIndex]);
                selB.left -= 4.f; selB.top -= 4.f; selB.right += 4.f; selB.bottom += 4.f;
                if ((cpt.x >= selB.left && cpt.x <= selB.right &&
                     cpt.y >= selB.top && cpt.y <= selB.bottom) ||
                    AnnotationHit(m_annotations[m_selectedAnnotationIndex], cpt)) {
                    m_annotationResizeHandle = 8;
                    m_annotationDragUndoPushed = false;
                    m_dragStartPoint = cpt;
                    m_dragStartAnnotation = m_annotations[m_selectedAnnotationIndex];
                    m_annotationStartBounds = GetAnnotationBounds(m_dragStartAnnotation);
                    return;
                }
            }

            int hitIndex = -1;
            for (int i = static_cast<int>(m_annotations.size()) - 1; i >= 0; --i) {
                bool hit = AnnotationHit(m_annotations[i], cpt);
                if (!hit && m_annotations[i].type == AnnotationType::FreeDraw) {
                    D2D1_RECT_F b = GetAnnotationBounds(m_annotations[i]);
                    hit = (cpt.x >= b.left - 4.f && cpt.x <= b.right + 4.f &&
                           cpt.y >= b.top - 4.f && cpt.y <= b.bottom + 4.f);
                }
                if (hit) {
                    hitIndex = i;
                    break;
                }
            }

            if (hitIndex != -1) {
                m_selectedAnnotationIndex = hitIndex;
                m_annotationResizeHandle = 8;
                m_annotationDragUndoPushed = false;
                m_dragStartPoint = cpt;
                m_dragStartAnnotation = m_annotations[hitIndex];
                m_annotationStartBounds = GetAnnotationBounds(m_dragStartAnnotation);

                if (m_annotations[hitIndex].type == AnnotationType::Text) {
                    const auto& ta = std::get<TextAnnotation>(m_annotations[hitIndex].data);
                    m_fontFamily = ta.fontFamily;
                    m_fontSize = ta.fontSize;
                }

                RenderFrame();
                return;
            }

            if (m_selectedAnnotationIndex != -1) {
                m_selectedAnnotationIndex = -1;
                RenderFrame();
            }

            if (!inside) {
                m_isReselecting = true;
                m_reselectPrevAnchor = m_selection.anchor;
                m_reselectPrevCurrent = m_selection.current;
                m_selection.BeginDrag(pt);
                RenderFrame();
                return;
            }

            m_resizeHandle = 8;
            m_resizeStartRect = sel;
            m_resizeStartPoint = pt;
            return;
        }

        if (m_toolMode == ToolMode::Text) {
            SetFocus(m_hwnd);
            if (m_textState.editing) {
                // If clicked inside active text box, update caret position
                if (TextTool_OnClick(m_textState, cpt, g_dwrite.Factory())) {
                    RenderFrame();
                    return;
                }

                // Clicked outside active text box: commit it
                Annotation ann{};
                if (TextTool_Commit(m_textState, ann)) {
                    PushUndoState();
                    m_annotations.push_back(ann);
                }
                KillTimer(m_hwnd, 1001);
                RenderFrame();
                return;
            }

            // Not currently editing: check if clicked an existing text annotation
            for (auto it = m_annotations.rbegin(); it != m_annotations.rend(); ++it) {
                if (it->type == AnnotationType::Text && AnnotationHit(*it, cpt)) {
                    PushUndoState();
                    const auto& ta = std::get<TextAnnotation>(it->data);
                    TextTool_Start(m_textState, ta.origin, ta.color, ta.fontSize, ta.fontFamily, ta.text, ta.scaleX, ta.scaleY);
                    m_fontSize = ta.fontSize;
                    m_strokeColor = ta.color;
                    m_fontFamily = ta.fontFamily;
                    m_annotations.erase(std::next(it).base());
                    SetTimer(m_hwnd, 1001, 500, nullptr);
                    TextTool_OnClick(m_textState, cpt, g_dwrite.Factory());
                    RenderFrame();
                    return;
                }
            }

            // Clicked empty area: start new text box
            TextTool_Start(m_textState, cpt, m_strokeColor, m_fontSize, m_fontFamily);
            SetTimer(m_hwnd, 1001, 500, nullptr);
            RenderFrame();
            return;
        }

        m_dragging = true;
        switch (m_toolMode) {
        case ToolMode::Rect:
        case ToolMode::FilledRect:
        case ToolMode::Ellipse:
            RectTool_OnDown(m_rectState, cpt);
            break;
        case ToolMode::Line:
        case ToolMode::Arrow:
            ArrowTool_OnDown(m_arrowState, cpt);
            break;
        case ToolMode::Blur:
            BlurTool_OnDown(m_blurState, cpt);
            break;
        case ToolMode::FreeDraw:
        case ToolMode::Highlighter:
            m_freeDrawState.points.clear();
            m_freeDrawState.points.push_back(cpt);
            m_freeDrawState.active = true;
            break;
        case ToolMode::Counter: {
            int nextStep = GetNextCounterNumber();
            m_counterState.start = cpt;
            m_counterState.current = cpt;
            m_counterState.step = nextStep;
            m_counterState.active = true;
            break;
        }
        default:
            m_dragging = false;
            break;
        }
        return;
    }

    if (m_captureMode == CaptureMode::Freeform) {
        m_lassoPoints.clear();
        m_lassoPoints.push_back(D2D1::Point2F(static_cast<float>(local.x), static_cast<float>(local.y)));
        m_lassoDrawing = true;
        RenderFrame();
        return;
    }

    if (m_captureMode == CaptureMode::Region || m_captureMode == CaptureMode::TextExtract) {
        m_isReselecting = false;
        m_selection.BeginDrag(pt);
        RenderFrame();
    }
}

static D2D1_COLOR_F HsvToRgb(float h, float s, float v, float a = 1.0f) noexcept {
    float c = v * s;
    float hPrime = fmodf(h / 60.0f, 6.0f);
    if (hPrime < 0.0f) hPrime += 6.0f;
    float x = c * (1.0f - fabsf(fmodf(hPrime, 2.0f) - 1.0f));
    float m = v - c;
    float r = 0, g = 0, b = 0;
    if (hPrime < 1.0f)      { r = c; g = x; b = 0; }
    else if (hPrime < 2.0f) { r = x; g = c; b = 0; }
    else if (hPrime < 3.0f) { r = 0; g = c; b = x; }
    else if (hPrime < 4.0f) { r = 0; g = x; b = c; }
    else if (hPrime < 5.0f) { r = x; g = 0; b = c; }
    else                    { r = c; g = 0; b = x; }
    return D2D1::ColorF(std::clamp(r + m, 0.0f, 1.0f),
                        std::clamp(g + m, 0.0f, 1.0f),
                        std::clamp(b + m, 0.0f, 1.0f), a);
}

static void RgbToHsv(D2D1_COLOR_F color, float& h, float& s, float& v) noexcept {
    float rd = std::clamp(color.r, 0.0f, 1.0f);
    float gd = std::clamp(color.g, 0.0f, 1.0f);
    float bd = std::clamp(color.b, 0.0f, 1.0f);
    float maxVal = std::max({ rd, gd, bd });
    float minVal = std::min({ rd, gd, bd });
    float delta = maxVal - minVal;
    v = maxVal;
    s = (maxVal == 0.0f) ? 0.0f : (delta / maxVal);
    if (delta == 0.0f) {
        h = 0.0f;
    } else {
        if (rd == maxVal) h = (gd - bd) / delta + (gd < bd ? 6.0f : 0.0f);
        else if (gd == maxVal) h = (bd - rd) / delta + 2.0f;
        else h = (rd - gd) / delta + 4.0f;
        h *= 60.0f;
    }
}

static void RgbToHsl(D2D1_COLOR_F color, int& h, int& s, int& l) noexcept {
    float rd = std::clamp(color.r, 0.0f, 1.0f);
    float gd = std::clamp(color.g, 0.0f, 1.0f);
    float bd = std::clamp(color.b, 0.0f, 1.0f);
    float maxVal = std::max({ rd, gd, bd });
    float minVal = std::min({ rd, gd, bd });
    float delta = maxVal - minVal;
    float lVal = (maxVal + minVal) * 0.5f;
    float sVal = 0.0f;
    float hDeg = 0.0f;
    if (delta != 0.0f) {
        sVal = lVal < 0.5f ? delta / (maxVal + minVal) : delta / (2.0f - maxVal - minVal);
        if (rd == maxVal) hDeg = (gd - bd) / delta + (gd < bd ? 6.0f : 0.0f);
        else if (gd == maxVal) hDeg = (bd - rd) / delta + 2.0f;
        else hDeg = (rd - gd) / delta + 4.0f;
        hDeg /= 6.0f;
    }
    h = static_cast<int>(std::round(hDeg * 360.0f)) % 360;
    if (h < 0) h += 360;
    s = static_cast<int>(std::round(sVal * 100.0f));
    l = static_cast<int>(std::round(lVal * 100.0f));
}

void OverlayWindow::ApplyHexInput(const std::wstring& text) noexcept {
    std::wstring h = text;
    if (!h.empty() && h[0] == L'#') h = h.substr(1);
    while (h.size() < 6) h += L'0';
    if (h.size() > 6) h = h.substr(0, 6);
    unsigned int val = 0;
    if (swscanf_s(h.c_str(), L"%x", &val) == 1) {
        float r = ((val >> 16) & 0xFF) / 255.0f;
        float g = ((val >> 8) & 0xFF) / 255.0f;
        float b = (val & 0xFF) / 255.0f;
        m_customPickerColor = D2D1::ColorF(r, g, b, 1.0f);
        RgbToHsv(m_customPickerColor, m_customPickerHue, m_customPickerSat, m_customPickerVal);
        m_strokeColor = { r, g, b, 1.0f };
        if (m_strokeBrush) m_strokeBrush->SetColor(m_customPickerColor);
    }
}

void OverlayWindow::OnMouseMove(POINT pt) noexcept {
    m_hoverPt = pt;
    POINT local{ pt.x - m_monitorRect.left, pt.y - m_monitorRect.top };

    if (m_draggingColorPicker) {
        float dx = static_cast<float>(pt.x - m_colorPickerDragStartPt.x);
        float dy = static_cast<float>(pt.y - m_colorPickerDragStartPt.y);
        m_colorPickerPos.x += dx;
        m_colorPickerPos.y += dy;
        m_colorPickerDragStartPt = pt;
        RenderFrame();
        return;
    }

    if (m_qrDetected && IsEditorOpen()) {
        float lx = static_cast<float>(local.x);
        float ly = static_cast<float>(local.y);
        bool inHighlight = (lx >= m_qrHighlightRect.left && lx <= m_qrHighlightRect.right &&
                            ly >= m_qrHighlightRect.top  && ly <= m_qrHighlightRect.bottom);
        bool inBadge     = (lx >= m_qrBadgeRect.left     && lx <= m_qrBadgeRect.right &&
                            ly >= m_qrBadgeRect.top      && ly <= m_qrBadgeRect.bottom);
        bool inTooltip   = (lx >= m_qrTooltipRect.left   && lx <= m_qrTooltipRect.right &&
                            ly >= m_qrTooltipRect.top    && ly <= m_qrTooltipRect.bottom);

        bool inHoverArea = false;
        if (m_qrHovered) {
            float minX = std::min({ m_qrHighlightRect.left, m_qrBadgeRect.left, m_qrTooltipRect.left }) - 16.f;
            float maxX = std::max({ m_qrHighlightRect.right, m_qrBadgeRect.right, m_qrTooltipRect.right }) + 16.f;
            float minY = std::min({ m_qrHighlightRect.top, m_qrBadgeRect.top, m_qrTooltipRect.top }) - 16.f;
            float maxY = std::max({ m_qrHighlightRect.bottom, m_qrBadgeRect.bottom, m_qrTooltipRect.bottom }) + 16.f;
            inHoverArea = (lx >= minX && lx <= maxX && ly >= minY && ly <= maxY);
        }

        bool newHovered = inHighlight || inBadge || inTooltip || inHoverArea;
        int newBtn = 0;
        if (m_qrHovered && inTooltip) {
            if (m_qrResult.isUrl &&
                lx >= m_qrOpenBtnRect.left && lx <= m_qrOpenBtnRect.right &&
                ly >= m_qrOpenBtnRect.top  && ly <= m_qrOpenBtnRect.bottom) {
                newBtn = 1;
            } else if (lx >= m_qrCopyBtnRect.left && lx <= m_qrCopyBtnRect.right &&
                       ly >= m_qrCopyBtnRect.top  && ly <= m_qrCopyBtnRect.bottom) {
                newBtn = 2;
            }
        }

        if (newHovered != m_qrHovered || newBtn != m_qrTooltipHoverBtn) {
            m_qrHovered = newHovered;
            m_qrTooltipHoverBtn = newBtn;
            RenderFrame();
        }

        if (m_qrHovered && inTooltip) {
            if (newBtn != 0) {
                SetCursor(LoadCursorW(nullptr, IDC_HAND));
            } else {
                SetCursor(LoadCursorW(nullptr, IDC_ARROW));
            }
            return;
        }
    }

    if (m_ocrActive) {
        if (m_resizeHandle != -1) {
            ResizeSelection(pt);
            RenderFrame();
            return;
        }

        // 1. Check top pill bar hover
        if (local.x >= m_ocrTopPillRect.left && local.x <= m_ocrTopPillRect.right &&
            local.y >= m_ocrTopPillRect.top && local.y <= m_ocrTopPillRect.bottom) {
            int prevHover = m_ocrHoverButton;
            float lx = static_cast<float>(local.x);
            float ly = static_cast<float>(local.y);

            if (lx >= m_ocrBackBtnRect.left && lx <= m_ocrBackBtnRect.right &&
                ly >= m_ocrBackBtnRect.top  && ly <= m_ocrBackBtnRect.bottom) {
                m_ocrHoverButton = 0; // Back
            } else if (lx >= m_ocrCopyAllRect.left && lx <= m_ocrCopyAllRect.right &&
                       ly >= m_ocrCopyAllRect.top  && ly <= m_ocrCopyAllRect.bottom) {
                m_ocrHoverButton = 1; // Copy all
            } else if (lx >= m_ocrCloseBtnRect.left && lx <= m_ocrCloseBtnRect.right &&
                       ly >= m_ocrCloseBtnRect.top  && ly <= m_ocrCloseBtnRect.bottom) {
                m_ocrHoverButton = 3; // Close
            } else {
                m_ocrHoverButton = -1;
            }

            if (prevHover != m_ocrHoverButton) {
                RenderFrame();
            }
            SetCursor(LoadCursorW(nullptr, m_ocrHoverButton != -1 ? IDC_HAND : IDC_ARROW));
            return;
        } else {
            if (m_ocrHoverButton != -1) {
                m_ocrHoverButton = -1;
                RenderFrame();
            }
        }

        // 2. Check quick action hover
        if (m_ocrQuickActionVisible &&
            local.x >= m_ocrQuickActionRect.left && local.x <= m_ocrQuickActionRect.right &&
            local.y >= m_ocrQuickActionRect.top && local.y <= m_ocrQuickActionRect.bottom) {
            int prevHover = m_ocrQuickActionHoverBtn;
            m_ocrQuickActionHoverBtn = 1; // Copy
            if (prevHover != m_ocrQuickActionHoverBtn) {
                RenderFrame();
            }
            SetCursor(LoadCursorW(nullptr, IDC_HAND));
            return;
        } else {
            if (m_ocrQuickActionHoverBtn != 0) {
                m_ocrQuickActionHoverBtn = 0;
                RenderFrame();
            }
        }

        // 3. Drag select chars
        if (m_isOcrSelecting) {
            float dx = static_cast<float>(local.x - m_ocrDragStartPos.x);
            float dy = static_cast<float>(local.y - m_ocrDragStartPos.y);
            float dragDist = std::hypot(dx, dy);
            if (dragDist >= 4.0f) {
                RECT sel = m_selection.NormalizedRect();
                float sx = static_cast<float>(sel.left - m_monitorRect.left);
                float sy = static_cast<float>(sel.top  - m_monitorRect.top);
                float x1 = static_cast<float>(m_ocrDragStartPos.x) - sx;
                float y1 = static_cast<float>(m_ocrDragStartPos.y) - sy;
                float x2 = static_cast<float>(local.x) - sx;
                float y2 = static_cast<float>(local.y) - sy;

                float minX = std::min(x1, x2);
                float maxX = std::max(x1, x2);
                float minY = std::min(y1, y2);
                float maxY = std::max(y1, y2);

                if (maxY - minY < 22.0f) {
                    float pad = (22.0f - (maxY - minY)) * 0.5f;
                    minY -= pad;
                    maxY += pad;
                }

                m_selectedOcrCharIndices.clear();
                for (size_t i = 0; i < m_ocrResult.chars.size(); ++i) {
                    const auto& c = m_ocrResult.chars[i];
                    float cx = (c.bounds.left + c.bounds.right) * 0.5f;
                    float cy = (c.bounds.top + c.bounds.bottom) * 0.5f;
                    if (cx >= minX && cx <= maxX && cy >= minY && cy <= maxY) {
                        m_selectedOcrCharIndices.push_back(static_cast<int>(i));
                    }
                }
            }
            SetCursor(LoadCursorW(nullptr, IDC_IBEAM));
            RenderFrame();
            return;
        }

        // 3b. Check hover over selection resize handles
        int resizeHandle = HitTestSelectionHandle(pt);
        if (resizeHandle != -1) {
            LPCWSTR cursorId = (resizeHandle == 0 || resizeHandle == 4) ? IDC_SIZENWSE
                : (resizeHandle == 2 || resizeHandle == 6) ? IDC_SIZENESW
                : (resizeHandle == 1 || resizeHandle == 5) ? IDC_SIZENS
                : IDC_SIZEWE;
            SetCursor(LoadCursorW(nullptr, cursorId));
            return;
        }

        // 4. Inside snippet selection
        RECT sel = m_selection.NormalizedRect();
        if (pt.x >= sel.left && pt.x < sel.right && pt.y >= sel.top && pt.y < sel.bottom) {
            SetCursor(LoadCursorW(nullptr, IDC_IBEAM));
            return;
        }

        SetCursor(LoadCursorW(nullptr, IDC_ARROW));
        return;
    }
    if (m_colorSpectrumDragging) {
        float specLeft = m_popupBounds.left + 14.f;
        float specTop = m_popupBounds.top + 36.f;
        float specW = 252.f, specH = 160.f;
        m_customPickerSat = std::clamp((static_cast<float>(local.x) - specLeft) / specW, 0.0f, 1.0f);
        m_customPickerVal = std::clamp(1.0f - (static_cast<float>(local.y) - specTop) / specH, 0.0f, 1.0f);
        m_customPickerColor = HsvToRgb(m_customPickerHue, m_customPickerSat, m_customPickerVal);
        m_strokeColor = { m_customPickerColor.r, m_customPickerColor.g, m_customPickerColor.b, 1.0f };
        if (m_strokeBrush) m_strokeBrush->SetColor(m_customPickerColor);
        RenderFrame();
        return;
    }
    if (m_colorHueDragging) {
        float hueTop = m_popupBounds.top + 36.f;
        float hueH = 160.f;
        m_customPickerHue = std::clamp((static_cast<float>(local.y) - hueTop) / hueH, 0.0f, 1.0f) * 360.0f;
        if (m_customPickerHue >= 360.0f) m_customPickerHue = 0.0f;
        m_customPickerColor = HsvToRgb(m_customPickerHue, m_customPickerSat, m_customPickerVal);
        m_strokeColor = { m_customPickerColor.r, m_customPickerColor.g, m_customPickerColor.b, 1.0f };
        if (m_strokeBrush) m_strokeBrush->SetColor(m_customPickerColor);
        RenderFrame();
        return;
    }
    if (m_fontSizeDragging) {
        float sliderLeft = m_popupBounds.left + 18.f;
        float sliderWidth = m_popupBounds.right - m_popupBounds.left - 36.f;
        float value = (static_cast<float>(local.x) - sliderLeft) / sliderWidth;
        m_fontSize = 10.f + std::max(0.f, std::min(value, 1.f)) * 26.f;
        if (m_selectedAnnotationIndex >= 0 && m_selectedAnnotationIndex < static_cast<int>(m_annotations.size()) &&
            m_annotations[m_selectedAnnotationIndex].type == AnnotationType::Text) {
            std::get<TextAnnotation>(m_annotations[m_selectedAnnotationIndex].data).fontSize = m_fontSize;
        }
        if (m_textState.editing) {
            m_textState.fontSize = m_fontSize;
        }
        RenderFrame();
        return;
    }
    if (m_penSizeDragging) {
        float sliderLeft = m_popupBounds.left + 18.f;
        float sliderWidth = m_popupBounds.right - m_popupBounds.left - 36.f;
        float value = (static_cast<float>(local.x) - sliderLeft) / sliderWidth;
        m_strokeWidth = 1.f + std::max(0.f, std::min(value, 1.f)) * 23.f;
        RenderFrame();
        return;
    }
    UpdateCursor(local);

    if (m_annotationResizeHandle != -1) {
        if (!m_annotationDragUndoPushed) {
            PushUndoState();
            m_annotationDragUndoPushed = true;
        }
        D2D1_POINT_2F cpt{ static_cast<float>(local.x), static_cast<float>(local.y) };
        ResizeOrMoveAnnotation(cpt);
        RenderFrame();
        return;
    }

    if (m_resizeHandle != -1) {
        ResizeSelection(pt);
        RenderFrame();
        return;
    }

    if (m_lassoDrawing) {
        D2D1_POINT_2F cpt{ static_cast<float>(local.x), static_cast<float>(local.y) };
        if (m_lassoPoints.empty() || Dist2(m_lassoPoints.back(), cpt) >= 2.25f)
            m_lassoPoints.push_back(cpt);
        RenderFrame();
        return;
    }

    if (m_captureMode == CaptureMode::ColorPicker && !IsEditorOpen()) {
        SamplePixel(pt, m_hoverR, m_hoverG, m_hoverB);
        RenderFrame();
        return;
    }

    if (m_erasing) {
        EraseAt(D2D1::Point2F(static_cast<float>(local.x), static_cast<float>(local.y)));
        RenderFrame();
        return;
    }

    if (m_dragging && m_freeDrawState.active) {
        D2D1_POINT_2F cpt{ static_cast<float>(local.x), static_cast<float>(local.y) };
        if (m_freeDrawState.points.empty() || Dist2(m_freeDrawState.points.back(), cpt) >= 2.25f)
            m_freeDrawState.points.push_back(cpt);
        RenderFrame();
        return;
    }

    if (m_dragging && m_counterState.active && m_toolMode == ToolMode::Counter) {
        D2D1_POINT_2F cpt{ static_cast<float>(local.x), static_cast<float>(local.y) };
        m_counterState.current = cpt;
        RenderFrame();
        return;
    }

    if (IsEditorOpen()) {
        RenderFrame();
        return;
    }

    m_selection.UpdateDrag(pt);
    RenderFrame();
}

void OverlayWindow::OnLButtonUp(POINT pt) noexcept {
    m_erasing = false;
    m_eraseUndoPushed = false;

    if (m_ocrActive) {
        if (m_resizeHandle != -1) {
            ResizeSelection(pt);
            m_resizeHandle = -1;
            StartTextExtract();
            return;
        }
        if (m_isOcrSelecting) {
            m_isOcrSelecting = false;
            POINT local{ pt.x - m_monitorRect.left, pt.y - m_monitorRect.top };
            float dx = static_cast<float>(local.x - m_ocrDragStartPos.x);
            float dy = static_cast<float>(local.y - m_ocrDragStartPos.y);
            float dragDist = std::hypot(dx, dy);
            RECT sel = m_selection.NormalizedRect();
            float sx = static_cast<float>(sel.left - m_monitorRect.left);
            float sy = static_cast<float>(sel.top  - m_monitorRect.top);

            if (dragDist < 4.0f) {
                m_selectedOcrCharIndices.clear();
                int bestCharIdx = -1;
                float bestDistSq = 1e9f;
                for (size_t i = 0; i < m_ocrResult.chars.size(); ++i) {
                    const auto& c = m_ocrResult.chars[i];
                    D2D1_RECT_F cr = D2D1::RectF(sx + c.bounds.left - 2.f, sy + c.bounds.top - 2.f,
                                                 sx + c.bounds.right + 2.f, sy + c.bounds.bottom + 2.f);
                    if (local.x >= cr.left && local.x <= cr.right && local.y >= cr.top && local.y <= cr.bottom) {
                        bestCharIdx = static_cast<int>(i);
                        break;
                    }
                    float cx = (cr.left + cr.right) * 0.5f;
                    float cy = (cr.top + cr.bottom) * 0.5f;
                    float distSq = (local.x - cx) * (local.x - cx) + (local.y - cy) * (local.y - cy);
                    if (distSq < 144.0f && distSq < bestDistSq) {
                        bestDistSq = distSq;
                        bestCharIdx = static_cast<int>(i);
                    }
                }
                if (bestCharIdx != -1) {
                    m_selectedOcrCharIndices.push_back(bestCharIdx);
                }
            }

            if (!m_selectedOcrCharIndices.empty()) {
                std::wstring txt = GetSelectedOcrText();
                if (!txt.empty()) {
                    Clipboard::WriteText(txt.c_str(), m_hwnd);
                }
                float selMinX = 1e9f, selMaxX = -1e9f;
                float selMinY = 1e9f, selMaxY = -1e9f;
                for (int idx : m_selectedOcrCharIndices) {
                    if (idx >= 0 && idx < static_cast<int>(m_ocrResult.chars.size())) {
                        const auto& b = m_ocrResult.chars[idx].bounds;
                        if (b.left < selMinX) selMinX = b.left;
                        if (b.right > selMaxX) selMaxX = b.right;
                        if (b.top < selMinY) selMinY = b.top;
                        if (b.bottom > selMaxY) selMaxY = b.bottom;
                    }
                }
                float qX = sx + (selMinX + selMaxX) * 0.5f - 89.0f;
                float qY = sy + selMaxY + 10.0f;
                float monW = static_cast<float>(m_monitorRect.right - m_monitorRect.left);
                float monH = static_cast<float>(m_monitorRect.bottom - m_monitorRect.top);
                if (qY + 45.0f > monH || (qY + 45.0f > sy + static_cast<float>(sel.bottom - sel.top) && sy + selMinY - 45.0f > 40.0f)) {
                    qY = sy + selMinY - 42.0f;
                }
                qX = std::clamp(qX, 10.0f, monW - 190.0f);
                m_ocrQuickActionPos = { qX, qY };
                m_ocrQuickActionVisible = true;
            } else {
                m_ocrQuickActionVisible = false;
            }
            RenderFrame();
            return;
        }
        return;
    }

    if (m_draggingColorPicker) {
        m_draggingColorPicker = false;
        return;
    }
    if (m_colorSpectrumDragging) {
        m_colorSpectrumDragging = false;
        return;
    }
    if (m_colorHueDragging) {
        m_colorHueDragging = false;
        return;
    }
    if (m_fontSizeDragging) {
        m_fontSizeDragging = false;
        return;
    }
    if (m_penSizeDragging) {
        m_penSizeDragging = false;
        return;
    }

    if (m_annotationResizeHandle != -1) {
        m_annotationResizeHandle = -1;
        m_annotationDragUndoPushed = false;
        RenderFrame();
        return;
    }

    if (m_resizeHandle != -1) {
        ResizeSelection(pt);
        m_resizeHandle = -1;
        StartAutoQrScan();
        RenderFrame();
        return;
    }

    if (m_lassoDrawing) {
        POINT local{ pt.x - m_monitorRect.left, pt.y - m_monitorRect.top };
        m_lassoPoints.push_back(D2D1::Point2F(static_cast<float>(local.x), static_cast<float>(local.y)));
        CommitLassoSelection();
        if (!IsEditorOpen()) {
            m_lassoPoints.clear();
            m_lassoDrawing = false;
        }
        RenderFrame();
        return;
    }

    if (IsEditorOpen()) {
        if (!m_dragging) return;
        m_dragging = false;
        POINT local{ pt.x - m_monitorRect.left, pt.y - m_monitorRect.top };
        D2D1_POINT_2F cpt = { static_cast<float>(local.x), static_cast<float>(local.y) };
        Annotation ann{};
        bool added = false;
        switch (m_toolMode) {
        case ToolMode::Rect:
        case ToolMode::FilledRect:
            added = RectTool_OnUp(m_rectState, cpt, ann, m_strokeColor, m_shapeStrokeWidth);
            if (added && m_toolMode == ToolMode::FilledRect) {
                auto& rectangle = std::get<RectAnnotation>(ann.data);
                rectangle.fillColor = m_strokeColor;
                rectangle.fillColor.a = 1.0f;
            }
            break;
        case ToolMode::Ellipse: {
            if (!m_rectState.active) break;
            m_rectState.active = false;
            float l = std::min(m_rectState.anchor.x, cpt.x);
            float t = std::min(m_rectState.anchor.y, cpt.y);
            float r = std::max(m_rectState.anchor.x, cpt.x);
            float b = std::max(m_rectState.anchor.y, cpt.y);
            if (r - l < 2.f || b - t < 2.f) break;
            EllipseAnnotation ea{};
            ea.ellipse = D2D1::Ellipse(D2D1::Point2F((l + r) * 0.5f, (t + b) * 0.5f),
                                       (r - l) * 0.5f, (b - t) * 0.5f);
            ea.strokeColor = m_strokeColor;
            ea.strokeWidth = m_shapeStrokeWidth;
            ann = Annotation{ AnnotationType::Ellipse, ea };
            added = true;
            break;
        }
        case ToolMode::Line:
        case ToolMode::Arrow:
            added = ArrowTool_OnUp(m_arrowState, cpt, ann, m_strokeColor, m_shapeStrokeWidth);
            if (added && m_toolMode == ToolMode::Line)
                std::get<ArrowAnnotation>(ann.data).arrowHead = false;
            break;
        case ToolMode::Blur:
            added = BlurTool_OnUp(m_blurState, cpt, ann, m_blurRadius);
            if (added) std::get<BlurAnnotation>(ann.data).style = m_blurStyle;
            break;
        case ToolMode::FreeDraw:
        case ToolMode::Highlighter:
            m_freeDrawState.active = false;
            if (m_freeDrawState.points.size() >= 2) {
                FreeDrawAnnotation fd{};
                fd.points = m_freeDrawState.points;
                if (m_toolMode == ToolMode::Highlighter) {
                    fd.color = m_strokeColor;
                    fd.color.a = 0.38f;
                    fd.width = std::max(16.0f, m_strokeWidth * 3.5f);
                    fd.isHighlighter = true;
                } else {
                    fd.color = m_strokeColor;
                    fd.width = m_strokeWidth;
                    fd.isHighlighter = false;
                }
                ann = Annotation{ AnnotationType::FreeDraw, fd };
                added = true;
            }
            m_freeDrawState.points.clear();
            break;
        case ToolMode::Counter: {
            if (m_counterState.active) {
                m_counterState.active = false;
                CounterAnnotation ca{};
                ca.center = m_counterState.start;
                ca.number = m_counterState.step;
                ca.color = m_strokeColor;
                float dx = cpt.x - m_counterState.start.x;
                float dy = cpt.y - m_counterState.start.y;
                float dist = sqrtf(dx * dx + dy * dy);
                constexpr float kMinPointerDist = 24.f;
                if (dist >= kMinPointerDist) {
                    ca.pointer = cpt;
                } else {
                    ca.pointer = D2D1::Point2F(ca.center.x + 24.f, ca.center.y + 24.f);
                }
                ca.hasPointer = true;
                ann = Annotation{ AnnotationType::Counter, ca };
                added = true;
            }
            break;
        }
        default: break;
        }
        if (added) {
            PushUndoState();
            m_annotations.push_back(ann);
        }
        RenderFrame();
        return;
    }

    if (m_selection.phase != SelectionState::Phase::Dragging) return;

    m_selection.UpdateDrag(pt);
    if (m_selection.IsValid()) {
        if (m_captureMode == CaptureMode::TextExtract) {
            StartTextExtract();
            return;
        }
        m_selection.phase = SelectionState::Phase::Editing;
        m_toolMode = ToolMode::Select;
        PERF_MARK(t_selection_confirmed);
        m_isReselecting = false;
        StartAutoQrScan();
        RenderFrame();
    } else {
        if (m_isReselecting) {
            m_selection.anchor = m_reselectPrevAnchor;
            m_selection.current = m_reselectPrevCurrent;
            m_selection.phase = SelectionState::Phase::Editing;
            m_isReselecting = false;
        } else {
            m_selection.Reset();
        }
        RenderFrame();
    }
}

void OverlayWindow::OnKeyDown(WPARAM vk) noexcept {
    // Check if hotkey pressed — restart snip immediately
    const auto& settings = SettingsManager::Current();
    const bool alt   = ((GetKeyState(VK_MENU) & 0x8000) != 0) || (GetAsyncKeyState(VK_MENU) < 0);
    const bool ctrl  = ((GetKeyState(VK_CONTROL) & 0x8000) != 0) || (GetAsyncKeyState(VK_CONTROL) < 0);
    const bool shift = ((GetKeyState(VK_SHIFT) & 0x8000) != 0) || (GetAsyncKeyState(VK_SHIFT) < 0);
    const bool win   = ((GetKeyState(VK_LWIN) & 0x8000) != 0) || ((GetKeyState(VK_RWIN) & 0x8000) != 0) ||
                       (GetAsyncKeyState(VK_LWIN) < 0 || GetAsyncKeyState(VK_RWIN) < 0);

    bool matchesCustom = (vk == settings.vk &&
                          alt == settings.altModifier &&
                          ctrl == settings.ctrlModifier &&
                          shift == settings.shiftModifier &&
                          win == settings.winModifier);
    bool isDefaultAltS = ((vk == 'S' || vk == 's') && alt && !ctrl && !shift && !win);
    bool isPrintScreen = (vk == VK_SNAPSHOT);

    if (matchesCustom || isDefaultAltS || isPrintScreen) {
        if (OnRequestNewSnip) {
            OnRequestNewSnip();
            return;
        }
    }

    if (m_toolbarPopup == ToolbarPopup::CustomColorPicker && m_hexInputActive) {
        if (vk == VK_RETURN) {
            ApplyHexInput(m_hexInputText);
            m_hexInputActive = false;
            RenderFrame();
            return;
        }
        if (vk == VK_ESCAPE) {
            m_hexInputActive = false;
            RenderFrame();
            return;
        }
        if (vk == VK_BACK) {
            if (!m_hexInputText.empty()) {
                m_hexInputText.pop_back();
                ApplyHexInput(m_hexInputText);
                RenderFrame();
            }
            return;
        }
    }

    // Top-level Ctrl+Z / Ctrl+Y undo/redo
    if (ctrl && (vk == 'Z' || vk == 'z' || vk == 'Y' || vk == 'y')) {
        if (vk == 'Y' || vk == 'y' || shift) {
            Redo();
            return;
        }

        // Ctrl+Z Undo:
        // A. If actively typing in text tool, cancel active text edit
        if (m_textState.editing) {
            TextTool_Cancel(m_textState);
            KillTimer(m_hwnd, 1001);
            RenderFrame();
            return;
        }

        // B. If in OCR selection mode, unselect OCR or exit OCR
        if (m_ocrActive) {
            if (!m_selectedOcrCharIndices.empty()) {
                m_selectedOcrCharIndices.clear();
                m_ocrQuickActionVisible = false;
                RenderFrame();
                return;
            }
            m_ocrActive = false;
            m_selection.Reset();
            m_ocrResult = {};
            RenderFrame();
            return;
        }

        if (m_qrHovered) {
            m_qrHovered = false;
            RenderFrame();
            return;
        }

        // C. If there are annotations or undo states, undo annotation
        if (!m_undoStack.empty() || !m_annotations.empty()) {
            Undo();
            return;
        }

        // If selection exists, reset selection
        if (m_selection.phase != SelectionState::Phase::Idle || m_selection.IsValid()) {
            m_selection.Reset();
            m_lassoPoints.clear();
            m_lassoDrawing = false;
            m_toolbar.clear();
            m_toolMode = ToolMode::Select;
            RenderFrame();
            return;
        }

        return;
    }

    if (m_ocrActive) {
        if (vk == VK_RETURN) {
            std::wstring txt = GetSelectedOcrText();
            Clipboard::WriteText(txt.c_str(), m_hwnd);
            Hide();
            if (OnCancel) OnCancel();
            return;
        }
        if (vk == 'C' && ctrl) {
            std::wstring txt = GetSelectedOcrText();
            Clipboard::WriteText(txt.c_str(), m_hwnd);
            Hide();
            if (OnCancel) OnCancel();
            return;
        }
        if (vk == 'A' && ctrl) {
            m_selectedOcrCharIndices.clear();
            for (size_t i = 0; i < m_ocrResult.chars.size(); ++i) {
                m_selectedOcrCharIndices.push_back(static_cast<int>(i));
            }
            RenderFrame();
            return;
        }
        if (vk == VK_ESCAPE) {
            if (!m_selectedOcrCharIndices.empty()) {
                m_selectedOcrCharIndices.clear();
                m_ocrQuickActionVisible = false;
                RenderFrame();
            } else {
                m_ocrActive = false;
                m_selection.Reset();
                m_ocrResult = {};
                RenderFrame();
            }
            return;
        }
        return;
    }

    if (m_selection.phase == SelectionState::Phase::Editing) {
        if (m_textState.editing) {
            Annotation ann{};
            if (TextTool_OnKeyDown(m_textState, vk, shift, ctrl, m_hwnd, ann, g_dwrite.Factory())) {
                PushUndoState();
                m_annotations.push_back(ann);
                KillTimer(m_hwnd, 1001);
            }
            if (!m_textState.editing) {
                KillTimer(m_hwnd, 1001);
            }
            RenderFrame();
            return;
        } else {
            if ((vk == VK_DELETE || vk == VK_BACK) && m_selectedAnnotationIndex >= 0 &&
                m_selectedAnnotationIndex < static_cast<int>(m_annotations.size())) {
                PushUndoState();
                m_annotations.erase(m_annotations.begin() + m_selectedAnnotationIndex);
                m_selectedAnnotationIndex = -1;
                RenderFrame();
                return;
            }
            if (vk == VK_ESCAPE && m_selectedAnnotationIndex != -1) {
                m_selectedAnnotationIndex = -1;
                RenderFrame();
                return;
            }
        }
        if (vk == 'C' && ctrl) { CopyToClipboard(); return; }
        if (vk == 'S' && ctrl) { SaveToFile(); return; }
        if (vk == VK_RETURN)   { CopyToClipboard(); return; }
    }
    if (vk == VK_ESCAPE) {
        Hide();
        if (OnCancel) OnCancel();
    }
}

void OverlayWindow::OnChar(WPARAM ch) noexcept {
    if (m_toolbarPopup == ToolbarPopup::CustomColorPicker && m_hexInputActive) {
        wchar_t wc = static_cast<wchar_t>(ch);
        if (iswxdigit(wc) || wc == L'#') {
            if (m_hexInputText.size() < 7) {
                m_hexInputText += towupper(wc);
                ApplyHexInput(m_hexInputText);
                RenderFrame();
            }
        }
        return;
    }
    if (m_textState.editing) {
        Annotation ann{};
        if (TextTool_OnChar(m_textState, static_cast<wchar_t>(ch), ann)) {
            PushUndoState();
            m_annotations.push_back(ann);
            KillTimer(m_hwnd, 1001);
        }
        RenderFrame();
        return;
    }
    if (m_selection.phase != SelectionState::Phase::Editing) return;
}

void OverlayWindow::UpdateCursor(POINT localPt) noexcept {
    if (m_qrDetected && m_qrHovered && m_qrTooltipHoverBtn != 0) {
        SetCursor(LoadCursorW(nullptr, IDC_HAND));
        return;
    }

    if (m_ocrActive) {
        if (localPt.x >= m_ocrTopPillRect.left && localPt.x <= m_ocrTopPillRect.right &&
            localPt.y >= m_ocrTopPillRect.top && localPt.y <= m_ocrTopPillRect.bottom) {
            SetCursor(LoadCursorW(nullptr, IDC_HAND));
            return;
        }
        if (m_ocrQuickActionVisible &&
            localPt.x >= m_ocrQuickActionRect.left && localPt.x <= m_ocrQuickActionRect.right &&
            localPt.y >= m_ocrQuickActionRect.top && localPt.y <= m_ocrQuickActionRect.bottom) {
            SetCursor(LoadCursorW(nullptr, IDC_HAND));
            return;
        }
        int ocrResizeHandle = HitTestSelectionHandle({
            localPt.x + m_monitorRect.left,
            localPt.y + m_monitorRect.top
        });
        if (ocrResizeHandle != -1) {
            LPCWSTR cursorId = (ocrResizeHandle == 0 || ocrResizeHandle == 4) ? IDC_SIZENWSE
                : (ocrResizeHandle == 2 || ocrResizeHandle == 6) ? IDC_SIZENESW
                : (ocrResizeHandle == 1 || ocrResizeHandle == 5) ? IDC_SIZENS
                : IDC_SIZEWE;
            SetCursor(LoadCursorW(nullptr, cursorId));
            return;
        }
        RECT sel = m_selection.NormalizedRect();
        POINT pt{ localPt.x + m_monitorRect.left, localPt.y + m_monitorRect.top };
        if (pt.x >= sel.left && pt.x < sel.right && pt.y >= sel.top && pt.y < sel.bottom) {
            SetCursor(LoadCursorW(nullptr, IDC_IBEAM));
            return;
        }
        SetCursor(LoadCursorW(nullptr, IDC_ARROW));
        return;
    }

    if (PointInToolbarPopup(localPt)) {
        if (m_toolbarPopup == ToolbarPopup::CustomColorPicker) {
            const float relX = static_cast<float>(localPt.x) - m_popupBounds.left;
            const float relY = static_cast<float>(localPt.y) - m_popupBounds.top;
            const float width = m_popupBounds.right - m_popupBounds.left;
            if (relY >= 0.f && relY <= 34.f && relX < width - 34.f) {
                SetCursor(LoadCursorW(nullptr, IDC_SIZEALL));
                return;
            }
            if (relX >= 102.f && relX <= width - 14.f && relY >= 205.f && relY <= 231.f) {
                SetCursor(LoadCursorW(nullptr, IDC_IBEAM));
                return;
            }
            if (relX >= 14.f && relX <= 266.f && relY >= 36.f && relY <= 196.f) {
                SetCursor(LoadCursorW(nullptr, IDC_CROSS));
                return;
            }
        }
        if (HitTestToolbarPopup(localPt)) {
            SetCursor(LoadCursorW(nullptr, IDC_HAND));
        } else {
            SetCursor(LoadCursorW(nullptr, IDC_ARROW));
        }
        return;
    }

    if (HitTestToolbar(localPt) != -1) {
        SetCursor(LoadCursorW(nullptr, IDC_HAND));
        return;
    }

    if (PointInToolbar(localPt)) {
        SetCursor(LoadCursorW(nullptr, IDC_ARROW));
        return;
    }
    if (m_captureMode == CaptureMode::ColorPicker && !IsEditorOpen()) {
        SetCursor(LoadCursorW(nullptr, IDC_CROSS));
        return;
    }
    int resizeHandle = HitTestSelectionHandle({
        localPt.x + m_monitorRect.left,
        localPt.y + m_monitorRect.top
    });
    if (resizeHandle != -1) {
        LPCWSTR cursorId = (resizeHandle == 0 || resizeHandle == 4) ? IDC_SIZENWSE
            : (resizeHandle == 2 || resizeHandle == 6) ? IDC_SIZENESW
            : (resizeHandle == 1 || resizeHandle == 5) ? IDC_SIZENS
            : IDC_SIZEWE;
        SetCursor(LoadCursorW(nullptr, cursorId));
        return;
    }

    if (m_toolMode == ToolMode::Select && IsEditorOpen()) {
        D2D1_POINT_2F cpt{ static_cast<float>(localPt.x), static_cast<float>(localPt.y) };

        if (m_selectedAnnotationIndex >= 0 && m_selectedAnnotationIndex < static_cast<int>(m_annotations.size())) {
            int handle = HitTestSelectedAnnotationHandle(cpt);
            if (handle != -1) {
                if (m_annotations[m_selectedAnnotationIndex].type == AnnotationType::Arrow ||
                    m_annotations[m_selectedAnnotationIndex].type == AnnotationType::Counter) {
                    SetCursor(LoadCursorW(nullptr, IDC_SIZEALL));
                } else {
                    LPCWSTR cursorId = (handle == 0 || handle == 4) ? IDC_SIZENWSE
                        : (handle == 2 || handle == 6) ? IDC_SIZENESW
                        : (handle == 1 || handle == 5) ? IDC_SIZENS
                        : IDC_SIZEWE;
                    SetCursor(LoadCursorW(nullptr, cursorId));
                }
                return;
            }
            D2D1_RECT_F selB = GetAnnotationBounds(m_annotations[m_selectedAnnotationIndex]);
            selB.left -= 4.f; selB.top -= 4.f; selB.right += 4.f; selB.bottom += 4.f;
            if ((cpt.x >= selB.left && cpt.x <= selB.right &&
                 cpt.y >= selB.top && cpt.y <= selB.bottom) ||
                AnnotationHit(m_annotations[m_selectedAnnotationIndex], cpt)) {
                SetCursor(LoadCursorW(nullptr, IDC_SIZEALL));
                return;
            }
        }
        for (const auto& a : m_annotations) {
            bool hit = AnnotationHit(a, cpt);
            if (!hit && a.type == AnnotationType::FreeDraw) {
                D2D1_RECT_F b = GetAnnotationBounds(a);
                hit = (cpt.x >= b.left - 4.f && cpt.x <= b.right + 4.f &&
                       cpt.y >= b.top - 4.f && cpt.y <= b.bottom + 4.f);
            }
            if (hit) {
                SetCursor(LoadCursorW(nullptr, IDC_HAND));
                return;
            }
        }

        RECT sel = m_selection.NormalizedRect();
        POINT pt{ localPt.x + m_monitorRect.left, localPt.y + m_monitorRect.top };
        bool inside = (m_captureMode == CaptureMode::Freeform)
            ? IsPointInFreeform(cpt)
            : (pt.x >= sel.left && pt.x < sel.right && pt.y >= sel.top && pt.y < sel.bottom);

        if (!inside) {
            SetCursor(LoadCursorW(nullptr, IDC_CROSS));
            return;
        }

        SetCursor(LoadCursorW(nullptr, IDC_SIZEALL));
        return;
    }

    if (m_toolMode == ToolMode::Text && IsEditorOpen()) {
        SetCursor(LoadCursorW(nullptr, IDC_IBEAM));
        return;
    }
    SetCursor(LoadCursorW(nullptr, IDC_CROSS));
}

// ── Toolbar ───────────────────────────────────────────────────────────────────

void OverlayWindow::BuildToolbar() noexcept {
    m_toolbar.clear();
    m_separators.clear();
    const bool editorOpen = IsEditorOpen();
    const float btnWidth = editorOpen ? kEditorBtnWidth : kCaptureBtnWidth;
    const float toolbarH = editorOpen ? kEditorToolbarH : kCaptureToolbarH;

    struct ToolbarItem {
        ToolbarButton id;
        const WCHAR* tooltip;
        const WCHAR* name;
        int group;
        size_t defaultOrder;
        bool hasDropdown;
    };
    std::vector<ToolbarItem> items;
    auto add = [&](ToolbarButton id, const WCHAR* tooltip, const WCHAR* name, int group,
                   bool hasDropdown = false) {
        if (!HasSetting(m_hiddenTools, name))
            items.push_back({ id, tooltip, name, group, items.size(), hasDropdown });
    };

    if (editorOpen) {
        add(ToolbarButton::Move, L"Move selection", L"Move", 0);
        add(ToolbarButton::Pen, L"Pen", L"Pen", 0, true);
        add(ToolbarButton::Shapes, L"Shapes", L"Shapes", 0, true);
        add(ToolbarButton::Counter, L"Step Counter (1, 2, 3...)", L"Counter", 0);
        add(ToolbarButton::Blur, L"Blur", L"Blur", 0, true);
        add(ToolbarButton::Text, L"Text", L"Text", 0, true);
        add(ToolbarButton::AnnotationColor, L"Annotation color", L"Color", 0, true);
        add(ToolbarButton::Undo, L"Undo", L"Undo", 1);
        add(ToolbarButton::Clear, L"Clear", L"Clear", 1);
        add(ToolbarButton::Save, L"Save", L"Save", 1);
        add(ToolbarButton::Copy, L"Copy", L"Copy", 1);
        add(ToolbarButton::Close, L"Close", L"Close", 1);
    } else {
        add(ToolbarButton::Region, L"Rectangular snip", L"Region", 0);
        add(ToolbarButton::Freeform, L"Freeform snip", L"Freeform", 0);
        add(ToolbarButton::Fullscreen, L"Fullscreen snip", L"Fullscreen", 0);
        add(ToolbarButton::ColorPicker, L"Color picker", L"ColorPicker", 1);
        add(ToolbarButton::TextExtract, L"Text extractor", L"TextExtract", 1);
        add(ToolbarButton::Close, L"Close", L"Close", 2);
    }

    if (!m_toolbarOrder.empty()) {
        auto rank = [&](const ToolbarItem& item) {
            const auto found = std::find(m_toolbarOrder.begin(), m_toolbarOrder.end(),
                                         TrimSetting(item.name));
            return found == m_toolbarOrder.end()
            ? m_toolbarOrder.size() + item.defaultOrder
                : static_cast<size_t>(found - m_toolbarOrder.begin());
        };
        std::stable_sort(items.begin(), items.end(), [&](const ToolbarItem& left, const ToolbarItem& right) {
            return rank(left) < rank(right);
        });
    }

    float monW = static_cast<float>(m_monitorRect.right - m_monitorRect.left);
    float monH = static_cast<float>(m_monitorRect.bottom - m_monitorRect.top);
    float content = 0.f;
    for (size_t i = 0; i < items.size(); ++i) {
        content += btnWidth + (editorOpen && items[i].hasDropdown ? 16.f : 0.f);
        if (i + 1 < items.size())
            content += items[i].group == items[i + 1].group ? kBtnGap : kGroupGap;
    }

    float totalW = content + kBarPadX * 2.f;
    float tx = (monW - totalW) * 0.5f;
    if (tx < 8.f) tx = 8.f;
    float ty = m_toolbarAtTop ? kToolbarMargin : monH - kToolbarMargin - toolbarH;

    m_toolbarBounds = D2D1::RectF(tx, ty, tx + totalW, ty + toolbarH);

    float x = tx + kBarPadX;
    float y = ty + (toolbarH - kBtnSize) * 0.5f;
    for (size_t i = 0; i < items.size(); ++i) {
        if (i > 0 && items[i - 1].group != items[i].group) {
            x -= kGroupGap;
            x += kGroupGap * 0.5f;
            m_separators.push_back(x);
            x += kGroupGap * 0.5f;
        }
        m_toolbar.push_back({ items[i].id,
            D2D1::RectF(x, y, x + btnWidth + (editorOpen && items[i].hasDropdown ? 16.f : 0.f),
                        y + kBtnSize), items[i].tooltip, items[i].hasDropdown });
        if (i + 1 < items.size())
            x += btnWidth + (editorOpen && items[i].hasDropdown ? 16.f : 0.f) +
                 (items[i].group == items[i + 1].group ? kBtnGap : kGroupGap);
    }
}

bool OverlayWindow::PointInToolbar(POINT localPt) const noexcept {
    return localPt.x >= m_toolbarBounds.left && localPt.x <= m_toolbarBounds.right &&
           localPt.y >= m_toolbarBounds.top  && localPt.y <= m_toolbarBounds.bottom;
}

int OverlayWindow::HitTestToolbar(POINT localPt) const noexcept {
    D2D1_POINT_2F p{ static_cast<float>(localPt.x), static_cast<float>(localPt.y) };
    for (const auto& btn : m_toolbar) {
        const float closeInset = (btn.rect.right - btn.rect.left - 28.f) * 0.5f;
        const float left = btn.id == ToolbarButton::Close ? btn.rect.left + closeInset : btn.rect.left;
        const float right = btn.id == ToolbarButton::Close ? btn.rect.right - closeInset : btn.rect.right;
        if (p.x >= left && p.x <= right &&
            p.y >= m_toolbarBounds.top && p.y <= m_toolbarBounds.bottom)
            return static_cast<int>(btn.id);
    }
    return -1;
}

bool OverlayWindow::PointInToolbarPopup(POINT localPt) const noexcept {
    if (m_toolbarPopup == ToolbarPopup::None) return false;
    return localPt.x >= m_popupBounds.left && localPt.x <= m_popupBounds.right &&
           localPt.y >= m_popupBounds.top  && localPt.y <= m_popupBounds.bottom;
}

bool OverlayWindow::HitTestToolbarPopup(POINT localPt) const noexcept {
    if (!PointInToolbarPopup(localPt)) return false;

    const float relX = static_cast<float>(localPt.x) - m_popupBounds.left;
    const float relY = static_cast<float>(localPt.y) - m_popupBounds.top;
    const float width = m_popupBounds.right - m_popupBounds.left;

    switch (m_toolbarPopup) {
    case ToolbarPopup::Pen: {
        if (relY >= 8.f && relY <= 36.f) {
            return (relX >= 10.f && relX <= 70.f) ||
                   (relX >= 80.f && relX <= 140.f) ||
                   (relX >= 150.f && relX <= 210.f);
        }
        if (relY >= 70.f && relY <= 110.f) {
            return relX >= 14.f && relX <= width - 14.f;
        }
        return false;
    }
    case ToolbarPopup::Shapes: {
        return relY >= 6.f && relY <= 151.f && relX >= 5.f && relX <= width - 5.f;
    }
    case ToolbarPopup::Blur: {
        return relY >= 6.f && relY <= 98.f && relX >= 5.f && relX <= width - 5.f;
    }
    case ToolbarPopup::Text: {
        if (relY >= 27.f && relY <= 53.f) {
            return relX >= 10.f && relX <= width - 10.f;
        }
        float listTop = 57.f;
        float listBottom = listTop + 200.f;
        if (m_fontDropdownOpen && relY >= listTop && relY <= listBottom) {
            return relX >= 10.f && relX <= width - 10.f;
        }
        float controlsTop = m_fontDropdownOpen ? (listBottom + 10.f) : 61.f;
        float sliderRelY = relY - controlsTop;
        if (sliderRelY >= 15.f && sliderRelY <= 55.f) {
            return relX >= 14.f && relX <= width - 14.f;
        }
        return false;
    }
    case ToolbarPopup::Color: {
        if (relY >= 30.f && relY <= 90.f) {
            int col = static_cast<int>((relX - 10.f) / 32.f);
            int row = static_cast<int>((relY - 30.f) / 30.f);
            return (col >= 0 && col < 5 && row >= 0 && row < 2);
        }
        return false;
    }
    case ToolbarPopup::CustomColorPicker: {
        if (relX >= width - 34.f && relY >= 6.f && relY <= 32.f) return true;
        if (relY >= 0.f && relY <= 34.f) return true;
        if ((relX >= 14.f && relX <= 266.f && relY >= 36.f && relY <= 196.f) ||
            (relX >= 274.f && relX <= 296.f && relY >= 36.f && relY <= 196.f)) {
            return true;
        }
        if (relX >= 102.f && relX <= width - 14.f && relY >= 205.f && relY <= 231.f) return true;
        if (relX >= 14.f && relX <= 296.f && relY >= 292.f && relY <= 368.f) return true;
        if (relY >= 374.f && relY <= 402.f && relX >= 14.f && relX <= width - 14.f) return true;
        return false;
    }
    default:
        return false;
    }
}

static constexpr D2D1_COLOR_F kQuickColors[50] = {
    // Row 1: Reds / Oranges / Yellows
    { 1.00f, 0.00f, 0.00f, 1.0f }, { 1.00f, 0.23f, 0.19f, 1.0f }, { 1.00f, 0.18f, 0.33f, 1.0f }, { 1.00f, 0.42f, 0.42f, 1.0f },
    { 1.00f, 0.58f, 0.00f, 1.0f }, { 1.00f, 0.80f, 0.00f, 1.0f }, { 1.00f, 0.88f, 0.40f, 1.0f }, { 1.00f, 0.44f, 0.00f, 1.0f },
    { 0.90f, 0.32f, 0.00f, 1.0f }, { 0.75f, 0.21f, 0.05f, 1.0f },
    // Row 2: Greens
    { 0.20f, 0.78f, 0.35f, 1.0f }, { 0.19f, 0.82f, 0.35f, 1.0f }, { 0.00f, 0.78f, 0.33f, 1.0f }, { 0.41f, 0.94f, 0.68f, 1.0f },
    { 0.11f, 0.37f, 0.13f, 1.0f }, { 0.18f, 0.49f, 0.20f, 1.0f }, { 0.26f, 0.63f, 0.28f, 1.0f }, { 0.40f, 0.73f, 0.42f, 1.0f },
    { 0.65f, 0.84f, 0.65f, 1.0f }, { 0.80f, 1.00f, 0.56f, 1.0f },
    // Row 3: Blues / Cyans
    { 0.00f, 0.48f, 1.00f, 1.0f }, { 0.04f, 0.52f, 1.00f, 1.0f }, { 0.20f, 0.68f, 0.90f, 1.0f }, { 0.39f, 0.82f, 1.00f, 1.0f },
    { 0.00f, 0.69f, 1.00f, 1.0f }, { 0.01f, 0.53f, 0.82f, 1.0f }, { 0.00f, 0.34f, 0.61f, 1.0f }, { 0.00f, 0.90f, 1.00f, 1.0f },
    { 0.00f, 0.38f, 0.39f, 1.0f }, { 0.50f, 0.87f, 0.92f, 1.0f },
    // Row 4: Purples / Pinks
    { 0.69f, 0.32f, 0.87f, 1.0f }, { 0.75f, 0.35f, 0.95f, 1.0f }, { 0.38f, 0.00f, 0.92f, 1.0f }, { 0.61f, 0.15f, 0.69f, 1.0f },
    { 0.88f, 0.25f, 0.98f, 1.0f }, { 0.92f, 0.50f, 0.99f, 1.0f }, { 1.00f, 0.50f, 0.67f, 1.0f }, { 0.96f, 0.56f, 0.69f, 1.0f },
    { 0.68f, 0.08f, 0.34f, 1.0f }, { 0.53f, 0.05f, 0.31f, 1.0f },
    // Row 5: Grays / Black / White
    { 1.00f, 1.00f, 1.00f, 1.0f }, { 0.96f, 0.96f, 0.96f, 1.0f }, { 0.74f, 0.74f, 0.74f, 1.0f }, { 0.62f, 0.62f, 0.62f, 1.0f },
    { 0.46f, 0.46f, 0.46f, 1.0f }, { 0.38f, 0.38f, 0.38f, 1.0f }, { 0.26f, 0.26f, 0.26f, 1.0f }, { 0.13f, 0.13f, 0.13f, 1.0f },
    { 0.07f, 0.07f, 0.07f, 1.0f }, { 0.00f, 0.00f, 0.00f, 1.0f }
};

bool OverlayWindow::HandleToolbarPopupClick(POINT localPt) noexcept {
    if (m_toolbarPopup == ToolbarPopup::None) return false;
    if (localPt.x < m_popupBounds.left || localPt.x > m_popupBounds.right ||
        localPt.y < m_popupBounds.top || localPt.y > m_popupBounds.bottom) {
        return false;
    }

    const float relativeY = static_cast<float>(localPt.y) - m_popupBounds.top;
    switch (m_toolbarPopup) {
    case ToolbarPopup::Pen: {
        if (relativeY >= 8.f && relativeY <= 36.f) {
            float relativeX = static_cast<float>(localPt.x) - m_popupBounds.left;
            if (relativeX >= 10.f && relativeX <= 70.f) m_toolMode = ToolMode::FreeDraw;
            else if (relativeX >= 80.f && relativeX <= 140.f) m_toolMode = ToolMode::Highlighter;
            else if (relativeX >= 150.f && relativeX <= 210.f) m_toolMode = ToolMode::Eraser;
            m_toolbarPopup = ToolbarPopup::None;
        } else if (relativeY >= 70.f && relativeY <= 110.f) {
            m_penSizeDragging = true;
            float sliderLeft = m_popupBounds.left + 18.f;
            float sliderWidth = m_popupBounds.right - m_popupBounds.left - 36.f;
            float value = (static_cast<float>(localPt.x) - sliderLeft) / sliderWidth;
            m_strokeWidth = 1.f + std::max(0.f, std::min(value, 1.f)) * 23.f;
        }
        return true;
    }
    case ToolbarPopup::Shapes: {
        static constexpr ToolMode modes[] = {
            ToolMode::Arrow, ToolMode::Rect, ToolMode::Line,
            ToolMode::Ellipse, ToolMode::FilledRect
        };
        if (relativeY < 35.f)      m_toolMode = ToolMode::Arrow;
        else if (relativeY < 64.f) m_toolMode = ToolMode::Rect;
        else if (relativeY < 93.f) m_toolMode = ToolMode::Line;
        else if (relativeY < 122.f) m_toolMode = ToolMode::Ellipse;
        else                       m_toolMode = ToolMode::FilledRect;
        m_toolbarPopup = ToolbarPopup::None;
        return true;
    }
    case ToolbarPopup::Blur: {
        if (relativeY < 36.f)      m_blurStyle = BlurStyle::Gaussian;
        else if (relativeY < 67.f) m_blurStyle = BlurStyle::Mosaic;
        else                       m_blurStyle = BlurStyle::Invert;
        m_toolMode = ToolMode::Blur;
        m_toolbarPopup = ToolbarPopup::None;
        return true;
    }
    case ToolbarPopup::Text: {
        float listTop = m_popupBounds.top + 57.f;
        float listBottom = listTop + 200.f;
        float itemH = 25.f;
        float controlsTop = m_fontDropdownOpen ? (listBottom + 10.f) : (m_popupBounds.top + 64.f);

        if (relativeY >= 27.f && relativeY <= 53.f) {
            PopulateSystemFonts();
            m_fontDropdownOpen = !m_fontDropdownOpen;
            if (m_fontDropdownOpen) {
                auto it = std::find(m_systemFonts.begin(), m_systemFonts.end(), m_fontFamily);
                if (it != m_systemFonts.end()) {
                    int idx = static_cast<int>(std::distance(m_systemFonts.begin(), it));
                    int maxScroll = std::max(0, static_cast<int>(m_systemFonts.size()) - 8);
                    m_fontScrollIndex = std::clamp(idx - 3, 0, maxScroll);
                }
            }
            return true;
        }

        if (m_fontDropdownOpen && localPt.y >= listTop && localPt.y <= listBottom) {
            float listRight = m_popupBounds.right - 10.f;
            if (localPt.x >= listRight - 12.f) {
                int maxScroll = std::max(0, static_cast<int>(m_systemFonts.size()) - 8);
                float ratio = (static_cast<float>(localPt.y) - listTop) / (listBottom - listTop);
                m_fontScrollIndex = std::clamp(static_cast<int>(ratio * maxScroll), 0, maxScroll);
                return true;
            }
            int clickedRow = static_cast<int>((localPt.y - listTop) / itemH);
            int selectedIdx = m_fontScrollIndex + clickedRow;
            if (selectedIdx >= 0 && selectedIdx < static_cast<int>(m_systemFonts.size())) {
                m_fontFamily = m_systemFonts[selectedIdx];
                m_fontDropdownOpen = false;

                if (m_selectedAnnotationIndex >= 0 && m_selectedAnnotationIndex < static_cast<int>(m_annotations.size()) &&
                    m_annotations[m_selectedAnnotationIndex].type == AnnotationType::Text) {
                    PushUndoState();
                    std::get<TextAnnotation>(m_annotations[m_selectedAnnotationIndex].data).fontFamily = m_fontFamily;
                }
                if (m_textState.editing) {
                    m_textState.fontFamily = m_fontFamily;
                }
            }
            return true;
        }

        float sliderRelativeY = static_cast<float>(localPt.y) - controlsTop;
        if (sliderRelativeY >= 10.f && sliderRelativeY <= 42.f) {
            m_fontSizeDragging = true;
            float sliderLeft = m_popupBounds.left + 18.f;
            float sliderWidth = m_popupBounds.right - m_popupBounds.left - 36.f;
            float value = (static_cast<float>(localPt.x) - sliderLeft) / sliderWidth;
            m_fontSize = 10.f + std::max(0.f, std::min(value, 1.f)) * 26.f;
            if (m_selectedAnnotationIndex >= 0 && m_selectedAnnotationIndex < static_cast<int>(m_annotations.size()) &&
                m_annotations[m_selectedAnnotationIndex].type == AnnotationType::Text) {
                std::get<TextAnnotation>(m_annotations[m_selectedAnnotationIndex].data).fontSize = m_fontSize;
            }
            if (m_textState.editing) {
                m_textState.fontSize = m_fontSize;
            }
            return true;
        }
        return true;
    }
    case ToolbarPopup::Color: {
        static constexpr D2D1_COLOR_F colors[] = {
            { 0.96f, 0.20f, 0.18f, 1.f }, { 1.f, 0.58f, 0.10f, 1.f },
            { 1.f, 0.82f, 0.12f, 1.f }, { 0.20f, 0.78f, 0.35f, 1.f },
            { 0.20f, 0.68f, 0.90f, 1.f }, { 0.00f, 0.48f, 1.00f, 1.f },
            { 0.69f, 0.32f, 0.87f, 1.f }, { 1.00f, 0.18f, 0.33f, 1.f },
            { 1.f, 1.f, 1.f, 1.f }
        };
        int col = static_cast<int>((localPt.x - m_popupBounds.left - 10.f) / 32.f);
        int row = static_cast<int>((relativeY - 30.f) / 30.f);
        int index = row * 5 + col;
        if (col >= 0 && col < 5 && row >= 0 && row < 2) {
            if (index == 9) {
                // Open Custom Color Picker!
                m_customPickerColor = ToD2D(m_strokeColor);
                m_customPickerBackupColor = m_customPickerColor;
                RgbToHsv(m_customPickerColor, m_customPickerHue, m_customPickerSat, m_customPickerVal);
                m_toolbarPopup = ToolbarPopup::CustomColorPicker;
                m_hexInputActive = false;
                WCHAR initHex[16];
                int rB = static_cast<int>(std::round(m_customPickerColor.r * 255.f));
                int gB = static_cast<int>(std::round(m_customPickerColor.g * 255.f));
                int bB = static_cast<int>(std::round(m_customPickerColor.b * 255.f));
                swprintf_s(initHex, L"#%02X%02X%02X", rB, gB, bB);
                m_hexInputText = initHex;
                return true;
            }
            if (index >= 0 && index < 9) {
                const auto color = colors[index];
                m_strokeColor = { color.r, color.g, color.b, color.a };
                if (m_strokeBrush) m_strokeBrush->SetColor(color);
                if (m_textState.editing) m_textState.color = m_strokeColor;

                if (m_selectedAnnotationIndex >= 0 && m_selectedAnnotationIndex < static_cast<int>(m_annotations.size())) {
                    PushUndoState();
                    auto& ann = m_annotations[m_selectedAnnotationIndex];
                    switch (ann.type) {
                    case AnnotationType::Rect: std::get<RectAnnotation>(ann.data).strokeColor = m_strokeColor; break;
                    case AnnotationType::Ellipse: std::get<EllipseAnnotation>(ann.data).strokeColor = m_strokeColor; break;
                    case AnnotationType::Arrow: std::get<ArrowAnnotation>(ann.data).color = m_strokeColor; break;
                    case AnnotationType::Text: std::get<TextAnnotation>(ann.data).color = m_strokeColor; break;
                    case AnnotationType::FreeDraw: std::get<FreeDrawAnnotation>(ann.data).color = m_strokeColor; break;
                    case AnnotationType::Counter: std::get<CounterAnnotation>(ann.data).color = m_strokeColor; break;
                    default: break;
                    }
                }
            }
        }
        m_toolbarPopup = ToolbarPopup::None;
        return true;
    }
    case ToolbarPopup::CustomColorPicker: {
        float relX = static_cast<float>(localPt.x) - m_popupBounds.left;
        float width = m_popupBounds.right - m_popupBounds.left;

        // 1. Close button (top right)
        if (relX >= width - 34.f && relativeY >= 6.f && relativeY <= 32.f) {
            m_hexInputActive = false;
            m_customPickerColor = m_customPickerBackupColor;
            m_strokeColor = { m_customPickerColor.r, m_customPickerColor.g, m_customPickerColor.b, 1.0f };
            if (m_strokeBrush) m_strokeBrush->SetColor(m_customPickerColor);
            m_toolbarPopup = ToolbarPopup::None;
            return true;
        }

        // 1b. Header drag area (top 0..34)
        if (relativeY >= 0.f && relativeY <= 34.f && relX < width - 34.f) {
            m_draggingColorPicker = true;
            m_colorPickerDragStartPt = { localPt.x + m_monitorRect.left, localPt.y + m_monitorRect.top };
            return true;
        }

        // 2. 2D Spectrum box (left 14..266, top 36..196)
        if (relX >= 14.f && relX <= 266.f && relativeY >= 36.f && relativeY <= 196.f) {
            if (m_hexInputActive) { ApplyHexInput(m_hexInputText); m_hexInputActive = false; }
            m_colorSpectrumDragging = true;
            m_customPickerSat = std::clamp((relX - 14.f) / 252.f, 0.0f, 1.0f);
            m_customPickerVal = std::clamp(1.0f - (relativeY - 36.f) / 160.f, 0.0f, 1.0f);
            m_customPickerColor = HsvToRgb(m_customPickerHue, m_customPickerSat, m_customPickerVal);
            m_strokeColor = { m_customPickerColor.r, m_customPickerColor.g, m_customPickerColor.b, 1.0f };
            if (m_strokeBrush) m_strokeBrush->SetColor(m_customPickerColor);
            return true;
        }

        // 3. Hue strip (left 274..296, top 36..196)
        if (relX >= 274.f && relX <= 296.f && relativeY >= 36.f && relativeY <= 196.f) {
            if (m_hexInputActive) { ApplyHexInput(m_hexInputText); m_hexInputActive = false; }
            m_colorHueDragging = true;
            m_customPickerHue = std::clamp((relativeY - 36.f) / 160.f, 0.0f, 1.0f) * 360.0f;
            if (m_customPickerHue >= 360.0f) m_customPickerHue = 0.0f;
            m_customPickerColor = HsvToRgb(m_customPickerHue, m_customPickerSat, m_customPickerVal);
            m_strokeColor = { m_customPickerColor.r, m_customPickerColor.g, m_customPickerColor.b, 1.0f };
            if (m_strokeBrush) m_strokeBrush->SetColor(m_customPickerColor);
            return true;
        }

        // 3b. HEX box click (left 102..width - 14.f, top 205..231)
        if (relX >= 102.f && relX <= width - 14.f && relativeY >= 205.f && relativeY <= 231.f) {
            m_hexInputActive = true;
            WCHAR hexBuf[16];
            int rByte = static_cast<int>(std::round(m_customPickerColor.r * 255.f));
            int gByte = static_cast<int>(std::round(m_customPickerColor.g * 255.f));
            int bByte = static_cast<int>(std::round(m_customPickerColor.b * 255.f));
            swprintf_s(hexBuf, L"#%02X%02X%02X", rByte, gByte, bByte);
            m_hexInputText = hexBuf;
            SetFocus(m_hwnd);
            SetTimer(m_hwnd, 1001, 500, nullptr);
            RenderFrame();
            return true;
        }

        // 4. Quick Colors (50 swatches: 10 cols x 5 rows, left 14..296, top 292..368)
        if (relX >= 14.f && relX <= 296.f && relativeY >= 292.f && relativeY <= 368.f) {
            if (m_hexInputActive) { ApplyHexInput(m_hexInputText); m_hexInputActive = false; }
            float cellW = 282.f / 10.f;
            float cellH = 76.f / 5.f;
            int c = static_cast<int>((relX - 14.f) / cellW);
            int r = static_cast<int>((relativeY - 292.f) / cellH);
            if (c >= 0 && c < 10 && r >= 0 && r < 5) {
                int qIdx = r * 10 + c;
                m_customPickerColor = kQuickColors[qIdx];
                RgbToHsv(m_customPickerColor, m_customPickerHue, m_customPickerSat, m_customPickerVal);
                m_strokeColor = { m_customPickerColor.r, m_customPickerColor.g, m_customPickerColor.b, 1.0f };
                if (m_strokeBrush) m_strokeBrush->SetColor(m_customPickerColor);
                return true;
            }
        }

        // 5. Cancel button (left 14..145, top 374..402)
        if (relX >= 14.f && relX <= 145.f && relativeY >= 374.f && relativeY <= 402.f) {
            m_hexInputActive = false;
            m_customPickerColor = m_customPickerBackupColor;
            m_strokeColor = { m_customPickerColor.r, m_customPickerColor.g, m_customPickerColor.b, 1.0f };
            if (m_strokeBrush) m_strokeBrush->SetColor(m_customPickerColor);
            m_toolbarPopup = ToolbarPopup::None;
            return true;
        }

        // 6. Apply Color button (left 155..width - 14.f, top 374..402)
        if (relX >= 155.f && relX <= width - 14.f && relativeY >= 374.f && relativeY <= 402.f) {
            if (m_hexInputActive) { ApplyHexInput(m_hexInputText); m_hexInputActive = false; }
            m_strokeColor = { m_customPickerColor.r, m_customPickerColor.g, m_customPickerColor.b, 1.0f };
            if (m_strokeBrush) m_strokeBrush->SetColor(m_customPickerColor);

            if (m_selectedAnnotationIndex >= 0 && m_selectedAnnotationIndex < static_cast<int>(m_annotations.size())) {
                PushUndoState();
                auto& ann = m_annotations[m_selectedAnnotationIndex];
                switch (ann.type) {
                case AnnotationType::Rect: std::get<RectAnnotation>(ann.data).strokeColor = m_strokeColor; break;
                case AnnotationType::Ellipse: std::get<EllipseAnnotation>(ann.data).strokeColor = m_strokeColor; break;
                case AnnotationType::Arrow: std::get<ArrowAnnotation>(ann.data).color = m_strokeColor; break;
                case AnnotationType::Text: std::get<TextAnnotation>(ann.data).color = m_strokeColor; break;
                case AnnotationType::FreeDraw: std::get<FreeDrawAnnotation>(ann.data).color = m_strokeColor; break;
                case AnnotationType::Counter: std::get<CounterAnnotation>(ann.data).color = m_strokeColor; break;
                default: break;
                }
            }

            m_toolbarPopup = ToolbarPopup::None;
            return true;
        }

        // Clicked elsewhere inside color picker: commit hex input if active
        if (m_hexInputActive) {
            ApplyHexInput(m_hexInputText);
            m_hexInputActive = false;
            return true;
        }
        return true;
    }
    default:
        return false;
    }
}

void OverlayWindow::OnToolbarClick(int btnId) noexcept {
    auto btn = static_cast<ToolbarButton>(btnId);
    if (m_textState.editing) {
        Annotation ann{};
        if (TextTool_Commit(m_textState, ann)) {
            PushUndoState();
            m_annotations.push_back(ann);
        }
        if (m_hwnd) KillTimer(m_hwnd, 1001);
    }
    if (btn != ToolbarButton::Move) {
        m_selectedAnnotationIndex = -1;
    }
    switch (btn) {
    case ToolbarButton::Move:
        m_toolMode = ToolMode::Select;
        break;
    case ToolbarButton::Shapes:
        if (m_toolMode != ToolMode::Arrow && m_toolMode != ToolMode::Rect &&
            m_toolMode != ToolMode::Line && m_toolMode != ToolMode::Ellipse &&
            m_toolMode != ToolMode::FilledRect)
            m_toolMode = ToolMode::Rect;
        break;
    case ToolbarButton::Blur:
        m_toolMode = ToolMode::Blur;
        break;
    case ToolbarButton::Region:
        m_captureMode = CaptureMode::Region;
        m_toolMode = ToolMode::Select;
        m_selection.Reset();
        m_lassoPoints.clear();
        break;
    case ToolbarButton::Fullscreen:
        m_captureMode = CaptureMode::Fullscreen;
        ApplyFullscreenSelection();
        break;
    case ToolbarButton::Freeform:
        m_captureMode = CaptureMode::Freeform;
        m_toolMode = ToolMode::Select;
        m_selection.Reset();
        m_lassoPoints.clear();
        break;
    case ToolbarButton::ColorPicker:
        m_captureMode = CaptureMode::ColorPicker;
        m_toolMode = ToolMode::Select;
        m_selection.Reset();
        m_lassoPoints.clear();
        break;
    case ToolbarButton::TextExtract:
        m_captureMode = CaptureMode::TextExtract;
        m_toolMode = ToolMode::Select;
        m_selection.Reset();
        m_lassoPoints.clear();
        break;
    case ToolbarButton::Pen:         m_toolMode = ToolMode::FreeDraw;    break;
    case ToolbarButton::Highlighter: m_toolMode = ToolMode::Highlighter; break;
    case ToolbarButton::Eraser:      m_toolMode = ToolMode::Eraser;      break;
    case ToolbarButton::Rect:        m_toolMode = ToolMode::Rect;        break;
    case ToolbarButton::Ellipse:     m_toolMode = ToolMode::Ellipse;     break;
    case ToolbarButton::Arrow:       m_toolMode = ToolMode::Arrow;       break;
    case ToolbarButton::Text:        m_toolMode = ToolMode::Text;        break;
    case ToolbarButton::Counter:     m_toolMode = ToolMode::Counter;     break;
    case ToolbarButton::Undo:  Undo();  return;
    case ToolbarButton::Redo:  Redo();  return;
    case ToolbarButton::Clear: ClearAnnotations(); return;
    case ToolbarButton::Copy:  CopyToClipboard(); return;
    case ToolbarButton::Save:  SaveToFile(); return;
    case ToolbarButton::Close:
        Hide();
        if (OnCancel) OnCancel();
        return;
    }
    RenderFrame();
}

static bool ArePointsEqual(const D2D1_POINT_2F& p1, const D2D1_POINT_2F& p2) noexcept {
    return p1.x == p2.x && p1.y == p2.y;
}

static bool AreRectsEqual(const D2D1_RECT_F& r1, const D2D1_RECT_F& r2) noexcept {
    return r1.left == r2.left && r1.top == r2.top && r1.right == r2.right && r1.bottom == r2.bottom;
}

static bool AreColorsEqual(const ColorF& c1, const ColorF& c2) noexcept {
    return c1.r == c2.r && c1.g == c2.g && c1.b == c2.b && c1.a == c2.a;
}

static bool AreAnnotationsEqual(const Annotation& a, const Annotation& b) noexcept {
    if (a.type != b.type || a.data.index() != b.data.index()) return false;
    switch (a.type) {
    case AnnotationType::Rect: {
        const auto& ra = std::get<RectAnnotation>(a.data);
        const auto& rb = std::get<RectAnnotation>(b.data);
        return AreRectsEqual(ra.rect, rb.rect) && AreColorsEqual(ra.strokeColor, rb.strokeColor) &&
               AreColorsEqual(ra.fillColor, rb.fillColor) && ra.strokeWidth == rb.strokeWidth;
    }
    case AnnotationType::Ellipse: {
        const auto& ea = std::get<EllipseAnnotation>(a.data);
        const auto& eb = std::get<EllipseAnnotation>(b.data);
        return ArePointsEqual(ea.ellipse.point, eb.ellipse.point) &&
               ea.ellipse.radiusX == eb.ellipse.radiusX && ea.ellipse.radiusY == eb.ellipse.radiusY &&
               AreColorsEqual(ea.strokeColor, eb.strokeColor) && AreColorsEqual(ea.fillColor, eb.fillColor) &&
               ea.strokeWidth == eb.strokeWidth;
    }
    case AnnotationType::Arrow: {
        const auto& aa = std::get<ArrowAnnotation>(a.data);
        const auto& ab = std::get<ArrowAnnotation>(b.data);
        return ArePointsEqual(aa.start, ab.start) && ArePointsEqual(aa.end, ab.end) &&
               AreColorsEqual(aa.color, ab.color) && aa.width == ab.width && aa.arrowHead == ab.arrowHead;
    }
    case AnnotationType::Text: {
        const auto& ta = std::get<TextAnnotation>(a.data);
        const auto& tb = std::get<TextAnnotation>(b.data);
        return ArePointsEqual(ta.origin, tb.origin) && ta.text == tb.text &&
               AreColorsEqual(ta.color, tb.color) && ta.fontSize == tb.fontSize &&
               ta.fontFamily == tb.fontFamily && ta.scaleX == tb.scaleX && ta.scaleY == tb.scaleY;
    }
    case AnnotationType::Blur: {
        const auto& ba = std::get<BlurAnnotation>(a.data);
        const auto& bb = std::get<BlurAnnotation>(b.data);
        return AreRectsEqual(ba.rect, bb.rect) && ba.radius == bb.radius && ba.style == bb.style;
    }
    case AnnotationType::FreeDraw: {
        const auto& fa = std::get<FreeDrawAnnotation>(a.data);
        const auto& fb = std::get<FreeDrawAnnotation>(b.data);
        if (fa.points.size() != fb.points.size()) return false;
        if (!AreColorsEqual(fa.color, fb.color) || fa.width != fb.width || fa.isHighlighter != fb.isHighlighter) return false;
        for (size_t i = 0; i < fa.points.size(); ++i) {
            if (!ArePointsEqual(fa.points[i], fb.points[i])) return false;
        }
        return true;
    }
    case AnnotationType::Counter: {
        const auto& ca = std::get<CounterAnnotation>(a.data);
        const auto& cb = std::get<CounterAnnotation>(b.data);
        return ArePointsEqual(ca.center, cb.center) && ArePointsEqual(ca.pointer, cb.pointer) &&
               ca.hasPointer == cb.hasPointer && ca.number == cb.number && AreColorsEqual(ca.color, cb.color);
    }
    }
    return false;
}

static bool AreAnnotationListsEqual(const std::vector<Annotation>& a, const std::vector<Annotation>& b) noexcept {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        if (!AreAnnotationsEqual(a[i], b[i])) return false;
    }
    return true;
}

void OverlayWindow::PushUndoState() noexcept {
    // Prevent duplicate undo states
    if (!m_undoStack.empty() && AreAnnotationListsEqual(m_undoStack.back(), m_annotations)) {
        return;
    }
    m_undoStack.push_back(m_annotations);
    if (m_undoStack.size() > kMaxUndo)
        m_undoStack.erase(m_undoStack.begin());
    m_redoStack.clear();
}

void OverlayWindow::Undo() noexcept {
    m_selectedAnnotationIndex = -1;

    // Discard any undo states that are identical to the current annotations
    while (!m_undoStack.empty() && AreAnnotationListsEqual(m_undoStack.back(), m_annotations)) {
        m_undoStack.pop_back();
    }

    if (!m_undoStack.empty()) {
        m_redoStack.push_back(m_annotations);
        m_annotations = m_undoStack.back();
        m_undoStack.pop_back();
        RenderFrame();
        return;
    }
    if (!m_annotations.empty()) {
        m_redoStack.push_back(m_annotations);
        m_annotations.pop_back();
        RenderFrame();
        return;
    }
}

void OverlayWindow::Redo() noexcept {
    while (!m_redoStack.empty() && AreAnnotationListsEqual(m_redoStack.back(), m_annotations)) {
        m_redoStack.pop_back();
    }
    if (m_redoStack.empty()) return;
    m_selectedAnnotationIndex = -1;
    m_undoStack.push_back(m_annotations);
    m_annotations = m_redoStack.back();
    m_redoStack.pop_back();
    RenderFrame();
}

void OverlayWindow::ClearAnnotations() noexcept {
    if (m_annotations.empty()) return;
    m_selectedAnnotationIndex = -1;
    PushUndoState();
    m_annotations.clear();
    RenderFrame();
}

bool OverlayWindow::RenderSnipImage(std::vector<BYTE>& outBgra, UINT& outW, UINT& outH) noexcept {
    if (!m_selection.IsValid() || !m_desktopBitmap) return false;

    RECT sel = m_selection.NormalizedRect();
    UINT width = static_cast<UINT>(sel.right - sel.left);
    UINT height = static_cast<UINT>(sel.bottom - sel.top);
    if (width == 0 || height == 0) return false;

    UINT fullW = static_cast<UINT>(m_monitorRect.right - m_monitorRect.left);
    UINT fullH = static_cast<UINT>(m_monitorRect.bottom - m_monitorRect.top);
    if (fullW == 0 || fullH == 0) return false;

    float shiftX = static_cast<float>(sel.left - m_monitorRect.left);
    float shiftY = static_cast<float>(sel.top - m_monitorRect.top);

    UINT left   = static_cast<UINT>(std::clamp(static_cast<LONG>(shiftX), 0L, static_cast<LONG>(fullW)));
    UINT top    = static_cast<UINT>(std::clamp(static_cast<LONG>(shiftY), 0L, static_cast<LONG>(fullH)));
    UINT right  = static_cast<UINT>(std::clamp(static_cast<LONG>(shiftX + width), static_cast<LONG>(left), static_cast<LONG>(fullW)));
    UINT bottom = static_cast<UINT>(std::clamp(static_cast<LONG>(shiftY + height), static_cast<LONG>(top), static_cast<LONG>(fullH)));
    if (right <= left || bottom <= top) return false;

    UINT cropW = right - left;
    UINT cropH = bottom - top;

    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = fullW;
    desc.Height = fullH;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;

    ComPtr<ID3D11Texture2D> renderTex;
    if (FAILED(g_d2d.D3DDevice()->CreateTexture2D(&desc, nullptr, &renderTex))) return false;

    ComPtr<IDXGISurface> dxgiSurface;
    if (FAILED(renderTex.As(&dxgiSurface))) return false;

    D2D1_BITMAP_PROPERTIES1 bp = D2D1::BitmapProperties1(
        D2D1_BITMAP_OPTIONS_TARGET,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED)
    );

    ComPtr<ID2D1Bitmap1> targetBitmap;
    if (FAILED(g_d2d.D2DDevCtx()->CreateBitmapFromDxgiSurface(dxgiSurface.Get(), &bp, &targetBitmap))) return false;

    auto* dc = g_d2d.D2DDevCtx();
    ComPtr<ID2D1Image> oldTarget;
    dc->GetTarget(&oldTarget);

    dc->SetTarget(targetBitmap.Get());
    dc->BeginDraw();
    dc->Clear(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.0f));

    ComPtr<ID2D1PathGeometry> freeformClip;
    ComPtr<ID2D1Layer> freeformLayer;
    if (m_captureMode == CaptureMode::Freeform && m_lassoPoints.size() >= 3) {
        ComPtr<ID2D1Factory> factory;
        dc->GetFactory(&factory);
        if (SUCCEEDED(factory->CreatePathGeometry(&freeformClip))) {
            ComPtr<ID2D1GeometrySink> sink;
            if (SUCCEEDED(freeformClip->Open(&sink))) {
                sink->BeginFigure(m_lassoPoints[0], D2D1_FIGURE_BEGIN_FILLED);
                for (size_t i = 1; i < m_lassoPoints.size(); ++i) {
                    sink->AddLine(m_lassoPoints[i]);
                }
                sink->EndFigure(D2D1_FIGURE_END_CLOSED);
                sink->Close();
                if (SUCCEEDED(dc->CreateLayer(nullptr, &freeformLayer))) {
                    dc->PushLayer(D2D1::LayerParameters(
                        D2D1::InfiniteRect(),
                        freeformClip.Get(),
                        D2D1_ANTIALIAS_MODE_PER_PRIMITIVE
                    ), freeformLayer.Get());
                }
            }
        }
    }

    D2D1_RECT_F fullRect = D2D1::RectF(0.0f, 0.0f, static_cast<float>(fullW), static_cast<float>(fullH));
    dc->DrawBitmap(m_desktopBitmap.Get(), fullRect, 1.0f,
                   D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR);

    m_activeRenderTexture = renderTex.Get();
    RenderAnnotations(dc);
    m_activeRenderTexture = nullptr;

    if (freeformLayer) {
        dc->PopLayer();
    }

    HRESULT drawHr = dc->EndDraw();
    dc->SetTarget(oldTarget.Get());
    if (FAILED(drawHr)) return false;

    D3D11_TEXTURE2D_DESC stagingDesc{};
    stagingDesc.Width = cropW;
    stagingDesc.Height = cropH;
    stagingDesc.MipLevels = 1;
    stagingDesc.ArraySize = 1;
    stagingDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    stagingDesc.SampleDesc.Count = 1;
    stagingDesc.Usage = D3D11_USAGE_STAGING;
    stagingDesc.BindFlags = 0;
    stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;

    ComPtr<ID3D11Texture2D> stagingTex;
    if (FAILED(g_d2d.D3DDevice()->CreateTexture2D(&stagingDesc, nullptr, &stagingTex))) return false;

    D3D11_BOX srcBox{};
    srcBox.left   = left;
    srcBox.top    = top;
    srcBox.front  = 0;
    srcBox.right  = right;
    srcBox.bottom = bottom;
    srcBox.back   = 1;

    g_d2d.D3DContext()->CopySubresourceRegion(
        stagingTex.Get(), 0, 0, 0, 0,
        renderTex.Get(), 0, &srcBox
    );

    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (FAILED(g_d2d.D3DContext()->Map(stagingTex.Get(), 0, D3D11_MAP_READ, 0, &mapped))) return false;

    outW = cropW;
    outH = cropH;
    outBgra.resize(static_cast<size_t>(cropW) * cropH * 4);
    for (UINT row = 0; row < cropH; ++row) {
        memcpy(outBgra.data() + static_cast<size_t>(row) * cropW * 4,
               static_cast<const BYTE*>(mapped.pData) + static_cast<size_t>(row) * mapped.RowPitch,
               static_cast<size_t>(cropW) * 4);
    }

    g_d2d.D3DContext()->Unmap(stagingTex.Get(), 0);
    return true;
}

void OverlayWindow::CopyToClipboard() noexcept {
    std::vector<BYTE> bgra;
    UINT width = 0, height = 0;
    if (!RenderSnipImage(bgra, width, height) || bgra.empty()) {
        if (!m_selection.IsValid()) return;
        RECT sel = m_selection.NormalizedRect();
        Hide();
        if (OnConfirm) OnConfirm(sel);
        return;
    }

    FrameBuffer tempFb{
        bgra.data(),
        width * 4,
        width,
        height
    };
    RECT full{ 0, 0, static_cast<LONG>(width), static_cast<LONG>(height) };
    Clipboard::WriteDIB(tempFb, full, m_hwnd);
    Clipboard::WritePNGAsync(tempFb, full);

    Hide();
    if (OnCancel) OnCancel();
}

void OverlayWindow::SaveToFile() noexcept {
    std::vector<BYTE> bgra;
    UINT width = 0, height = 0;
    if (!RenderSnipImage(bgra, width, height) || bgra.empty()) return;

    PWSTR picturesFolder = nullptr;
    std::wstring saveDir;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Pictures, KF_FLAG_CREATE, nullptr, &picturesFolder)) && picturesFolder) {
        saveDir = std::wstring(picturesFolder) + L"\\Snippy";
        CoTaskMemFree(picturesFolder);
    } else {
        WCHAR myPics[MAX_PATH];
        if (SHGetFolderPathW(nullptr, CSIDL_MYPICTURES, nullptr, 0, myPics) == S_OK) {
            saveDir = std::wstring(myPics) + L"\\Snippy";
        } else {
            saveDir = L"Snippy";
        }
    }

    CreateDirectoryW(saveDir.c_str(), nullptr);

    SYSTEMTIME st;
    GetLocalTime(&st);
    WCHAR baseName[64];
    swprintf_s(baseName, L"Snippy_%04d%02d%02d_%02d%02d%02d",
               st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);

    std::wstring filePath = saveDir + L"\\" + baseName + L".png";
    int counter = 1;
    while (GetFileAttributesW(filePath.c_str()) != INVALID_FILE_ATTRIBUTES) {
        WCHAR suffix[16];
        swprintf_s(suffix, L"_%d.png", counter++);
        filePath = saveDir + L"\\" + baseName + suffix;
    }

    ComPtr<IWICImagingFactory> wic;
    if (SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic)))) {
        ComPtr<IWICStream> stream;
        if (SUCCEEDED(wic->CreateStream(&stream)) &&
            SUCCEEDED(stream->InitializeFromFilename(filePath.c_str(), GENERIC_WRITE))) {
            ComPtr<IWICBitmapEncoder> encoder;
            if (SUCCEEDED(wic->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder)) &&
                SUCCEEDED(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache))) {
                ComPtr<IWICBitmapFrameEncode> frame;
                ComPtr<IPropertyBag2> props;
                if (SUCCEEDED(encoder->CreateNewFrame(&frame, &props)) &&
                    SUCCEEDED(frame->Initialize(props.Get()))) {
                    frame->SetSize(width, height);
                    WICPixelFormatGUID fmt = GUID_WICPixelFormat32bppBGRA;
                    frame->SetPixelFormat(&fmt);
                    frame->WritePixels(height, width * 4, static_cast<UINT>(bgra.size()), bgra.data());
                    frame->Commit();
                    encoder->Commit();
                }
            }
        }
    }

    Hide();
    if (OnCancel) OnCancel();
}

void OverlayWindow::PopulateSystemFonts() noexcept {
    if (!m_systemFonts.empty()) return;
    if (!g_dwrite.Factory()) return;

    ComPtr<IDWriteFontCollection> collection;
    if (SUCCEEDED(g_dwrite.Factory()->GetSystemFontCollection(&collection, FALSE))) {
        UINT32 familyCount = collection->GetFontFamilyCount();
        std::vector<std::wstring> names;
        names.reserve(familyCount);

        for (UINT32 i = 0; i < familyCount; ++i) {
            ComPtr<IDWriteFontFamily> family;
            if (FAILED(collection->GetFontFamily(i, &family))) continue;

            ComPtr<IDWriteLocalizedStrings> familyNames;
            if (FAILED(family->GetFamilyNames(&familyNames))) continue;

            UINT32 index = 0;
            BOOL exists = FALSE;
            familyNames->FindLocaleName(L"en-us", &index, &exists);
            if (!exists) index = 0;

            UINT32 length = 0;
            if (FAILED(familyNames->GetStringLength(index, &length))) continue;

            std::wstring name(length + 1, L'\0');
            if (SUCCEEDED(familyNames->GetString(index, name.data(), length + 1))) {
                name.resize(length);
                if (!name.empty() && name[0] != L'@') {
                    names.push_back(name);
                }
            }
        }

        std::sort(names.begin(), names.end(), [](const std::wstring& a, const std::wstring& b) {
            return _wcsicmp(a.c_str(), b.c_str()) < 0;
        });
        names.erase(std::unique(names.begin(), names.end()), names.end());
        m_systemFonts = std::move(names);
    }

    if (m_systemFonts.empty()) {
        m_systemFonts = {
            L"Arial", L"Calibri", L"Cascadia Code", L"Consolas",
            L"Courier New", L"Georgia", L"Impact", L"Segoe UI",
            L"Tahoma", L"Times New Roman", L"Trebuchet MS", L"Verdana"
        };
    }
}

D2D1_RECT_F OverlayWindow::GetAnnotationBounds(const Annotation& ann) const noexcept {
    switch (ann.type) {
    case AnnotationType::Rect: {
        const auto& ra = std::get<RectAnnotation>(ann.data);
        return D2D1::RectF(
            std::min(ra.rect.left, ra.rect.right),
            std::min(ra.rect.top, ra.rect.bottom),
            std::max(ra.rect.left, ra.rect.right),
            std::max(ra.rect.top, ra.rect.bottom)
        );
    }
    case AnnotationType::Ellipse: {
        const auto& ea = std::get<EllipseAnnotation>(ann.data);
        return D2D1::RectF(
            ea.ellipse.point.x - ea.ellipse.radiusX,
            ea.ellipse.point.y - ea.ellipse.radiusY,
            ea.ellipse.point.x + ea.ellipse.radiusX,
            ea.ellipse.point.y + ea.ellipse.radiusY
        );
    }
    case AnnotationType::Arrow: {
        const auto& aa = std::get<ArrowAnnotation>(ann.data);
        return D2D1::RectF(
            std::min(aa.start.x, aa.end.x),
            std::min(aa.start.y, aa.end.y),
            std::max(aa.start.x, aa.end.x),
            std::max(aa.start.y, aa.end.y)
        );
    }
    case AnnotationType::Text: {
        const auto& ta = std::get<TextAnnotation>(ann.data);
        D2D1_RECT_F base = TextTool_GetBounds(D2D1::Point2F(0, 0), ta.text, ta.fontSize, ta.fontFamily, g_dwrite.Factory());
        float w = (base.right - base.left) * ta.scaleX;
        float h = (base.bottom - base.top) * ta.scaleY;
        float left = (w >= 0.f) ? ta.origin.x : ta.origin.x + w;
        float right = (w >= 0.f) ? ta.origin.x + w : ta.origin.x;
        float top = (h >= 0.f) ? ta.origin.y : ta.origin.y + h;
        float bottom = (h >= 0.f) ? ta.origin.y + h : ta.origin.y;
        return D2D1::RectF(left, top, right, bottom);
    }
    case AnnotationType::Blur: {
        const auto& ba = std::get<BlurAnnotation>(ann.data);
        return D2D1::RectF(
            std::min(ba.rect.left, ba.rect.right),
            std::min(ba.rect.top, ba.rect.bottom),
            std::max(ba.rect.left, ba.rect.right),
            std::max(ba.rect.top, ba.rect.bottom)
        );
    }
    case AnnotationType::FreeDraw: {
        const auto& fd = std::get<FreeDrawAnnotation>(ann.data);
        if (fd.points.empty()) return D2D1::RectF(0, 0, 0, 0);
        float minX = fd.points[0].x, maxX = minX;
        float minY = fd.points[0].y, maxY = minY;
        for (const auto& p : fd.points) {
            minX = std::min(minX, p.x); maxX = std::max(maxX, p.x);
            minY = std::min(minY, p.y); maxY = std::max(maxY, p.y);
        }
        if (maxX - minX < 12.f) { minX -= 6.f; maxX += 6.f; }
        if (maxY - minY < 12.f) { minY -= 6.f; maxY += 6.f; }
        return D2D1::RectF(minX, minY, maxX, maxY);
    }
    case AnnotationType::Counter: {
        const auto& ca = std::get<CounterAnnotation>(ann.data);
        float minX = std::min(ca.center.x - 16.f, ca.pointer.x - 6.f);
        float maxX = std::max(ca.center.x + 16.f, ca.pointer.x + 6.f);
        float minY = std::min(ca.center.y - 16.f, ca.pointer.y - 6.f);
        float maxY = std::max(ca.center.y + 16.f, ca.pointer.y + 6.f);
        return D2D1::RectF(minX, minY, maxX, maxY);
    }
    }
    return D2D1::RectF(0, 0, 0, 0);
}

int OverlayWindow::HitTestSelectedAnnotationHandle(D2D1_POINT_2F pt) const noexcept {
    if (m_selectedAnnotationIndex < 0 || m_selectedAnnotationIndex >= static_cast<int>(m_annotations.size())) {
        return -1;
    }
    const auto& ann = m_annotations[m_selectedAnnotationIndex];
    if (ann.type == AnnotationType::Arrow) {
        const auto& aa = std::get<ArrowAnnotation>(ann.data);
        constexpr float rSq = 14.f * 14.f;
        float d0 = Dist2(pt, aa.start);
        float d1 = Dist2(pt, aa.end);
        if (d0 <= rSq && d0 <= d1) return 0;
        if (d1 <= rSq) return 1;
        return -1;
    }
    if (ann.type == AnnotationType::Counter) {
        const auto& ca = std::get<CounterAnnotation>(ann.data);
        constexpr float rSq = 14.f * 14.f;
        float d0 = Dist2(pt, ca.center);
        float d1 = Dist2(pt, ca.pointer);
        if (d0 <= rSq && d0 <= d1) return 0;
        if (d1 <= rSq) return 1;
        return -1;
    }

    D2D1_RECT_F b = GetAnnotationBounds(ann);
    D2D1_RECT_F out = D2D1::RectF(b.left - 4.f, b.top - 4.f, b.right + 4.f, b.bottom + 4.f);
    const D2D1_POINT_2F handles[8] = {
        { out.left, out.top },
        { (out.left + out.right) * 0.5f, out.top },
        { out.right, out.top },
        { out.right, (out.top + out.bottom) * 0.5f },
        { out.right, out.bottom },
        { (out.left + out.right) * 0.5f, out.bottom },
        { out.left, out.bottom },
        { out.left, (out.top + out.bottom) * 0.5f }
    };

    constexpr float hitRadiusSq = 12.f * 12.f;
    float nearestDist = hitRadiusSq + 1.f;
    int nearest = -1;
    for (int i = 0; i < 8; ++i) {
        float d = Dist2(pt, handles[i]);
        if (d < nearestDist) {
            nearestDist = d;
            nearest = i;
        }
    }
    return nearest;
}

void OverlayWindow::ResizeOrMoveAnnotation(D2D1_POINT_2F cpt) noexcept {
    if (m_selectedAnnotationIndex < 0 || m_selectedAnnotationIndex >= static_cast<int>(m_annotations.size())) {
        return;
    }
    auto& ann = m_annotations[m_selectedAnnotationIndex];
    float dx = cpt.x - m_dragStartPoint.x;
    float dy = cpt.y - m_dragStartPoint.y;

    if (m_annotationResizeHandle == 8) {
        // Move whole annotation
        switch (ann.type) {
        case AnnotationType::Rect: {
            const auto& start = std::get<RectAnnotation>(m_dragStartAnnotation.data);
            auto& ra = std::get<RectAnnotation>(ann.data);
            ra.rect = D2D1::RectF(start.rect.left + dx, start.rect.top + dy,
                                 start.rect.right + dx, start.rect.bottom + dy);
            break;
        }
        case AnnotationType::Ellipse: {
            const auto& start = std::get<EllipseAnnotation>(m_dragStartAnnotation.data);
            auto& ea = std::get<EllipseAnnotation>(ann.data);
            ea.ellipse.point = D2D1::Point2F(start.ellipse.point.x + dx, start.ellipse.point.y + dy);
            break;
        }
        case AnnotationType::Arrow: {
            const auto& start = std::get<ArrowAnnotation>(m_dragStartAnnotation.data);
            auto& aa = std::get<ArrowAnnotation>(ann.data);
            aa.start = D2D1::Point2F(start.start.x + dx, start.start.y + dy);
            aa.end   = D2D1::Point2F(start.end.x + dx, start.end.y + dy);
            break;
        }
        case AnnotationType::Text: {
            const auto& start = std::get<TextAnnotation>(m_dragStartAnnotation.data);
            auto& ta = std::get<TextAnnotation>(ann.data);
            ta.origin = D2D1::Point2F(start.origin.x + dx, start.origin.y + dy);
            break;
        }
        case AnnotationType::Blur: {
            const auto& start = std::get<BlurAnnotation>(m_dragStartAnnotation.data);
            auto& ba = std::get<BlurAnnotation>(ann.data);
            ba.rect = D2D1::RectF(start.rect.left + dx, start.rect.top + dy,
                                 start.rect.right + dx, start.rect.bottom + dy);
            break;
        }
        case AnnotationType::FreeDraw: {
            const auto& start = std::get<FreeDrawAnnotation>(m_dragStartAnnotation.data);
            auto& fd = std::get<FreeDrawAnnotation>(ann.data);
            fd.points.resize(start.points.size());
            for (size_t i = 0; i < start.points.size(); ++i) {
                fd.points[i] = D2D1::Point2F(start.points[i].x + dx, start.points[i].y + dy);
            }
            break;
        }
        case AnnotationType::Counter: {
            const auto& start = std::get<CounterAnnotation>(m_dragStartAnnotation.data);
            auto& ca = std::get<CounterAnnotation>(ann.data);
            ca.center  = D2D1::Point2F(start.center.x + dx, start.center.y + dy);
            ca.pointer = D2D1::Point2F(start.pointer.x + dx, start.pointer.y + dy);
            break;
        }
        }
        return;
    }

    // Resize handles
    if (ann.type == AnnotationType::Arrow) {
        auto& aa = std::get<ArrowAnnotation>(ann.data);
        if (m_annotationResizeHandle == 0) aa.start = cpt;
        else if (m_annotationResizeHandle == 1) aa.end = cpt;
        return;
    }
    if (ann.type == AnnotationType::Counter) {
        auto& ca = std::get<CounterAnnotation>(ann.data);
        constexpr float kMinPointerDist = 22.0f;
        if (m_annotationResizeHandle == 0) {
            float pdx = cpt.x - ca.pointer.x;
            float pdy = cpt.y - ca.pointer.y;
            float dist = sqrtf(pdx * pdx + pdy * pdy);
            if (dist < kMinPointerDist) {
                if (dist < 0.001f) { pdx = -kMinPointerDist; pdy = 0.f; }
                else { float s = kMinPointerDist / dist; pdx *= s; pdy *= s; }
                ca.center.x = ca.pointer.x + pdx;
                ca.center.y = ca.pointer.y + pdy;
            } else {
                ca.center = cpt;
            }
            ca.hasPointer = true;
        } else if (m_annotationResizeHandle == 1) {
            float pdx = cpt.x - ca.center.x;
            float pdy = cpt.y - ca.center.y;
            float dist = sqrtf(pdx * pdx + pdy * pdy);
            if (dist < kMinPointerDist) {
                if (dist < 0.001f) { pdx = kMinPointerDist; pdy = 0.f; }
                else { float s = kMinPointerDist / dist; pdx *= s; pdy *= s; }
                ca.pointer.x = ca.center.x + pdx;
                ca.pointer.y = ca.center.y + pdy;
            } else {
                ca.pointer = cpt;
            }
            ca.hasPointer = true;
        }
        return;
    }

    float L0 = m_annotationStartBounds.left;
    float T0 = m_annotationStartBounds.top;
    float R0 = m_annotationStartBounds.right;
    float B0 = m_annotationStartBounds.bottom;
    float W0 = std::max(4.f, R0 - L0);
    float H0 = std::max(4.f, B0 - T0);

    float L1 = L0, T1 = T0, R1 = R0, B1 = B0;
    switch (m_annotationResizeHandle) {
    case 0: L1 = std::min(L0 + dx, R1 - 8.f); T1 = std::min(T0 + dy, B1 - 8.f); break;
    case 1: T1 = std::min(T0 + dy, B1 - 8.f); break;
    case 2: R1 = std::max(R0 + dx, L1 + 8.f); T1 = std::min(T0 + dy, B1 - 8.f); break;
    case 3: R1 = std::max(R0 + dx, L1 + 8.f); break;
    case 4: R1 = std::max(R0 + dx, L1 + 8.f); B1 = std::max(B0 + dy, T1 + 8.f); break;
    case 5: B1 = std::max(B0 + dy, T1 + 8.f); break;
    case 6: L1 = std::min(L0 + dx, R1 - 8.f); B1 = std::max(B0 + dy, T1 + 8.f); break;
    case 7: L1 = std::min(L0 + dx, R1 - 8.f); break;
    default: return;
    }

    switch (ann.type) {
    case AnnotationType::Rect: {
        auto& ra = std::get<RectAnnotation>(ann.data);
        ra.rect = D2D1::RectF(L1, T1, R1, B1);
        break;
    }
    case AnnotationType::Ellipse: {
        auto& ea = std::get<EllipseAnnotation>(ann.data);
        ea.ellipse.point = D2D1::Point2F((L1 + R1) * 0.5f, (T1 + B1) * 0.5f);
        ea.ellipse.radiusX = std::max(2.0f, (R1 - L1) * 0.5f);
        ea.ellipse.radiusY = std::max(2.0f, (B1 - T1) * 0.5f);
        break;
    }
    case AnnotationType::Blur: {
        auto& ba = std::get<BlurAnnotation>(ann.data);
        ba.rect = D2D1::RectF(L1, T1, R1, B1);
        break;
    }
    case AnnotationType::Text: {
        auto& ta = std::get<TextAnnotation>(ann.data);
        const auto& startTa = std::get<TextAnnotation>(m_dragStartAnnotation.data);
        float baseW = (startTa.scaleX > 0.001f) ? (W0 / startTa.scaleX) : W0;
        float baseH = (startTa.scaleY > 0.001f) ? (H0 / startTa.scaleY) : H0;

        switch (m_annotationResizeHandle) {
        case 0: { // NW (anchored at SE: R0, B0)
            float newW = std::max(10.f, R0 - (L0 + dx));
            float newH = std::max(10.f, B0 - (T0 + dy));
            if (GetAsyncKeyState(VK_SHIFT) < 0) {
                float scale = std::max(newW / W0, newH / H0);
                newW = W0 * scale; newH = H0 * scale;
            }
            ta.origin = D2D1::Point2F(R0 - newW, B0 - newH);
            ta.scaleX = newW / baseW;
            ta.scaleY = newH / baseH;
            break;
        }
        case 1: { // N (anchored at bottom B0)
            float newH = std::max(10.f, B0 - (T0 + dy));
            ta.origin = D2D1::Point2F(startTa.origin.x, B0 - newH);
            ta.scaleX = startTa.scaleX;
            ta.scaleY = newH / baseH;
            break;
        }
        case 2: { // NE (anchored at SW: L0, B0)
            float newW = std::max(10.f, (R0 + dx) - L0);
            float newH = std::max(10.f, B0 - (T0 + dy));
            if (GetAsyncKeyState(VK_SHIFT) < 0) {
                float scale = std::max(newW / W0, newH / H0);
                newW = W0 * scale; newH = H0 * scale;
            }
            ta.origin = D2D1::Point2F(L0, B0 - newH);
            ta.scaleX = newW / baseW;
            ta.scaleY = newH / baseH;
            break;
        }
        case 3: { // E (anchored at left L0)
            float newW = std::max(10.f, (R0 + dx) - L0);
            ta.origin = startTa.origin;
            ta.scaleX = newW / baseW;
            ta.scaleY = startTa.scaleY;
            break;
        }
        case 4: { // SE (anchored at NW: L0, T0)
            float newW = std::max(10.f, (R0 + dx) - L0);
            float newH = std::max(10.f, (B0 + dy) - T0);
            if (GetAsyncKeyState(VK_SHIFT) < 0) {
                float scale = std::max(newW / W0, newH / H0);
                newW = W0 * scale; newH = H0 * scale;
            }
            ta.origin = D2D1::Point2F(L0, T0);
            ta.scaleX = newW / baseW;
            ta.scaleY = newH / baseH;
            break;
        }
        case 5: { // S (anchored at top T0)
            float newH = std::max(10.f, (B0 + dy) - T0);
            ta.origin = startTa.origin;
            ta.scaleX = startTa.scaleX;
            ta.scaleY = newH / baseH;
            break;
        }
        case 6: { // SW (anchored at NE: R0, T0)
            float newW = std::max(10.f, R0 - (L0 + dx));
            float newH = std::max(10.f, (B0 + dy) - T0);
            if (GetAsyncKeyState(VK_SHIFT) < 0) {
                float scale = std::max(newW / W0, newH / H0);
                newW = W0 * scale; newH = H0 * scale;
            }
            ta.origin = D2D1::Point2F(R0 - newW, T0);
            ta.scaleX = newW / baseW;
            ta.scaleY = newH / baseH;
            break;
        }
        case 7: { // W (anchored at right R0)
            float newW = std::max(10.f, R0 - (L0 + dx));
            ta.origin = D2D1::Point2F(R0 - newW, startTa.origin.y);
            ta.scaleX = newW / baseW;
            ta.scaleY = startTa.scaleY;
            break;
        }
        default: break;
        }
        break;
    }
    case AnnotationType::FreeDraw: {
        auto& fd = std::get<FreeDrawAnnotation>(ann.data);
        const auto& startFd = std::get<FreeDrawAnnotation>(m_dragStartAnnotation.data);
        fd.points.resize(startFd.points.size());
        for (size_t i = 0; i < startFd.points.size(); ++i) {
            float relX = (W0 > 1.f) ? (startFd.points[i].x - L0) / W0 : 0.5f;
            float relY = (H0 > 1.f) ? (startFd.points[i].y - T0) / H0 : 0.5f;
            fd.points[i] = D2D1::Point2F(L1 + relX * (R1 - L1), T1 + relY * (B1 - T1));
        }
        break;
    }
    default:
        break;
    }
}

void OverlayWindow::DrawSelectedAnnotationOutline(ID2D1DeviceContext* dc) noexcept {
    if (m_selectedAnnotationIndex < 0 || m_selectedAnnotationIndex >= static_cast<int>(m_annotations.size())) {
        return;
    }
    const auto& ann = m_annotations[m_selectedAnnotationIndex];

    auto drawHandle = [&](float x, float y) {
        float half = kHandleSize * 0.5f;
        dc->FillRectangle(D2D1::RectF(x - half, y - half, x + half, y + half), m_labelFgBrush.Get());
        dc->DrawRectangle(D2D1::RectF(x - half, y - half, x + half, y + half), m_selBorderBrush.Get(), 1.5f);
    };

    if (ann.type == AnnotationType::Arrow) {
        const auto& aa = std::get<ArrowAnnotation>(ann.data);
        drawHandle(aa.start.x, aa.start.y);
        drawHandle(aa.end.x, aa.end.y);
        return;
    }
    if (ann.type == AnnotationType::Counter) {
        const auto& ca = std::get<CounterAnnotation>(ann.data);
        drawHandle(ca.center.x, ca.center.y);
        drawHandle(ca.pointer.x, ca.pointer.y);
        return;
    }

    D2D1_RECT_F b = GetAnnotationBounds(ann);
    D2D1_RECT_F out = D2D1::RectF(b.left - 4.f, b.top - 4.f, b.right + 4.f, b.bottom + 4.f);
    dc->DrawRectangle(out, m_selBorderBrush.Get(), 1.5f, m_dashStroke.Get());

    drawHandle(out.left, out.top);
    drawHandle((out.left + out.right) * 0.5f, out.top);
    drawHandle(out.right, out.top);
    drawHandle(out.right, (out.top + out.bottom) * 0.5f);
    drawHandle(out.right, out.bottom);
    drawHandle((out.left + out.right) * 0.5f, out.bottom);
    drawHandle(out.left, out.bottom);
    drawHandle(out.left, (out.top + out.bottom) * 0.5f);
}

bool OverlayWindow::AnnotationHit(const Annotation& ann, D2D1_POINT_2F pt) const noexcept {
    switch (ann.type) {
    case AnnotationType::Rect: {
        const auto& r = std::get<RectAnnotation>(ann.data).rect;
        return pt.x >= r.left - 6 && pt.x <= r.right + 6 &&
               pt.y >= r.top - 6 && pt.y <= r.bottom + 6;
    }
    case AnnotationType::Ellipse: {
        const auto& e = std::get<EllipseAnnotation>(ann.data).ellipse;
        if (e.radiusX < 1.f || e.radiusY < 1.f) return false;
        float nx = (pt.x - e.point.x) / (e.radiusX + 6.f);
        float ny = (pt.y - e.point.y) / (e.radiusY + 6.f);
        return nx * nx + ny * ny <= 1.f;
    }
    case AnnotationType::Arrow: {
        const auto& a = std::get<ArrowAnnotation>(ann.data);
        return DistToSegment(pt, a.start, a.end) <= a.width + 8.f;
    }
    case AnnotationType::Text: {
        D2D1_RECT_F b = GetAnnotationBounds(ann);
        return pt.x >= b.left - 4.f && pt.x <= b.right + 4.f &&
               pt.y >= b.top - 4.f && pt.y <= b.bottom + 4.f;
    }
    case AnnotationType::Blur: {
        const auto& r = std::get<BlurAnnotation>(ann.data).rect;
        return pt.x >= r.left && pt.x <= r.right && pt.y >= r.top && pt.y <= r.bottom;
    }
    case AnnotationType::FreeDraw: {
        const auto& fd = std::get<FreeDrawAnnotation>(ann.data);
        float tol = fd.width + 6.f;
        for (size_t i = 1; i < fd.points.size(); ++i) {
            if (DistToSegment(pt, fd.points[i - 1], fd.points[i]) <= tol)
                return true;
        }
        return false;
    }
    case AnnotationType::Counter: {
        const auto& c = std::get<CounterAnnotation>(ann.data);
        float dx = pt.x - c.center.x;
        float dy = pt.y - c.center.y;
        if (dx * dx + dy * dy <= 16.f * 16.f) return true;
        if (DistToSegment(pt, c.center, c.pointer) <= 12.f) return true;
        return false;
    }
    }
    return false;
}

void OverlayWindow::EraseAt(D2D1_POINT_2F pt) noexcept {
    auto it = std::remove_if(m_annotations.begin(), m_annotations.end(),
        [&](const Annotation& a) { return AnnotationHit(a, pt); });
    if (it != m_annotations.end()) {
        if (!m_eraseUndoPushed) {
            PushUndoState();
            m_eraseUndoPushed = true;
        }
        m_annotations.erase(it, m_annotations.end());
    }
}

// ── Icon drawing ──────────────────────────────────────────────────────────────

void OverlayWindow::DrawFluentGlyph(ID2D1DeviceContext* dc, WCHAR glyph, D2D1_RECT_F rect,
                                    ID2D1Brush* foreground,
                                    IDWriteTextFormat* format) noexcept {
    WCHAR s[2] = { glyph, 0 };
    IDWriteTextFormat* tf = format ? format : g_dwrite.IconFont();
    dc->DrawTextW(s, 1, tf, rect, foreground);
}

void OverlayWindow::DrawToolbarIcon(ID2D1DeviceContext* dc, int id, D2D1_RECT_F rect,
                                   bool isActive, bool isHovered) noexcept {
    WCHAR glyph = 0;
    IDWriteTextFormat* format = g_dwrite.IconFont();
    switch (static_cast<ToolbarButton>(id)) {
    case ToolbarButton::Move:
        glyph = g_dwrite.IsWindows11OrGreater() ? L'\uE7C2' : L'\uE759';
        format = g_dwrite.IconFont();
        break;
    case ToolbarButton::Shapes:
        if (m_toolMode == ToolMode::Arrow) glyph = L'\uE8AD';
        else if (m_toolMode == ToolMode::Line) glyph = L'\uE738';
        else if (m_toolMode == ToolMode::Ellipse) glyph = L'\uEA3A';
        else if (m_toolMode == ToolMode::FilledRect) glyph = L'\uE73B';
        else glyph = L'\uE739';
        format = g_dwrite.IconFont14();
        break;
    case ToolbarButton::Blur:
        if (m_blurStyle == BlurStyle::Mosaic) glyph = L'\uE80A';
        else if (m_blurStyle == BlurStyle::Invert) glyph = L'\uE790';
        else glyph = L'\uEB42';
        format = g_dwrite.IconFont14();
        break;
    case ToolbarButton::Region:
        glyph = L'\uE7A8';
        format = g_dwrite.IconFont14();
        break;
    case ToolbarButton::Fullscreen:
        glyph = L'\uE9A6';
        format = g_dwrite.IconFont14();
        break;
    case ToolbarButton::Freeform:
        glyph = L'\uF408';
        format = g_dwrite.IconFont14();
        break;
    case ToolbarButton::ColorPicker:
        glyph = L'\uEF3C';
        format = g_dwrite.IconFont14();
        break;
    case ToolbarButton::TextExtract:
        glyph = L'\uF714';
        format = g_dwrite.IconFont14();
        break;
    case ToolbarButton::Pen:
        if (m_toolMode == ToolMode::Highlighter) glyph = L'\uE7E6';
        else if (m_toolMode == ToolMode::Eraser) glyph = L'\uE75C';
        else glyph = L'\uE70F';
        format = g_dwrite.IconFont15();
        break;
    case ToolbarButton::Highlighter:
        glyph = L'\uE7E6';
        format = g_dwrite.IconFont15();
        break;
    case ToolbarButton::Eraser:
        glyph = L'\uE75C';
        format = g_dwrite.IconFont15();
        break;
    case ToolbarButton::Rect:
        glyph = L'\uE739';
        format = g_dwrite.IconFont14();
        break;
    case ToolbarButton::Ellipse:
        glyph = L'\uEA3A';
        format = g_dwrite.IconFont14();
        break;
    case ToolbarButton::Arrow:
        glyph = L'\uE8AD';
        format = g_dwrite.IconFont14();
        break;
    case ToolbarButton::Text:
        glyph = L'\uE8D2';
        format = g_dwrite.IconFont();
        break;
    case ToolbarButton::Counter:
        glyph = L'\uE8EC';
        format = g_dwrite.IconFont15();
        break;
    case ToolbarButton::Undo:
        glyph = L'\uE7A7';
        format = g_dwrite.IconFont();
        break;
    case ToolbarButton::Redo:
        glyph = L'\uE7A6';
        format = g_dwrite.IconFont();
        break;
    case ToolbarButton::Clear:
        glyph = L'\uE74D';
        format = g_dwrite.IconFont();
        break;
    case ToolbarButton::Save:
        glyph = L'\uE74E';
        format = g_dwrite.IconFont();
        break;
    case ToolbarButton::Copy:
        glyph = L'\uE8C8';
        format = g_dwrite.IconFont();
        break;
    case ToolbarButton::Close:
        glyph = L'\uE711';
        format = g_dwrite.IconFont14();
        break;
    }
    if (glyph) {
        ID2D1Brush* foreground = m_toolbarIconBrush.Get();
        if (isActive) foreground = m_labelFgBrush.Get();
        else if (static_cast<ToolbarButton>(id) == ToolbarButton::Close)
            foreground = isHovered ? m_toolbarCloseHoverBrush.Get() : m_toolbarCloseBrush.Get();
        else if (static_cast<ToolbarButton>(id) == ToolbarButton::Copy)
            foreground = m_toolbarCopyBrush.Get();
        DrawFluentGlyph(dc, glyph, rect, foreground, format);
    }
}

void OverlayWindow::RenderFloatingToolbar(ID2D1DeviceContext* dc) noexcept {
    if (m_toolbarActiveBrush) m_toolbarActiveBrush->SetColor(m_toolbarAccent);
    BuildToolbar();
    if (m_toolbar.empty()) return;
    const float toolbarH = IsEditorOpen() ? kEditorToolbarH : kCaptureToolbarH;

    for (int layer = 8; layer >= 0; --layer) {
        const float spread = static_cast<float>(layer) * 2.f;
        const float alpha = 0.4f / 9.f;
        D2D1_ROUNDED_RECT shadow = D2D1::RoundedRect(
            D2D1::RectF(m_toolbarBounds.left - spread, m_toolbarBounds.top + 4.f - spread,
                        m_toolbarBounds.right + spread, m_toolbarBounds.bottom + 4.f + spread),
            toolbarH * 0.5f + spread, toolbarH * 0.5f + spread);
        m_toolbarShadowBrush->SetColor(D2D1::ColorF(0.f, 0.f, 0.f, alpha));
        dc->FillRoundedRectangle(shadow, m_toolbarShadowBrush.Get());
    }
    m_toolbarShadowBrush->SetColor(D2D1::ColorF(0.f, 0.f, 0.f, 0.4f));

    D2D1_ROUNDED_RECT pill = D2D1::RoundedRect(m_toolbarBounds, toolbarH * 0.5f, toolbarH * 0.5f);
    dc->FillRoundedRectangle(pill, m_toolbarBgBrush.Get());
    dc->DrawRoundedRectangle(pill, m_toolbarBorderBrush.Get(), 1.0f);

    float sepPad = (toolbarH - 12.f) * 0.5f;
    float sepTop = m_toolbarBounds.top + sepPad;
    float sepBot = m_toolbarBounds.bottom - sepPad;
    for (float sx : m_separators) {
        dc->DrawLine(D2D1::Point2F(sx, sepTop), D2D1::Point2F(sx, sepBot),
                     m_separatorBrush.Get(), 1.0f);
    }

    POINT localHover{ m_hoverPt.x - m_monitorRect.left, m_hoverPt.y - m_monitorRect.top };

    for (const auto& btn : m_toolbar) {
        bool isActive = false;
        switch (btn.id) {
        case ToolbarButton::Move:
            isActive = (m_toolMode == ToolMode::Select);
            break;
        case ToolbarButton::Shapes:
            isActive = (m_toolMode == ToolMode::Rect || m_toolMode == ToolMode::FilledRect ||
                        m_toolMode == ToolMode::Ellipse || m_toolMode == ToolMode::Line ||
                        m_toolMode == ToolMode::Arrow);
            break;
        case ToolbarButton::Blur:
            isActive = (m_toolMode == ToolMode::Blur);
            break;
        case ToolbarButton::Region:
            isActive = (m_captureMode == CaptureMode::Region && m_toolMode == ToolMode::Select);
            break;
        case ToolbarButton::Freeform:
            isActive = (m_captureMode == CaptureMode::Freeform);
            break;
        case ToolbarButton::Fullscreen:
            isActive = (m_captureMode == CaptureMode::Fullscreen && m_toolMode == ToolMode::Select);
            break;
        case ToolbarButton::ColorPicker:
            isActive = (m_captureMode == CaptureMode::ColorPicker);
            break;
        case ToolbarButton::TextExtract:
            isActive = (m_captureMode == CaptureMode::TextExtract || m_ocrActive);
            break;
        case ToolbarButton::Pen:         isActive = (m_toolMode == ToolMode::FreeDraw || m_toolMode == ToolMode::Highlighter || m_toolMode == ToolMode::Eraser);    break;
        case ToolbarButton::Highlighter: isActive = (m_toolMode == ToolMode::Highlighter); break;
        case ToolbarButton::Eraser:      isActive = (m_toolMode == ToolMode::Eraser);      break;
        case ToolbarButton::Rect:        isActive = (m_toolMode == ToolMode::Rect);        break;
        case ToolbarButton::Ellipse:     isActive = (m_toolMode == ToolMode::Ellipse);     break;
        case ToolbarButton::Arrow:       isActive = (m_toolMode == ToolMode::Arrow);       break;
        case ToolbarButton::Text:        isActive = (m_toolMode == ToolMode::Text);        break;
        case ToolbarButton::Counter:     isActive = (m_toolMode == ToolMode::Counter);     break;
        default: break;
        }

        D2D1_POINT_2F hp{ static_cast<float>(localHover.x), static_cast<float>(localHover.y) };
        const float closeInset = (btn.rect.right - btn.rect.left - 28.f) * 0.5f;
        const float hitLeft = btn.id == ToolbarButton::Close ? btn.rect.left + closeInset : btn.rect.left;
        const float hitRight = btn.id == ToolbarButton::Close ? btn.rect.right - closeInset : btn.rect.right;
        const bool isHovered = hp.x >= hitLeft && hp.x <= hitRight &&
                               hp.y >= m_toolbarBounds.top && hp.y <= m_toolbarBounds.bottom;

        bool isPopupOpen = false;
        if (m_toolbarPopup != ToolbarPopup::None) {
            ToolbarButton openId = ToolbarButton::Close;
            switch (m_toolbarPopup) {
            case ToolbarPopup::Pen: openId = ToolbarButton::Pen; break;
            case ToolbarPopup::Shapes: openId = ToolbarButton::Shapes; break;
            case ToolbarPopup::Blur: openId = ToolbarButton::Blur; break;
            case ToolbarPopup::Text: openId = ToolbarButton::Text; break;
            case ToolbarPopup::Color:
            case ToolbarPopup::CustomColorPicker: openId = ToolbarButton::AnnotationColor; break;
            default: break;
            }
            if (btn.id == openId) isPopupOpen = true;
        }

        bool mainHovered = isHovered;
        bool dropdownHovered = isPopupOpen;

        if (btn.hasDropdown && isHovered) {
            const float chevronSplit = btn.rect.right - 23.f;
            if (hp.x >= chevronSplit) {
                mainHovered = false;
                dropdownHovered = true;
            }
        }
        
        if (isPopupOpen) {
            mainHovered = true;
            dropdownHovered = true;
        }

        if (isActive || mainHovered || dropdownHovered) {
            constexpr float kActiveH = 26.f;
            constexpr float kActiveRadius = 13.f;
            float pillW = btn.hasDropdown ? 50.f : 34.f;
            
            const float centerX = (btn.rect.left + btn.rect.right) * 0.5f;
            const float centerY = (btn.rect.top + btn.rect.bottom) * 0.5f;
            const D2D1_RECT_F fullBg = D2D1::RectF(
                centerX - pillW * 0.5f,
                centerY - kActiveH * 0.5f,
                centerX + pillW * 0.5f,
                centerY + kActiveH * 0.5f);

            if (btn.hasDropdown) {
                float splitX = btn.rect.right - 23.f;
                
                if (isActive || mainHovered) {
                    dc->PushAxisAlignedClip(D2D1::RectF(fullBg.left, fullBg.top, splitX - 1.0f, fullBg.bottom), D2D1_ANTIALIAS_MODE_ALIASED);
                    dc->FillRoundedRectangle(D2D1::RoundedRect(fullBg, kActiveRadius, kActiveRadius),
                        isActive ? m_toolbarActiveBrush.Get() : m_toolbarHoverBrush.Get());
                    dc->PopAxisAlignedClip();
                }

                if (dropdownHovered) {
                    dc->PushAxisAlignedClip(D2D1::RectF(splitX + 1.0f, fullBg.top, fullBg.right, fullBg.bottom), D2D1_ANTIALIAS_MODE_ALIASED);
                    dc->FillRoundedRectangle(D2D1::RoundedRect(fullBg, kActiveRadius, kActiveRadius), m_toolbarHoverBrush.Get());
                    dc->PopAxisAlignedClip();
                }
            } else {
                if (isActive || mainHovered) {
                    dc->FillRoundedRectangle(D2D1::RoundedRect(fullBg, kActiveRadius, kActiveRadius),
                        isActive ? m_toolbarActiveBrush.Get() : m_toolbarHoverBrush.Get());
                }
            }
        }
        D2D1_RECT_F iconRect = btn.rect;
        if (btn.hasDropdown) iconRect.right -= 22.f;
        if (btn.id == ToolbarButton::AnnotationColor) {
            float cx = (iconRect.left + iconRect.right) * 0.5f;
            float cy = (iconRect.top + iconRect.bottom) * 0.5f;
            m_swatchBrush->SetColor(ToD2D(m_strokeColor));
            dc->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), 7.f, 7.f), m_swatchBrush.Get());
            dc->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), 7.f, 7.f),
                            m_toolbarIconBrush.Get(), 1.f);
        } else {
            DrawToolbarIcon(dc, static_cast<int>(btn.id), iconRect, isActive, isHovered);
        }
        if (btn.hasDropdown) {
            float dividerX = btn.rect.right - 23.f;
            float center = (btn.rect.top + btn.rect.bottom) * 0.5f;
            dc->DrawLine(D2D1::Point2F(dividerX, center - 8.f), 
                         D2D1::Point2F(dividerX, center + 8.f),
                         m_separatorBrush.Get(), 1.0f);

            D2D1_RECT_F chevron = D2D1::RectF(dividerX, btn.rect.top, btn.rect.right - 1.f, btn.rect.bottom);
            DrawFluentGlyph(dc, L'\uE70D', chevron, m_toolbarIconBrush.Get(), g_dwrite.IconFontChevron());
        }
    }

    RenderToolbarPopup(dc);
}

void OverlayWindow::RenderToolbarPopup(ID2D1DeviceContext* dc) noexcept {
    if (m_toolbarPopup == ToolbarPopup::None) return;

    ToolbarButton anchorId = ToolbarButton::Pen;
    float width = 180.f, height = 150.f;
    switch (m_toolbarPopup) {
    case ToolbarPopup::Pen: anchorId = ToolbarButton::Pen; width = 220.f; height = 110.f; break;
    case ToolbarPopup::Shapes: anchorId = ToolbarButton::Shapes; width = 152.f; height = 156.f; break;
    case ToolbarPopup::Blur: anchorId = ToolbarButton::Blur; width = 148.f; height = 102.f; break;
    case ToolbarPopup::Text: anchorId = ToolbarButton::Text; width = 230.f; height = m_fontDropdownOpen ? 312.f : 106.f; break;
    case ToolbarPopup::Color: anchorId = ToolbarButton::AnnotationColor; width = 190.f; height = 98.f; break;
    case ToolbarPopup::CustomColorPicker: anchorId = ToolbarButton::AnnotationColor; width = 310.f; height = 412.f; break;
    default: return;
    }

    auto anchor = std::find_if(m_toolbar.begin(), m_toolbar.end(),
        [&](const TBButton& button) { return button.id == anchorId; });
    if (anchor == m_toolbar.end()) return;

    const float monW = static_cast<float>(m_monitorRect.right - m_monitorRect.left);
    const float monH = static_cast<float>(m_monitorRect.bottom - m_monitorRect.top);
    float left = std::min(anchor->rect.left, monW - width - 4.f);
    float top = m_toolbarBounds.bottom + 7.f;
    if (top + height > monH - 4.f) top = m_toolbarBounds.top - height - 7.f;
    left = std::max(4.f, left);
    top = std::max(4.f, top);
    if (m_toolbarPopup == ToolbarPopup::CustomColorPicker) {
        if (!m_colorPickerPosInitialized) {
            m_colorPickerPos = { left, top };
            m_colorPickerPosInitialized = true;
        }
        left = std::clamp(m_colorPickerPos.x, 4.f, monW - width - 4.f);
        top = std::clamp(m_colorPickerPos.y, 4.f, monH - height - 4.f);
        m_colorPickerPos = { left, top };
    }
    m_popupBounds = D2D1::RectF(left, top, left + width, top + height);

    D2D1_ROUNDED_RECT panel = D2D1::RoundedRect(m_popupBounds, 8.f, 8.f);
    dc->FillRoundedRectangle(panel, m_toolbarBgBrush.Get());
    dc->DrawRoundedRectangle(panel, m_toolbarBorderBrush.Get(), 1.f);

    auto drawLabel = [&](const WCHAR* label, D2D1_RECT_F rect) {
        dc->DrawTextW(label, static_cast<UINT32>(wcslen(label)), g_dwrite.UIFont(),
                      rect, m_toolbarIconBrush.Get());
    };
    auto drawRow = [&](int index, const WCHAR* label, WCHAR glyph, float rowTop, bool active) {
        const float rowH = 28.f;
        D2D1_RECT_F row = D2D1::RectF(left + 5.f, rowTop, left + width - 5.f, rowTop + rowH);
        POINT hover{ m_hoverPt.x - m_monitorRect.left, m_hoverPt.y - m_monitorRect.top };
        bool hovered = hover.x >= row.left && hover.x <= row.right &&
                       hover.y >= row.top && hover.y <= row.bottom;
        if (active || hovered) {
            D2D1_ROUNDED_RECT bg = D2D1::RoundedRect(row, 5.f, 5.f);
            dc->FillRoundedRectangle(bg, active ? m_toolbarActiveBrush.Get() : m_toolbarHoverBrush.Get());
        }
        ID2D1Brush* fgBrush = active ? m_labelFgBrush.Get() : m_toolbarIconBrush.Get();
        if (glyph) {
            DrawFluentGlyph(dc, glyph,
                D2D1::RectF(left + 12.f, rowTop, left + 34.f, rowTop + rowH),
                fgBrush, g_dwrite.IconFont14());
        }
        dc->DrawTextW(label, static_cast<UINT32>(wcslen(label)), g_dwrite.UIFont(),
                      D2D1::RectF(left + 42.f, rowTop, left + width - 12.f, rowTop + rowH),
                      fgBrush);
        (void)index;
    };

    if (m_toolbarPopup == ToolbarPopup::Pen) {
        const WCHAR* hoveredTooltip = nullptr;
        D2D1_POINT_2F tooltipCenter = {};

        auto drawToolBtn = [&](float x, float y, WCHAR icon, bool active, const WCHAR* name) {
            D2D1_RECT_F btnRect = D2D1::RectF(x, y, x + 60.f, y + 28.f);
            POINT hover{ m_hoverPt.x - m_monitorRect.left, m_hoverPt.y - m_monitorRect.top };
            bool hovered = hover.x >= btnRect.left && hover.x <= btnRect.right && hover.y >= btnRect.top && hover.y <= btnRect.bottom;
            if (active || hovered) {
                dc->FillRoundedRectangle(D2D1::RoundedRect(btnRect, 14.f, 14.f),
                                         active ? m_toolbarActiveBrush.Get() : m_toolbarHoverBrush.Get());
            }
            DrawFluentGlyph(dc, icon, D2D1::RectF(x + 16.f, y + 2.f, x + 44.f, y + 26.f),
                            active ? m_labelFgBrush.Get() : m_toolbarIconBrush.Get(),
                            g_dwrite.IconFont15());

            if (hovered && !active) {
                hoveredTooltip = name;
                tooltipCenter = D2D1::Point2F((btnRect.left + btnRect.right) * 0.5f, y + 36.f);
            }
        };
        const bool isHighlighter = (m_toolMode == ToolMode::Highlighter);
        const bool isEraser = (m_toolMode == ToolMode::Eraser);
        const bool isPen = (m_toolMode == ToolMode::FreeDraw) || (!isHighlighter && !isEraser);
        drawToolBtn(left + 10.f, top + 8.f, L'\uE70F', isPen, L"Pen");
        drawToolBtn(left + 80.f, top + 8.f, L'\uE7E6', isHighlighter, L"Highlighter");
        drawToolBtn(left + 150.f, top + 8.f, L'\uE75C', isEraser, L"Eraser");

        dc->DrawLine(D2D1::Point2F(left + 10.f, top + 44.f), D2D1::Point2F(left + width - 10.f, top + 44.f),
                     m_separatorBrush.Get(), 1.0f);

        WCHAR sizeStr[24];
        swprintf_s(sizeStr, L"%.0f px", m_strokeWidth);
        drawLabel(L"Pen Size", D2D1::RectF(left + 12.f, top + 52.f, left + 100.f, top + 70.f));
        
        dc->DrawTextW(sizeStr, static_cast<UINT32>(wcslen(sizeStr)), g_dwrite.UIFontSmall(),
                      D2D1::RectF(left + width - 42.f, top + 52.f, left + width - 10.f, top + 70.f),
                      m_toolbarActiveBrush.Get());
                      
        float sliderLeft = left + 18.f, sliderRight = left + width - 18.f;
        float sliderY = top + 86.f;
        
        dc->DrawLine(D2D1::Point2F(sliderLeft, sliderY), D2D1::Point2F(sliderRight, sliderY),
                     m_toolbarBorderBrush.Get(), 4.f);
                     
        float thumbX = sliderLeft + ((m_strokeWidth - 1.f) / 23.f) * (sliderRight - sliderLeft);
        thumbX = std::max(sliderLeft, std::min(thumbX, sliderRight));
        
        dc->DrawLine(D2D1::Point2F(sliderLeft, sliderY), D2D1::Point2F(thumbX, sliderY),
                     m_toolbarActiveBrush.Get(), 4.f);
                     
        dc->FillEllipse(D2D1::Ellipse(D2D1::Point2F(thumbX, sliderY), 7.5f, 7.5f), m_labelFgBrush.Get());
        dc->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(thumbX, sliderY), 7.5f, 7.5f), m_toolbarActiveBrush.Get(), 2.5f);

        // Draw floating hover tooltip capsule
        if (hoveredTooltip) {
            float textLen = static_cast<float>(wcslen(hoveredTooltip)) * 7.f + 20.f;
            D2D1_RECT_F tipRect = D2D1::RectF(
                tooltipCenter.x - textLen * 0.5f, tooltipCenter.y - 11.f,
                tooltipCenter.x + textLen * 0.5f, tooltipCenter.y + 12.f);
            D2D1_ROUNDED_RECT tipR = D2D1::RoundedRect(tipRect, 10.f, 10.f);
            dc->FillRoundedRectangle(tipR, m_toolbarBgBrush.Get());
            dc->DrawRoundedRectangle(tipR, m_toolbarBorderBrush.Get(), 1.0f);

            ComPtr<IDWriteTextFormat> tipFormat;
            if (SUCCEEDED(g_dwrite.Factory()->CreateTextFormat(
                    L"Segoe UI", nullptr,
                    DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
                    DWRITE_FONT_STRETCH_NORMAL, 11.0f, L"en-us", &tipFormat))) {
                tipFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
                tipFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
                dc->DrawTextW(hoveredTooltip, static_cast<UINT32>(wcslen(hoveredTooltip)), tipFormat.Get(),
                              tipRect, m_labelFgBrush.Get());
            }
        }
    } else if (m_toolbarPopup == ToolbarPopup::Shapes) {
        static constexpr const WCHAR* labels[] = { L"Arrow", L"Rectangle", L"Line", L"Circle", L"Fill Rectangle" };
        static constexpr WCHAR glyphs[] = { L'\uE8AD', L'\uE739', L'\uE738', L'\uEA3A', L'\uE73B' };
        static constexpr ToolMode modes[] = {
            ToolMode::Arrow, ToolMode::Rect, ToolMode::Line,
            ToolMode::Ellipse, ToolMode::FilledRect
        };
        const bool anyShapeActive = (m_toolMode == ToolMode::Arrow || m_toolMode == ToolMode::Rect ||
                                     m_toolMode == ToolMode::Line || m_toolMode == ToolMode::Ellipse ||
                                     m_toolMode == ToolMode::FilledRect);
        for (int i = 0; i < 5; ++i) {
            bool active = anyShapeActive ? (m_toolMode == modes[i]) : (modes[i] == ToolMode::Rect);
            drawRow(i, labels[i], glyphs[i], top + 6.f + i * 29.f, active);
        }
    } else if (m_toolbarPopup == ToolbarPopup::Blur) {
        drawRow(0, L"Gaussian Blur", L'\uEB42', top + 6.f, m_blurStyle == BlurStyle::Gaussian);
        drawRow(1, L"Mosaic Blur", L'\uE80A', top + 37.f, m_blurStyle == BlurStyle::Mosaic);
        drawRow(2, L"Invert Colors", L'\uE790', top + 68.f, m_blurStyle == BlurStyle::Invert);
    } else if (m_toolbarPopup == ToolbarPopup::Text) {
        drawLabel(L"Font Family", D2D1::RectF(left + 12.f, top + 7.f, left + width - 12.f, top + 25.f));
        D2D1_RECT_F familyBox = D2D1::RectF(left + 10.f, top + 27.f, left + width - 10.f, top + 53.f);
        POINT hp{ m_hoverPt.x - m_monitorRect.left, m_hoverPt.y - m_monitorRect.top };
        bool familyHover = (hp.x >= familyBox.left && hp.x <= familyBox.right && hp.y >= familyBox.top && hp.y <= familyBox.bottom);
        if (familyHover) {
            dc->FillRoundedRectangle(D2D1::RoundedRect(familyBox, 5.f, 5.f), m_toolbarHoverBrush.Get());
        }
        dc->DrawRoundedRectangle(D2D1::RoundedRect(familyBox, 5.f, 5.f), m_toolbarBorderBrush.Get(), 1.f);
        drawLabel(m_fontFamily.c_str(), D2D1::RectF(familyBox.left + 8.f, familyBox.top + 4.f,
                                                    familyBox.right - 24.f, familyBox.bottom - 3.f));
        DrawFluentGlyph(dc, m_fontDropdownOpen ? L'\uE70E' : L'\uE70D',
                        D2D1::RectF(familyBox.right - 21.f, familyBox.top + 4.f,
                                   familyBox.right - 5.f, familyBox.bottom - 3.f),
                        m_toolbarIconBrush.Get(), g_dwrite.IconFontChevron());

        float listBottom = top + 53.f;
        if (m_fontDropdownOpen) {
            PopulateSystemFonts();
            float listTop = top + 57.f;
            listBottom = listTop + 200.f;
            float listLeft = left + 10.f;
            float listRight = left + width - 10.f;
            D2D1_RECT_F listRect = D2D1::RectF(listLeft, listTop, listRight, listBottom);

            dc->FillRoundedRectangle(D2D1::RoundedRect(listRect, 5.f, 5.f), m_toolbarBgBrush.Get());
            dc->DrawRoundedRectangle(D2D1::RoundedRect(listRect, 5.f, 5.f), m_toolbarBorderBrush.Get(), 1.f);

            dc->PushAxisAlignedClip(listRect, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);

            float itemH = 25.f;
            int visibleCount = static_cast<int>((listBottom - listTop) / itemH);
            int maxScroll = std::max(0, static_cast<int>(m_systemFonts.size()) - visibleCount);
            m_fontScrollIndex = std::clamp(m_fontScrollIndex, 0, maxScroll);

            for (int i = 0; i < visibleCount; ++i) {
                int fIdx = m_fontScrollIndex + i;
                if (fIdx >= static_cast<int>(m_systemFonts.size())) break;
                float rowY = listTop + i * itemH;
                D2D1_RECT_F itemRect = D2D1::RectF(listLeft + 2.f, rowY, listRight - 12.f, rowY + itemH);
                bool isHover = (hp.x >= itemRect.left && hp.x <= itemRect.right && hp.y >= itemRect.top && hp.y <= itemRect.bottom);
                bool isCur = (m_systemFonts[fIdx] == m_fontFamily);

                if (isCur) {
                    dc->FillRoundedRectangle(D2D1::RoundedRect(itemRect, 4.f, 4.f), m_toolbarActiveBrush.Get());
                } else if (isHover) {
                    dc->FillRoundedRectangle(D2D1::RoundedRect(itemRect, 4.f, 4.f), m_toolbarHoverBrush.Get());
                }
                drawLabel(m_systemFonts[fIdx].c_str(),
                          D2D1::RectF(itemRect.left + 6.f, itemRect.top + 3.f, itemRect.right - 4.f, itemRect.bottom - 2.f));
            }

            if (m_systemFonts.size() > static_cast<size_t>(visibleCount)) {
                float trackH = listBottom - listTop - 4.f;
                float thumbH = std::max(18.f, trackH * visibleCount / static_cast<float>(m_systemFonts.size()));
                float thumbY = (listTop + 2.f) + (static_cast<float>(m_fontScrollIndex) / maxScroll) * (trackH - thumbH);
                D2D1_RECT_F thumbRect = D2D1::RectF(listRight - 9.f, thumbY, listRight - 4.f, thumbY + thumbH);
                dc->FillRoundedRectangle(D2D1::RoundedRect(thumbRect, 2.5f, 2.5f), m_toolbarBorderBrush.Get());
            }

            dc->PopAxisAlignedClip();
        }

        if (!m_fontDropdownOpen) {
            dc->DrawLine(D2D1::Point2F(left + 10.f, top + 56.f), D2D1::Point2F(left + width - 10.f, top + 56.f),
                         m_separatorBrush.Get(), 1.0f);
        }

        float controlsTop = m_fontDropdownOpen ? (listBottom + 10.f) : (top + 64.f);
        WCHAR size[24];
        swprintf_s(size, L"%.0f pt", m_fontSize);
        drawLabel(L"Font Size", D2D1::RectF(left + 12.f, controlsTop, left + 104.f, controlsTop + 18.f));

        dc->DrawTextW(size, static_cast<UINT32>(wcslen(size)), g_dwrite.UIFontSmall(),
                      D2D1::RectF(left + width - 52.f, controlsTop, left + width - 12.f, controlsTop + 18.f),
                      m_toolbarActiveBrush.Get());

        float sliderLeft = left + 18.f, sliderRight = left + width - 18.f;
        float sliderY = controlsTop + 26.f;

        dc->DrawLine(D2D1::Point2F(sliderLeft, sliderY), D2D1::Point2F(sliderRight, sliderY),
                     m_toolbarBorderBrush.Get(), 4.f);

        float thumbX = sliderLeft + ((m_fontSize - 10.f) / 26.f) * (sliderRight - sliderLeft);
        thumbX = std::max(sliderLeft, std::min(thumbX, sliderRight));

        dc->DrawLine(D2D1::Point2F(sliderLeft, sliderY), D2D1::Point2F(thumbX, sliderY),
                     m_toolbarActiveBrush.Get(), 4.f);

        dc->FillEllipse(D2D1::Ellipse(D2D1::Point2F(thumbX, sliderY), 7.5f, 7.5f), m_labelFgBrush.Get());
        dc->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(thumbX, sliderY), 7.5f, 7.5f), m_toolbarActiveBrush.Get(), 2.5f);
    } else if (m_toolbarPopup == ToolbarPopup::Color) {
        drawLabel(L"Annotation Color", D2D1::RectF(left + 12.f, top + 7.f, left + width - 12.f, top + 26.f));
        static constexpr D2D1_COLOR_F colors[] = {
            { 0.96f, 0.20f, 0.18f, 1.f }, { 1.f, 0.58f, 0.10f, 1.f },
            { 1.f, 0.82f, 0.12f, 1.f }, { 0.20f, 0.78f, 0.35f, 1.f },
            { 0.20f, 0.68f, 0.90f, 1.f }, { 0.00f, 0.48f, 1.00f, 1.f },
            { 0.69f, 0.32f, 0.87f, 1.f }, { 1.00f, 0.18f, 0.33f, 1.f },
            { 1.f, 1.f, 1.f, 1.f }
        };
        POINT hp{ m_hoverPt.x - m_monitorRect.left, m_hoverPt.y - m_monitorRect.top };
        for (int i = 0; i < 9; ++i) {
            float cx = left + 24.f + (i % 5) * 33.f;
            float cy = top + 47.f + (i / 5) * 29.f;
            m_swatchBrush->SetColor(colors[i]);
            dc->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), 9.f, 9.f), m_swatchBrush.Get());
            dc->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), 9.f, 9.f),
                            m_toolbarBorderBrush.Get(), 1.f);
        }
        float cx = left + 24.f + 4 * 33.f;
        float cy = top + 47.f + 1 * 29.f;
        const D2D1_ELLIPSE rainbowCircle = D2D1::Ellipse(D2D1::Point2F(cx, cy), 10.f, 10.f);
        bool isHovered = (hp.x - cx) * (hp.x - cx) + (hp.y - cy) * (hp.y - cy) <= 100.f;

        D2D1_GRADIENT_STOP stops[] = {
            { 0.0f, D2D1::ColorF(0xFF3B30) },
            { 0.2f, D2D1::ColorF(0xFF9500) },
            { 0.4f, D2D1::ColorF(0xFFCC00) },
            { 0.6f, D2D1::ColorF(0x34C759) },
            { 0.8f, D2D1::ColorF(0x007AFF) },
            { 1.0f, D2D1::ColorF(0xAF52DE) }
        };
        ComPtr<ID2D1GradientStopCollection> stopCol;
        if (SUCCEEDED(dc->CreateGradientStopCollection(stops, 6, &stopCol))) {
            ComPtr<ID2D1LinearGradientBrush> rainbowBrush;
            D2D1_LINEAR_GRADIENT_BRUSH_PROPERTIES gradProps = D2D1::LinearGradientBrushProperties(
                D2D1::Point2F(cx - 10.f, cy - 10.f),
                D2D1::Point2F(cx + 10.f, cy + 10.f));
            if (SUCCEEDED(dc->CreateLinearGradientBrush(gradProps, stopCol.Get(), &rainbowBrush))) {
                dc->FillEllipse(rainbowCircle, rainbowBrush.Get());
            }
        }
        dc->DrawEllipse(rainbowCircle, m_labelFgBrush.Get(), isHovered ? 2.5f : 1.5f);
        DrawFluentGlyph(dc, L'\uE790', D2D1::RectF(cx - 7.f, cy - 7.f, cx + 7.f, cy + 7.f), m_labelFgBrush.Get(), g_dwrite.IconFont14());
    } else if (m_toolbarPopup == ToolbarPopup::CustomColorPicker) {
        POINT hp{ m_hoverPt.x - m_monitorRect.left, m_hoverPt.y - m_monitorRect.top };

        DrawFluentGlyph(dc, L'\uE790', D2D1::RectF(left + 14.f, top + 10.f, left + 30.f, top + 26.f), m_toolbarActiveBrush.Get(), g_dwrite.IconFont14());
        dc->DrawTextW(L"Color Picker", 12, g_dwrite.UIFontBold(),
                      D2D1::RectF(left + 36.f, top + 9.f, left + 180.f, top + 28.f), m_labelFgBrush.Get());

        D2D1_RECT_F closeBtnRect = D2D1::RectF(left + width - 30.f, top + 7.f, left + width - 10.f, top + 27.f);
        bool closeHover = hp.x >= closeBtnRect.left && hp.x <= closeBtnRect.right && hp.y >= closeBtnRect.top && hp.y <= closeBtnRect.bottom;
        if (closeHover) {
            dc->FillRoundedRectangle(D2D1::RoundedRect(closeBtnRect, 4.f, 4.f), m_toolbarHoverBrush.Get());
        }
        DrawFluentGlyph(dc, L'\uE711', closeBtnRect, m_toolbarIconBrush.Get(), g_dwrite.IconFont14());

        D2D1_RECT_F specRect = D2D1::RectF(left + 14.f, top + 36.f, left + 14.f + 252.f, top + 36.f + 160.f);
        D2D1_GRADIENT_STOP hStops[2] = {
            { 0.0f, D2D1::ColorF(1.f, 1.f, 1.f, 1.f) },
            { 1.0f, HsvToRgb(m_customPickerHue, 1.0f, 1.0f) }
        };
        ComPtr<ID2D1GradientStopCollection> hStopCol;
        if (SUCCEEDED(dc->CreateGradientStopCollection(hStops, 2, &hStopCol))) {
            ComPtr<ID2D1LinearGradientBrush> hBrush;
            auto hProps = D2D1::LinearGradientBrushProperties(
                D2D1::Point2F(specRect.left, specRect.top),
                D2D1::Point2F(specRect.right, specRect.top));
            if (SUCCEEDED(dc->CreateLinearGradientBrush(hProps, hStopCol.Get(), &hBrush))) {
                dc->FillRoundedRectangle(D2D1::RoundedRect(specRect, 6.f, 6.f), hBrush.Get());
            }
        }
        D2D1_GRADIENT_STOP vStops[2] = {
            { 0.0f, D2D1::ColorF(0.f, 0.f, 0.f, 0.0f) },
            { 1.0f, D2D1::ColorF(0.f, 0.f, 0.f, 1.0f) }
        };
        ComPtr<ID2D1GradientStopCollection> vStopCol;
        if (SUCCEEDED(dc->CreateGradientStopCollection(vStops, 2, &vStopCol))) {
            ComPtr<ID2D1LinearGradientBrush> vBrush;
            auto vProps = D2D1::LinearGradientBrushProperties(
                D2D1::Point2F(specRect.left, specRect.top),
                D2D1::Point2F(specRect.left, specRect.bottom));
            if (SUCCEEDED(dc->CreateLinearGradientBrush(vProps, vStopCol.Get(), &vBrush))) {
                dc->FillRoundedRectangle(D2D1::RoundedRect(specRect, 6.f, 6.f), vBrush.Get());
            }
        }
        dc->DrawRoundedRectangle(D2D1::RoundedRect(specRect, 6.f, 6.f), m_toolbarBorderBrush.Get(), 1.0f);

        float crossX = specRect.left + m_customPickerSat * 252.f;
        float crossY = specRect.top + (1.0f - m_customPickerVal) * 160.f;
        ComPtr<ID2D1SolidColorBrush> shadowBrush;
        dc->CreateSolidColorBrush(D2D1::ColorF(0.f, 0.f, 0.f, 0.75f), &shadowBrush);
        dc->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(crossX, crossY), 7.f, 7.f), shadowBrush.Get(), 3.5f);
        dc->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(crossX, crossY), 7.f, 7.f), m_labelFgBrush.Get(), 2.0f);

        D2D1_RECT_F hueRect = D2D1::RectF(left + 14.f + 252.f + 8.f, top + 36.f, left + width - 14.f, top + 36.f + 160.f);
        D2D1_GRADIENT_STOP hueStops[7] = {
            { 0.000f, D2D1::ColorF(1.f, 0.f, 0.f, 1.f) },
            { 0.167f, D2D1::ColorF(1.f, 1.f, 0.f, 1.f) },
            { 0.333f, D2D1::ColorF(0.f, 1.f, 0.f, 1.f) },
            { 0.500f, D2D1::ColorF(0.f, 1.f, 1.f, 1.f) },
            { 0.667f, D2D1::ColorF(0.f, 0.f, 1.f, 1.f) },
            { 0.833f, D2D1::ColorF(1.f, 0.f, 1.f, 1.f) },
            { 1.000f, D2D1::ColorF(1.f, 0.f, 0.f, 1.f) }
        };
        ComPtr<ID2D1GradientStopCollection> hueStopCol;
        if (SUCCEEDED(dc->CreateGradientStopCollection(hueStops, 7, &hueStopCol))) {
            ComPtr<ID2D1LinearGradientBrush> hueBrush;
            auto hueProps = D2D1::LinearGradientBrushProperties(
                D2D1::Point2F(hueRect.left, hueRect.top),
                D2D1::Point2F(hueRect.left, hueRect.bottom));
            if (SUCCEEDED(dc->CreateLinearGradientBrush(hueProps, hueStopCol.Get(), &hueBrush))) {
                dc->FillRoundedRectangle(D2D1::RoundedRect(hueRect, 4.f, 4.f), hueBrush.Get());
            }
        }
        dc->DrawRoundedRectangle(D2D1::RoundedRect(hueRect, 4.f, 4.f), m_toolbarBorderBrush.Get(), 1.0f);
        float thumbY = hueRect.top + (m_customPickerHue / 360.0f) * 160.f;
        thumbY = std::clamp(thumbY, hueRect.top + 2.f, hueRect.bottom - 2.f);
        D2D1_ROUNDED_RECT thumbRect = D2D1::RoundedRect(D2D1::RectF(hueRect.left - 1.f, thumbY - 2.5f, hueRect.right + 1.f, thumbY + 2.5f), 2.f, 2.f);
        dc->DrawRoundedRectangle(thumbRect, shadowBrush.Get(), 3.5f);
        dc->DrawRoundedRectangle(thumbRect, m_labelFgBrush.Get(), 2.0f);

        D2D1_ROUNDED_RECT prevBox = D2D1::RoundedRect(D2D1::RectF(left + 14.f, top + 204.f, left + 58.f, top + 232.f), 5.f, 5.f);
        m_swatchBrush->SetColor(m_customPickerColor);
        dc->FillRoundedRectangle(prevBox, m_swatchBrush.Get());
        dc->DrawRoundedRectangle(prevBox, m_toolbarBorderBrush.Get(), 1.0f);

        dc->DrawTextW(L"HEX", 3, g_dwrite.UIFontSmall(), D2D1::RectF(left + 68.f, top + 209.f, left + 100.f, top + 227.f), m_toolbarIconBrush.Get());

        WCHAR hexStr[16];
        int rByte = static_cast<int>(std::round(m_customPickerColor.r * 255.f));
        int gByte = static_cast<int>(std::round(m_customPickerColor.g * 255.f));
        int bByte = static_cast<int>(std::round(m_customPickerColor.b * 255.f));
        swprintf_s(hexStr, L"#%02X%02X%02X", rByte, gByte, bByte);

        const std::wstring displayHex = m_hexInputActive ? m_hexInputText : hexStr;

        D2D1_ROUNDED_RECT hexBox = D2D1::RoundedRect(D2D1::RectF(left + 102.f, top + 205.f, left + width - 14.f, top + 231.f), 4.f, 4.f);
        dc->FillRoundedRectangle(hexBox, m_toolbarBgBrush.Get());
        dc->DrawRoundedRectangle(hexBox, m_hexInputActive ? m_toolbarActiveBrush.Get() : m_toolbarBorderBrush.Get(), m_hexInputActive ? 1.5f : 1.0f);

        D2D1_RECT_F hexTextRect = D2D1::RectF(left + 112.f, top + 209.f, left + width - 18.f, top + 229.f);
        dc->DrawTextW(displayHex.c_str(), static_cast<UINT32>(displayHex.size()), g_dwrite.UIFontSmall(),
                      hexTextRect, m_labelFgBrush.Get());

        if (m_hexInputActive) {
            float textWidth = 0.0f;
            ComPtr<IDWriteTextLayout> hexLayout;
            if (SUCCEEDED(g_dwrite.Factory()->CreateTextLayout(
                    displayHex.c_str(), static_cast<UINT32>(displayHex.size()),
                    g_dwrite.UIFontSmall(), 1000.f, 100.f, &hexLayout))) {
                DWRITE_TEXT_METRICS tm{};
                if (SUCCEEDED(hexLayout->GetMetrics(&tm))) {
                    textWidth = tm.width;
                }
            }
            ULONGLONG now = GetTickCount64();
            bool showCaret = ((now / 500) % 2) == 0;
            if (showCaret) {
                float caretX = hexTextRect.left + textWidth + 1.0f;
                dc->DrawLine(D2D1::Point2F(caretX, top + 210.f),
                             D2D1::Point2F(caretX, top + 226.f),
                             m_labelFgBrush.Get(), 1.5f);
            }
        }

        WCHAR rgbStr[64];
        swprintf_s(rgbStr, L"RGB:    R: %d     G: %d     B: %d", rByte, gByte, bByte);
        dc->DrawTextW(rgbStr, static_cast<UINT32>(wcslen(rgbStr)), g_dwrite.UIFontSmall(),
                      D2D1::RectF(left + 14.f, top + 238.f, left + width - 14.f, top + 254.f), m_toolbarIconBrush.Get());

        int hslH = 0, hslS = 0, hslL = 0;
        RgbToHsl(m_customPickerColor, hslH, hslS, hslL);
        WCHAR hslStr[64];
        swprintf_s(hslStr, L"HSL:    H: %d\u00B0    S: %d%%    L: %d%%", hslH, hslS, hslL);
        dc->DrawTextW(hslStr, static_cast<UINT32>(wcslen(hslStr)), g_dwrite.UIFontSmall(),
                      D2D1::RectF(left + 14.f, top + 256.f, left + width - 14.f, top + 272.f), m_toolbarIconBrush.Get());

        dc->DrawTextW(L"Quick Colors", 12, g_dwrite.UIFontSmall(),
                      D2D1::RectF(left + 14.f, top + 276.f, left + 140.f, top + 292.f), m_toolbarIconBrush.Get());
        D2D1_ROUNDED_RECT qCard = D2D1::RoundedRect(D2D1::RectF(left + 14.f, top + 292.f, left + width - 14.f, top + 368.f), 6.f, 6.f);
        dc->FillRoundedRectangle(qCard, m_toolbarBgBrush.Get());
        dc->DrawRoundedRectangle(qCard, m_toolbarBorderBrush.Get(), 1.0f);

        float cellW = 282.f / 10.f;
        float cellH = 76.f / 5.f;
        for (int i = 0; i < 50; ++i) {
            int c = i % 10;
            int r = i / 10;
            float sx = left + 14.f + c * cellW + cellW * 0.5f;
            float sy = top + 292.f + r * cellH + cellH * 0.5f;
            D2D1_RECT_F swatchRect = D2D1::RectF(sx - 11.f, sy - 5.5f, sx + 11.f, sy + 5.5f);
            bool isSwatchHover = hp.x >= swatchRect.left && hp.x <= swatchRect.right && hp.y >= swatchRect.top && hp.y <= swatchRect.bottom;
            m_swatchBrush->SetColor(kQuickColors[i]);
            dc->FillRoundedRectangle(D2D1::RoundedRect(swatchRect, 3.f, 3.f), m_swatchBrush.Get());
            if (isSwatchHover) {
                dc->DrawRoundedRectangle(D2D1::RoundedRect(swatchRect, 3.f, 3.f), m_labelFgBrush.Get(), 1.5f);
            }
        }

        D2D1_RECT_F cancelBtn = D2D1::RectF(left + 14.f, top + 374.f, left + 145.f, top + 400.f);
        bool cancelHover = hp.x >= cancelBtn.left && hp.x <= cancelBtn.right && hp.y >= cancelBtn.top && hp.y <= cancelBtn.bottom;
        dc->FillRoundedRectangle(D2D1::RoundedRect(cancelBtn, 5.f, 5.f), cancelHover ? m_toolbarHoverBrush.Get() : m_toolbarBgBrush.Get());
        dc->DrawRoundedRectangle(D2D1::RoundedRect(cancelBtn, 5.f, 5.f), m_toolbarBorderBrush.Get(), 1.0f);
        dc->DrawTextW(L"Cancel", 6, g_dwrite.UIFontSmallCenter(),
                      cancelBtn, m_labelFgBrush.Get());

        D2D1_RECT_F applyBtn = D2D1::RectF(left + 155.f, top + 374.f, left + width - 14.f, top + 400.f);
        bool applyHover = hp.x >= applyBtn.left && hp.x <= applyBtn.right && hp.y >= applyBtn.top && hp.y <= applyBtn.bottom;
        dc->FillRoundedRectangle(D2D1::RoundedRect(applyBtn, 5.f, 5.f), m_toolbarActiveBrush.Get());
        if (applyHover) {
            dc->DrawRoundedRectangle(D2D1::RoundedRect(applyBtn, 5.f, 5.f), m_labelFgBrush.Get(), 1.0f);
        }
        dc->DrawTextW(L"Apply Color", 11, g_dwrite.UIFontSmallCenter(),
                      applyBtn, m_labelFgBrush.Get());
    }
}

// ── Annotation rendering ──────────────────────────────────────────────────────

void OverlayWindow::RenderAnnotations(ID2D1DeviceContext* dc) noexcept {
    for (const auto& ann : m_annotations) {
        switch (ann.type) {
        case AnnotationType::Rect: {
            const auto& ra = std::get<RectAnnotation>(ann.data);
            if (ra.fillColor.a > 0.f) {
                ComPtr<ID2D1SolidColorBrush> fill;
                dc->CreateSolidColorBrush(ToD2D(ra.fillColor), &fill);
                dc->FillRectangle(ra.rect, fill.Get());
            }
            ComPtr<ID2D1SolidColorBrush> b;
            dc->CreateSolidColorBrush(ToD2D(ra.strokeColor), &b);
            dc->DrawRectangle(ra.rect, b.Get(), ra.strokeWidth);
            break;
        }
        case AnnotationType::Ellipse: {
            const auto& ea = std::get<EllipseAnnotation>(ann.data);
            ComPtr<ID2D1SolidColorBrush> b;
            dc->CreateSolidColorBrush(ToD2D(ea.strokeColor), &b);
            dc->DrawEllipse(ea.ellipse, b.Get(), ea.strokeWidth);
            break;
        }
        case AnnotationType::Arrow:
            RenderArrowAnnotation(dc, std::get<ArrowAnnotation>(ann.data));
            break;
        case AnnotationType::Text: {
            const auto& ta = std::get<TextAnnotation>(ann.data);
            ComPtr<ID2D1SolidColorBrush> b;
            dc->CreateSolidColorBrush(ToD2D(ta.color), &b);
            ComPtr<IDWriteTextFormat> format;
            if (FAILED(g_dwrite.Factory()->CreateTextFormat(ta.fontFamily.c_str(), nullptr,
                    DWRITE_FONT_WEIGHT_BOLD, DWRITE_FONT_STYLE_NORMAL,
                    DWRITE_FONT_STRETCH_NORMAL, ta.fontSize, L"en-us", &format)))
                break;
            D2D1_MATRIX_3X2_F oldTrans;
            dc->GetTransform(&oldTrans);
            dc->SetTransform(
                D2D1::Matrix3x2F::Scale(ta.scaleX, ta.scaleY, D2D1::Point2F(0, 0)) *
                D2D1::Matrix3x2F::Translation(ta.origin.x, ta.origin.y) *
                oldTrans
            );
            dc->DrawTextW(ta.text.c_str(), static_cast<UINT32>(ta.text.size()),
                          format.Get(),
                          D2D1::RectF(4.f, 2.f, 4.f + 4000.f, 2.f + 4000.f),
                          b.Get());
            dc->SetTransform(oldTrans);
            break;
        }
        case AnnotationType::Blur:
            RenderBlurAnnotation(dc, std::get<BlurAnnotation>(ann.data));
            break;
        case AnnotationType::FreeDraw:
            RenderFreeDrawAnnotation(dc, std::get<FreeDrawAnnotation>(ann.data));
            break;
        case AnnotationType::Counter:
            RenderCounterAnnotation(dc, std::get<CounterAnnotation>(ann.data));
            break;
        }
    }
}

void OverlayWindow::RenderFreeDrawAnnotation(ID2D1DeviceContext* dc, const FreeDrawAnnotation& fd) noexcept {
    if (fd.points.size() < 2) return;
    ComPtr<ID2D1SolidColorBrush> b;
    dc->CreateSolidColorBrush(ToD2D(fd.color), &b);

    if (fd.isHighlighter) {
        // Render as a single path geometry to prevent overlapping dark joint artifacts with translucent alpha
        ComPtr<ID2D1PathGeometry> path;
        ComPtr<ID2D1Factory> factory;
        dc->GetFactory(&factory);
        if (SUCCEEDED(factory->CreatePathGeometry(&path))) {
            ComPtr<ID2D1GeometrySink> sink;
            if (SUCCEEDED(path->Open(&sink))) {
                sink->BeginFigure(fd.points[0], D2D1_FIGURE_BEGIN_HOLLOW);
                for (size_t i = 1; i < fd.points.size(); ++i) {
                    sink->AddLine(fd.points[i]);
                }
                sink->EndFigure(D2D1_FIGURE_END_OPEN);
                sink->Close();
                dc->DrawGeometry(path.Get(), b.Get(), fd.width, m_roundStroke.Get());
            }
        }
    } else {
        for (size_t i = 1; i < fd.points.size(); ++i)
            dc->DrawLine(fd.points[i - 1], fd.points[i], b.Get(), fd.width, m_roundStroke.Get());
    }
}

void OverlayWindow::RenderArrowAnnotation(ID2D1DeviceContext* dc, const ArrowAnnotation& aa) noexcept {
    ComPtr<ID2D1SolidColorBrush> brush;
    dc->CreateSolidColorBrush(ToD2D(aa.color), &brush);

    if (!aa.arrowHead) {
        dc->DrawLine(aa.start, aa.end, brush.Get(), aa.width);
        return;
    }

    float dx = aa.end.x - aa.start.x, dy = aa.end.y - aa.start.y;
    float len = sqrtf(dx * dx + dy * dy);
    if (len < 1.f) return;
    float ux = dx / len, uy = dy / len;
    float headLen  = std::max(12.f, aa.width * 4.f);
    float headHalf = headLen * 0.45f;

    D2D1_POINT_2F tip   = aa.end;
    D2D1_POINT_2F base  = { aa.end.x - ux * headLen,  aa.end.y - uy * headLen  };
    D2D1_POINT_2F left  = { base.x   + uy * headHalf, base.y   - ux * headHalf };
    D2D1_POINT_2F right = { base.x   - uy * headHalf, base.y   + ux * headHalf };

    ComPtr<ID2D1PathGeometry> path;
    ComPtr<ID2D1Factory> factory;
    dc->GetFactory(&factory);
    factory->CreatePathGeometry(&path);
    ComPtr<ID2D1GeometrySink> sink;
    path->Open(&sink);
    sink->BeginFigure(tip, D2D1_FIGURE_BEGIN_FILLED);
    sink->AddLine(left);
    sink->AddLine(right);
    sink->EndFigure(D2D1_FIGURE_END_CLOSED);
    sink->Close();
    dc->FillGeometry(path.Get(), brush.Get());
    dc->DrawLine(aa.start, base, brush.Get(), aa.width);
}

void OverlayWindow::RenderBlurAnnotation(ID2D1DeviceContext* dc, const BlurAnnotation& ba) noexcept {
    if (!m_desktopBitmap) return;
    auto* context = g_d2d.D2DDevCtx();

    ComPtr<ID3D11Texture2D> srcTex;
    if (m_activeRenderTexture) {
        srcTex = m_activeRenderTexture;
    } else if (m_swapChain) {
        m_swapChain->GetBuffer(0, IID_PPV_ARGS(&srcTex));
    }

    ID2D1Bitmap* sourceBitmap = m_desktopBitmap.Get();
    if (srcTex) {
        D3D11_TEXTURE2D_DESC desc{};
        srcTex->GetDesc(&desc);
        EnsureIntermediateTexture(desc.Width, desc.Height);
        if (m_blurIntermediateTex && m_blurIntermediateBitmap) {
            dc->Flush();
            g_d2d.D3DContext()->CopyResource(m_blurIntermediateTex.Get(), srcTex.Get());
            sourceBitmap = m_blurIntermediateBitmap.Get();
        }
    }

    ComPtr<ID2D1Effect> effect;
    if (ba.style == BlurStyle::Gaussian) {
        context->CreateEffect(CLSID_D2D1GaussianBlur, &effect);
        if (effect) {
            effect->SetInput(0, sourceBitmap);
            effect->SetValue(D2D1_GAUSSIANBLUR_PROP_STANDARD_DEVIATION, ba.radius);
            effect->SetValue(D2D1_GAUSSIANBLUR_PROP_BORDER_MODE, D2D1_BORDER_MODE_HARD);
        }
    } else if (ba.style == BlurStyle::Invert) {
        context->CreateEffect(CLSID_D2D1Invert, &effect);
        if (effect) effect->SetInput(0, sourceBitmap);
    } else {
        ComPtr<ID2D1Effect> downScale;
        context->CreateEffect(CLSID_D2D1Scale, &downScale);
        if (downScale) {
            downScale->SetInput(0, sourceBitmap);
            downScale->SetValue(D2D1_SCALE_PROP_SCALE, D2D1::Vector2F(0.08f, 0.08f));
            downScale->SetValue(D2D1_SCALE_PROP_INTERPOLATION_MODE,
                                D2D1_SCALE_INTERPOLATION_MODE_NEAREST_NEIGHBOR);
            context->CreateEffect(CLSID_D2D1Scale, &effect);
            if (effect) {
                effect->SetInputEffect(0, downScale.Get());
                effect->SetValue(D2D1_SCALE_PROP_SCALE, D2D1::Vector2F(12.5f, 12.5f));
                effect->SetValue(D2D1_SCALE_PROP_INTERPOLATION_MODE,
                                 D2D1_SCALE_INTERPOLATION_MODE_NEAREST_NEIGHBOR);
            }
        }
    }
    if (!effect) return;
    dc->PushAxisAlignedClip(ba.rect, D2D1_ANTIALIAS_MODE_ALIASED);
    D2D1_POINT_2F targetOffset = D2D1::Point2F(ba.rect.left, ba.rect.top);
    dc->DrawImage(effect.Get(), &targetOffset, &ba.rect,
        ba.style == BlurStyle::Mosaic ? D2D1_INTERPOLATION_MODE_NEAREST_NEIGHBOR
                                      : D2D1_INTERPOLATION_MODE_LINEAR);
    dc->PopAxisAlignedClip();
}

void OverlayWindow::RenderCounterAnnotation(ID2D1DeviceContext* dc, const CounterAnnotation& ca) noexcept {
    const float radius = 13.0f;
    ComPtr<ID2D1SolidColorBrush> fillBrush;
    dc->CreateSolidColorBrush(ToD2D(ca.color), &fillBrush);

    float dx = ca.pointer.x - ca.center.x;
    float dy = ca.pointer.y - ca.center.y;
    float dist = sqrtf(dx * dx + dy * dy);
    if (dist > radius + 2.f) {
            float ux = dx / dist;
            float uy = dy / dist;
            float px = -uy;
            float py = ux;
            float baseWidth = radius * 0.85f;
            D2D1_POINT_2F b1 = { ca.center.x + ux * (radius * 0.3f) + px * baseWidth,
                                 ca.center.y + uy * (radius * 0.3f) + py * baseWidth };
            D2D1_POINT_2F b2 = { ca.center.x + ux * (radius * 0.3f) - px * baseWidth,
                                 ca.center.y + uy * (radius * 0.3f) - py * baseWidth };

            ComPtr<ID2D1PathGeometry> path;
            ComPtr<ID2D1Factory> factory;
            dc->GetFactory(&factory);
            if (SUCCEEDED(factory->CreatePathGeometry(&path))) {
                ComPtr<ID2D1GeometrySink> sink;
                if (SUCCEEDED(path->Open(&sink))) {
                    sink->BeginFigure(b1, D2D1_FIGURE_BEGIN_FILLED);
                    sink->AddLine(ca.pointer);
                    sink->AddLine(b2);
                    sink->EndFigure(D2D1_FIGURE_END_CLOSED);
                    sink->Close();
                    dc->FillGeometry(path.Get(), fillBrush.Get());
                }
            }
        }

    ComPtr<ID2D1SolidColorBrush> shadowBrush;
    dc->CreateSolidColorBrush(D2D1::ColorF(0.f, 0.f, 0.f, 0.35f), &shadowBrush);
    dc->FillEllipse(D2D1::Ellipse(ca.center, radius + 1.f, radius + 1.f), shadowBrush.Get());

    dc->FillEllipse(D2D1::Ellipse(ca.center, radius, radius), fillBrush.Get());

    ComPtr<ID2D1SolidColorBrush> whiteBrush;
    dc->CreateSolidColorBrush(D2D1::ColorF(1.f, 1.f, 1.f, 1.f), &whiteBrush);
    dc->DrawEllipse(D2D1::Ellipse(ca.center, radius, radius), whiteBrush.Get(), 2.0f);

    std::wstring text = std::to_wstring(ca.number);
    ComPtr<IDWriteTextFormat> format;
    if (SUCCEEDED(g_dwrite.Factory()->CreateTextFormat(
            L"Segoe UI", nullptr,
            DWRITE_FONT_WEIGHT_BOLD, DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL, 13.0f, L"en-us", &format))) {
        format->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        format->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        D2D1_RECT_F textRect = D2D1::RectF(ca.center.x - radius, ca.center.y - radius,
                                           ca.center.x + radius, ca.center.y + radius);
        dc->DrawTextW(text.c_str(), static_cast<UINT32>(text.size()), format.Get(), textRect, whiteBrush.Get());
    }
}

int OverlayWindow::GetNextCounterNumber() const noexcept {
    std::unordered_set<int> used;
    for (const auto& a : m_annotations) {
        if (a.type == AnnotationType::Counter) {
            used.insert(std::get<CounterAnnotation>(a.data).number);
        }
    }
    int next = 1;
    while (used.find(next) != used.end()) {
        next++;
    }
    return next;
}

void OverlayWindow::RenderPreview(ID2D1DeviceContext* dc) noexcept {
    POINT local{ m_hoverPt.x - m_monitorRect.left, m_hoverPt.y - m_monitorRect.top };
    D2D1_POINT_2F cpt = { static_cast<float>(local.x), static_cast<float>(local.y) };

    if (m_toolMode == ToolMode::Select) {
        DrawSelectedAnnotationOutline(dc);
    }

    if (m_freeDrawState.active && m_freeDrawState.points.size() >= 1) {
        FreeDrawAnnotation fd{};
        fd.points = m_freeDrawState.points;
        if (m_toolMode == ToolMode::Highlighter) {
            fd.color = m_strokeColor;
            fd.color.a = 0.38f;
            fd.width = std::max(16.0f, m_strokeWidth * 3.5f);
            fd.isHighlighter = true;
        } else {
            fd.color = m_strokeColor;
            fd.width = m_strokeWidth;
            fd.isHighlighter = false;
        }
        RenderFreeDrawAnnotation(dc, fd);
        return;
    }

    if (m_textState.editing) {
        TextTool_Draw(dc, m_textState, g_dwrite.Factory(),
                      m_strokeBrush.Get(),
                      m_selBorderBrush.Get());
    }

    if (m_blurState.active) {
        D2D1_RECT_F r = D2D1::RectF(
            std::min(m_blurState.anchor.x, cpt.x), std::min(m_blurState.anchor.y, cpt.y),
            std::max(m_blurState.anchor.x, cpt.x), std::max(m_blurState.anchor.y, cpt.y)
        );
        if (r.right - r.left >= 2.0f && r.bottom - r.top >= 2.0f) {
            BlurAnnotation ba{ r, m_blurRadius, m_blurStyle };
            RenderBlurAnnotation(dc, ba);
            dc->DrawRectangle(r, m_previewBrush.Get(), 1.5f, m_dashStroke.Get());
        }
    }

    if (!m_dragging) return;
    switch (m_toolMode) {
    case ToolMode::Rect:
    case ToolMode::FilledRect:
        if (m_toolMode == ToolMode::FilledRect && m_rectState.active) {
            D2D1_RECT_F fillRect = D2D1::RectF(
                std::min(m_rectState.anchor.x, cpt.x), std::min(m_rectState.anchor.y, cpt.y),
                std::max(m_rectState.anchor.x, cpt.x), std::max(m_rectState.anchor.y, cpt.y));
            D2D1_COLOR_F fillColor = ToD2D(m_strokeColor);
            fillColor.a = 1.0f;
            m_strokeBrush->SetColor(fillColor);
            dc->FillRectangle(fillRect, m_strokeBrush.Get());
            m_strokeBrush->SetColor(ToD2D(m_strokeColor));
        }
        RectTool_Draw(dc, m_rectState, cpt, m_strokeBrush.Get(), m_shapeStrokeWidth);
        break;
    case ToolMode::Ellipse: {
        if (!m_rectState.active) break;
        float l = std::min(m_rectState.anchor.x, cpt.x);
        float t = std::min(m_rectState.anchor.y, cpt.y);
        float r = std::max(m_rectState.anchor.x, cpt.x);
        float b = std::max(m_rectState.anchor.y, cpt.y);
        dc->DrawEllipse(D2D1::Ellipse(D2D1::Point2F((l + r) * 0.5f, (t + b) * 0.5f),
                                      (r - l) * 0.5f, (b - t) * 0.5f),
                        m_strokeBrush.Get(), m_shapeStrokeWidth);
        break;
    }
    case ToolMode::Line:
        if (m_arrowState.active)
            dc->DrawLine(m_arrowState.start, cpt, m_strokeBrush.Get(), m_shapeStrokeWidth);
        break;
    case ToolMode::Arrow:
        ArrowTool_Draw(dc, m_arrowState, cpt, m_strokeBrush.Get(), m_shapeStrokeWidth);
        break;
    case ToolMode::Counter:
        if (m_counterState.active) {
            CounterAnnotation ca{};
            ca.center = m_counterState.start;
            ca.number = m_counterState.step;
            ca.color = m_strokeColor;
            float dx = cpt.x - m_counterState.start.x;
            float dy = cpt.y - m_counterState.start.y;
            float dist = sqrtf(dx * dx + dy * dy);
            if (dist > 20.f) {
                ca.hasPointer = true;
                ca.pointer = cpt;
            } else {
                ca.hasPointer = false;
                ca.pointer = ca.center;
            }
            RenderCounterAnnotation(dc, ca);
        }
        break;
    default: break;
    }
}

// ── Window procedure ──────────────────────────────────────────────────────────

LRESULT CALLBACK OverlayWindow::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) noexcept {
    OverlayWindow* self = nullptr;
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = static_cast<OverlayWindow*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        self->m_hwnd = hwnd;
    } else {
        self = reinterpret_cast<OverlayWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }
    if (self) return self->HandleMessage(msg, wParam, lParam);
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

LRESULT OverlayWindow::HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam) noexcept {
    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT ps{};
        BeginPaint(m_hwnd, &ps);
        if (m_visible) RenderFrame();
        EndPaint(m_hwnd, &ps);
        return 0;
    }
    case WM_LBUTTONDOWN:
        OnLButtonDown({ GET_X_LPARAM(lParam) + m_monitorRect.left,
                        GET_Y_LPARAM(lParam) + m_monitorRect.top });
        return 0;
    case WM_MOUSEMOVE:
        OnMouseMove({ GET_X_LPARAM(lParam) + m_monitorRect.left,
                      GET_Y_LPARAM(lParam) + m_monitorRect.top });
        return 0;
    case WM_LBUTTONUP:
        OnLButtonUp({ GET_X_LPARAM(lParam) + m_monitorRect.left,
                      GET_Y_LPARAM(lParam) + m_monitorRect.top });
        return 0;
    case WM_TIMER:
        if (wParam == 1001) {
            if (m_textState.editing || (m_toolbarPopup == ToolbarPopup::CustomColorPicker && m_hexInputActive)) {
                RenderFrame();
            } else {
                KillTimer(m_hwnd, 1001);
            }
            return 0;
        }
        break;
    case WM_SYSKEYDOWN:
    case WM_KEYDOWN:
        OnKeyDown(wParam);
        return 0;
    case WM_CHAR:
        OnChar(wParam);
        return 0;
    case WM_USER + 105: {
        auto* res = reinterpret_cast<OcrInlineResult*>(lParam);
        if (res) {
            OnOcrComplete(*res);
            delete res;
        }
        return 0;
    }
    case WM_USER + 106: {
        auto* qrMsg = reinterpret_cast<QrScanMsg*>(lParam);
        if (qrMsg) {
            OnQrComplete(qrMsg->seq, qrMsg->result);
            delete qrMsg;
        }
        return 0;
    }
    case WM_MOUSEACTIVATE:
        return MA_ACTIVATE;
    case WM_LBUTTONDBLCLK: {
        POINT pt{ GET_X_LPARAM(lParam) + m_monitorRect.left,
                  GET_Y_LPARAM(lParam) + m_monitorRect.top };
        if (m_ocrActive) {
            RECT sel = m_selection.NormalizedRect();
            POINT local{ pt.x - m_monitorRect.left, pt.y - m_monitorRect.top };
            bool inside = pt.x >= sel.left && pt.x < sel.right && pt.y >= sel.top && pt.y < sel.bottom;
            if (inside) {
                m_isOcrSelecting = false;
                m_selectedOcrCharIndices.clear();
                float sx = static_cast<float>(sel.left - m_monitorRect.left);
                float sy = static_cast<float>(sel.top  - m_monitorRect.top);
                for (const auto& w : m_ocrResult.words) {
                    D2D1_RECT_F wr = D2D1::RectF(sx + w.bounds.left - 3.f, sy + w.bounds.top - 3.f,
                                                 sx + w.bounds.right + 3.f, sy + w.bounds.bottom + 3.f);
                    if (local.x >= wr.left && local.x <= wr.right && local.y >= wr.top && local.y <= wr.bottom) {
                        for (size_t i = 0; i < m_ocrResult.chars.size(); ++i) {
                            const auto& c = m_ocrResult.chars[i];
                            D2D1_RECT_F cr = D2D1::RectF(sx + c.bounds.left, sy + c.bounds.top,
                                                         sx + c.bounds.right, sy + c.bounds.bottom);
                            if (cr.left >= wr.left && cr.right <= wr.right && cr.top >= wr.top && cr.bottom <= wr.bottom) {
                                m_selectedOcrCharIndices.push_back(static_cast<int>(i));
                            }
                        }
                        Clipboard::WriteText(w.text.c_str(), m_hwnd);
                        float qX = sx + (w.bounds.left + w.bounds.right) * 0.5f - 89.0f;
                        float qY = sy + w.bounds.bottom + 10.0f;
                        float monW = static_cast<float>(m_monitorRect.right - m_monitorRect.left);
                        float monH = static_cast<float>(m_monitorRect.bottom - m_monitorRect.top);
                        if (qY + 45.0f > monH || (qY + 45.0f > sy + static_cast<float>(sel.bottom - sel.top) && sy + w.bounds.top - 45.0f > 40.0f)) {
                            qY = sy + w.bounds.top - 42.0f;
                        }
                        qX = std::clamp(qX, 10.0f, monW - 190.0f);
                        m_ocrQuickActionPos = { qX, qY };
                        m_ocrQuickActionVisible = true;
                        RenderFrame();
                        return 0;
                    }
                }
            }
            return 0;
        }

        if (IsEditorOpen()) {
            D2D1_POINT_2F cpt = { static_cast<float>(pt.x - m_monitorRect.left),
                                  static_cast<float>(pt.y - m_monitorRect.top) };

            // Check if double-clicked an existing text annotation
            int hitIndex = -1;
            if (m_selectedAnnotationIndex >= 0 &&
                m_selectedAnnotationIndex < static_cast<int>(m_annotations.size()) &&
                m_annotations[m_selectedAnnotationIndex].type == AnnotationType::Text &&
                AnnotationHit(m_annotations[m_selectedAnnotationIndex], cpt)) {
                hitIndex = m_selectedAnnotationIndex;
            } else {
                for (int i = static_cast<int>(m_annotations.size()) - 1; i >= 0; --i) {
                    if (m_annotations[i].type == AnnotationType::Text && AnnotationHit(m_annotations[i], cpt)) {
                        hitIndex = i;
                        break;
                    }
                }
            }

            if (hitIndex >= 0) {
                PushUndoState();
                const auto& ta = std::get<TextAnnotation>(m_annotations[hitIndex].data);
                TextTool_Start(m_textState, ta.origin, ta.color, ta.fontSize, ta.fontFamily, ta.text, ta.scaleX, ta.scaleY);
                m_fontSize = ta.fontSize;
                m_strokeColor = ta.color;
                m_fontFamily = ta.fontFamily;
                m_annotations.erase(m_annotations.begin() + hitIndex);
                m_selectedAnnotationIndex = -1;
                m_toolMode = ToolMode::Text;
                SetForegroundWindow(m_hwnd);
                SetActiveWindow(m_hwnd);
                SetFocus(m_hwnd);
                SetTimer(m_hwnd, 1001, 500, nullptr);
                TextTool_OnClick(m_textState, cpt, g_dwrite.Factory());
                RenderFrame();
                return 0;
            }

            OnLButtonDown(pt);
            return 0;
        }
        return 0;
    }
    case WM_RBUTTONDOWN: {
        if (m_toolbarPopup != ToolbarPopup::None) {
            m_toolbarPopup = ToolbarPopup::None;
            m_fontDropdownOpen = false;
            m_hexInputActive = false;
            RenderFrame();
            return 0;
        }

        if (m_ocrActive) {
            POINT pt{ GET_X_LPARAM(lParam) + m_monitorRect.left,
                      GET_Y_LPARAM(lParam) + m_monitorRect.top };
            RECT sel = m_selection.NormalizedRect();
            POINT local{ pt.x - m_monitorRect.left, pt.y - m_monitorRect.top };
            bool inside = pt.x >= sel.left && pt.x < sel.right && pt.y >= sel.top && pt.y < sel.bottom;
            if (inside && !m_ocrResult.fullText.empty()) {
                m_ocrQuickActionPos = { static_cast<float>(local.x), static_cast<float>(local.y) };
                m_ocrQuickActionVisible = true;
                RenderFrame();
                return 0;
            }
        }
        return 0;
    }
    case WM_SETCURSOR: {
        POINT pt{};
        GetCursorPos(&pt);
        POINT local{ pt.x - m_monitorRect.left, pt.y - m_monitorRect.top };
        UpdateCursor(local);
        return TRUE;
    }
    case WM_MOUSEWHEEL: {
        short delta = GET_WHEEL_DELTA_WPARAM(wParam);
        if (m_toolbarPopup == ToolbarPopup::Text && m_fontDropdownOpen) {
            int lines = delta / WHEEL_DELTA;
            int maxScroll = std::max(0, static_cast<int>(m_systemFonts.size()) - 8);
            m_fontScrollIndex = std::clamp(m_fontScrollIndex - lines * 3, 0, maxScroll);
            RenderFrame();
            return 0;
        }
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    }
    return DefWindowProcW(m_hwnd, msg, wParam, lParam);
}

void OverlayWindow::Shutdown() noexcept {
    Hide();
    m_dashStroke.Reset();
    m_roundStroke.Reset();
    m_previewBrush.Reset();
    m_highlightBrush.Reset();
    m_strokeBrush.Reset();
    m_separatorBrush.Reset();
    m_toolbarHoverBrush.Reset();
    m_toolbarActiveBrush.Reset();
    m_toolbarCopyBrush.Reset();
    m_toolbarCloseHoverBrush.Reset();
    m_toolbarCloseBrush.Reset();
    m_toolbarIconBrush.Reset();
    m_toolbarShadowBrush.Reset();
    m_toolbarBorderBrush.Reset();
    m_toolbarBgBrush.Reset();
    m_guideBrush.Reset();
    m_labelFgBrush.Reset();
    m_labelBgBrush.Reset();
    m_handleBrush.Reset();
    m_selBorderBrush.Reset();
    m_dimBrush.Reset();
    m_desktopBitmap.Reset();
    m_blurIntermediateBitmap.Reset();
    m_blurIntermediateTex.Reset();
    m_activeRenderTexture = nullptr;
    m_d2dTarget.Reset();
    m_swapChain.Reset();
    m_dcompVisual.Reset();
    m_dcompTarget.Reset();
    m_dcompDevice.Reset();
    if (m_hwnd) {
        DestroyWindow(m_hwnd);
        m_hwnd = nullptr;
    }
    m_initialized = false;
}

} // namespace snippy
