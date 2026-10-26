#include "settings_window.h"
#include "../render/d2d_context.h"
#include "../render/dwrite_context.h"
#include "../theme/theme_manager.h"
#include "../update/update_checker.h"
#include "../resources/resource.h"
#include <dwmapi.h>
#include <shellapi.h>
#include <windowsx.h>
#include <algorithm>
#include <cmath>
#include <commdlg.h>

#ifndef DWMWA_WINDOW_CORNER_PREFERENCE
#define DWMWA_WINDOW_CORNER_PREFERENCE 33
#define DWMWCP_ROUND 2
#endif

#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif

namespace snippy {

SettingsWindow g_settingsWindow;

static constexpr WCHAR kSettingsClassName[] = L"SnippySettingsWnd";
static constexpr UINT_PTR kTimerSaveFeedback = 101;
static constexpr UINT_PTR kTimerCaretBlink    = 102;

static UINT GetCaretBlinkInterval() noexcept {
    UINT u = GetCaretBlinkTime();
    if (u == 0 || u == INFINITE) return 500;
    return u;
}

enum ControlId {
    ID_NONE = 0,
    ID_CLOSE_BTN,
    ID_THEME_COMBO,
    ID_ACCENT_TOGGLE,
    ID_ACCENT_SWATCH_SYSTEM,
    ID_ACCENT_SWATCH_PRESET_START,
    ID_ACCENT_SWATCH_CUSTOM = ID_ACCENT_SWATCH_PRESET_START + 8,
    ID_HOTKEY_RECORD_BOX,
    ID_HOTKEY_RECORD_BTN,
    ID_HOTKEY_RESET,
    ID_DEBUG_LOGGER_CARD,
    ID_SLIDER_DIMMING,
    ID_SLIDER_BORDER,
    ID_TOOLBAR_POS_COMBO,
    ID_TOOL_DRAW,
    ID_TOOL_SHAPES,
    ID_TOOL_BLUR,
    ID_TOOL_TEXT,
    ID_TOOL_COUNTER,
    ID_BTN_RESET,
    ID_BTN_CANCEL,
    ID_BTN_SAVE
};

struct AccentPreset {
    const WCHAR* name;
    const WCHAR* hex;
    D2D1_COLOR_F color;
};

static const AccentPreset kAccentPresets[] = {
    { L"Blue",    L"#0078D4", D2D1::ColorF(0.000f, 0.471f, 0.831f, 1.0f) },
    { L"Purple",  L"#8764B8", D2D1::ColorF(0.529f, 0.392f, 0.722f, 1.0f) },
    { L"Pink",    L"#E3008C", D2D1::ColorF(0.890f, 0.000f, 0.549f, 1.0f) },
    { L"Red",     L"#E81123", D2D1::ColorF(0.910f, 0.067f, 0.137f, 1.0f) },
    { L"Orange",  L"#F7630C", D2D1::ColorF(0.969f, 0.388f, 0.047f, 1.0f) },
    { L"Gold",    L"#FFB900", D2D1::ColorF(1.000f, 0.725f, 0.000f, 1.0f) },
    { L"Green",   L"#107C41", D2D1::ColorF(0.063f, 0.486f, 0.255f, 1.0f) },
    { L"Cyan",    L"#00B7C3", D2D1::ColorF(0.000f, 0.718f, 0.765f, 1.0f) },
};

static constexpr float kComboToolbarPosY = 612.0f;

SettingsWindow::SettingsWindow() noexcept = default;

SettingsWindow::~SettingsWindow() noexcept {
    Shutdown();
}

bool SettingsWindow::Initialize(HINSTANCE hInstance) noexcept {
    m_hInstance = hInstance;

    WNDCLASSEXW wc{};
    wc.cbSize        = sizeof(wc);
    wc.style         = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS | CS_DROPSHADOW;
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInstance;
    wc.hIcon         = LoadIconW(hInstance, MAKEINTRESOURCEW(IDI_SNIPPY_ICON));
    wc.hCursor       = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;
    wc.lpszClassName = kSettingsClassName;

    if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        return false;
    }

    HDC screenDc = GetDC(nullptr);
    int dpi = screenDc ? GetDeviceCaps(screenDc, LOGPIXELSX) : 96;
    if (screenDc) ReleaseDC(nullptr, screenDc);
    m_dpiScale = dpi / 96.0f;

    int width  = static_cast<int>(kBaseWidth * m_dpiScale);
    int height = static_cast<int>(kBaseHeight * m_dpiScale);

    int screenW = GetSystemMetrics(SM_CXSCREEN);
    int screenH = GetSystemMetrics(SM_CYSCREEN);
    int x = (screenW - width) / 2;
    int y = (screenH - height) / 2;

    m_hwnd = CreateWindowExW(
        WS_EX_TOPMOST,
        kSettingsClassName,
        L"Snippy Settings",
        WS_POPUP,
        x, y, width, height,
        nullptr, nullptr,
        hInstance, this
    );

    if (!m_hwnd) return false;

    DWORD corner = DWMWCP_ROUND;
    DwmSetWindowAttribute(m_hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, &corner, sizeof(corner));

    CreateD2DResources();
    return true;
}

void SettingsWindow::Shutdown() noexcept {
    DiscardD2DResources();
    if (m_hwnd) {
        KillTimer(m_hwnd, kTimerSaveFeedback);
        KillTimer(m_hwnd, kTimerCaretBlink);
        DestroyWindow(m_hwnd);
        m_hwnd = nullptr;
    }
}

bool SettingsWindow::IsVisible() const noexcept {
    return m_hwnd && IsWindowVisible(m_hwnd);
}

void SettingsWindow::ApplyThemePalette(const std::wstring& themeSetting) noexcept {
    ThemePalette p = ThemeManager::GetPalette(themeSetting, m_stagedSettings.accentColor, m_stagedSettings.useAccentColor);

    if (m_bgBrush)                 m_bgBrush->SetColor(p.windowBg);
    if (m_cardBgBrush)             m_cardBgBrush->SetColor(p.cardBg);
    if (m_cardBorderBrush)         m_cardBorderBrush->SetColor(p.cardBorder);
    if (m_subtleBgBrush)           m_subtleBgBrush->SetColor(p.subtleBg);
    if (m_subtleBorderBrush)       m_subtleBorderBrush->SetColor(p.subtleBorder);
    if (m_inputBgBrush)            m_inputBgBrush->SetColor(p.inputBg);
    if (m_inputBorderBrush)        m_inputBorderBrush->SetColor(p.inputBorder);
    if (m_dropdownBgBrush)         m_dropdownBgBrush->SetColor(p.dropdownBg);
    if (m_dropdownHoverBrush)      m_dropdownHoverBrush->SetColor(p.dropdownHover);
    if (m_btnSecondaryBgBrush)     m_btnSecondaryBgBrush->SetColor(p.btnSecondaryBg);
    if (m_btnSecondaryBorderBrush) m_btnSecondaryBorderBrush->SetColor(p.btnSecondaryBorder);
    if (m_btnSecondaryHoverBrush)  m_btnSecondaryHoverBrush->SetColor(p.btnSecondaryHover);
    if (m_textPrimaryBrush)        m_textPrimaryBrush->SetColor(p.textPrimary);
    if (m_textSecondaryBrush)      m_textSecondaryBrush->SetColor(p.textSecondary);
    if (m_scrollThumbBrush)        m_scrollThumbBrush->SetColor(p.scrollThumb);
    if (m_sliderTrackBgBrush)      m_sliderTrackBgBrush->SetColor(p.sliderTrack);
    if (m_accentBrush)             m_accentBrush->SetColor(p.accent);
    if (m_accentHoverBrush)        m_accentHoverBrush->SetColor(p.accentHover);

    if (m_hwnd) {
        BOOL darkMode = !p.isLight;
        DwmSetWindowAttribute(m_hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &darkMode, sizeof(darkMode));
    }
}

void SettingsWindow::Show() noexcept {
    if (!m_hwnd) return;

    SettingsManager::LoadSettings();
    m_stagedSettings = SettingsManager::Current();
    m_isRecording = false;
    m_caretVisible = false;
    if (m_hwnd) {
        KillTimer(m_hwnd, kTimerCaretBlink);
    }
    m_statusError.clear();
    m_saveFeedback = false;
    m_scrollY = 0.0f;
    m_openDropdown = DROPDOWN_NONE;
    m_hoverDropdownItem = -1;
    m_hoverId = ID_NONE;

    ApplyThemePalette(m_stagedSettings.theme);

    int w = static_cast<int>(kBaseWidth * m_dpiScale);
    int h = static_cast<int>(kBaseHeight * m_dpiScale);
    POINT pt{};
    GetCursorPos(&pt);
    HMONITOR hMon = MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi{ sizeof(mi) };
    if (GetMonitorInfoW(hMon, &mi)) {
        int cx = mi.rcWork.left + (mi.rcWork.right - mi.rcWork.left - w) / 2;
        int cy = mi.rcWork.top + (mi.rcWork.bottom - mi.rcWork.top - h) / 2;
        SetWindowPos(m_hwnd, HWND_TOPMOST, cx, cy, w, h, SWP_SHOWWINDOW);
    } else {
        ShowWindow(m_hwnd, SW_SHOW);
    }

    SetForegroundWindow(m_hwnd);
    SetActiveWindow(m_hwnd);
    SetFocus(m_hwnd);
    UpdateWindow(m_hwnd);
    InvalidateRect(m_hwnd, nullptr, TRUE);
}

void SettingsWindow::Hide() noexcept {
    if (m_hwnd) {
        if (m_isRecording) StopRecording();
        m_openDropdown = DROPDOWN_NONE;
        ShowWindow(m_hwnd, SW_HIDE);
    }
}

// ── Direct2D Resources ────────────────────────────────────────────────────────

bool SettingsWindow::CreateD2DResources() noexcept {
    if (m_renderTarget) return true;

    RECT rc{};
    GetClientRect(m_hwnd, &rc);
    UINT32 w = static_cast<UINT32>(std::max(1L, rc.right - rc.left));
    UINT32 h = static_cast<UINT32>(std::max(1L, rc.bottom - rc.top));
    D2D1_SIZE_U size = D2D1::SizeU(w, h);

    D2D1_RENDER_TARGET_PROPERTIES rtProps = D2D1::RenderTargetProperties();
    D2D1_HWND_RENDER_TARGET_PROPERTIES hwndProps = D2D1::HwndRenderTargetProperties(
        m_hwnd, size, D2D1_PRESENT_OPTIONS_IMMEDIATELY
    );

    HRESULT hr = g_d2d.D2DFactory()->CreateHwndRenderTarget(rtProps, hwndProps, &m_renderTarget);
    if (FAILED(hr)) return false;

    // Brushes default to dark-mode values; ApplyThemePalette overwrites them on first Show().
    m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(0.09f, 0.09f, 0.10f, 1.0f), &m_bgBrush);
    m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(0.14f, 0.14f, 0.16f, 1.0f), &m_cardBgBrush);
    m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(0.21f, 0.21f, 0.23f, 1.0f), &m_cardBorderBrush);
    m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(0.09f, 0.09f, 0.10f, 1.0f), &m_subtleBgBrush);
    m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(0.21f, 0.21f, 0.23f, 1.0f), &m_subtleBorderBrush);
    m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(0.07f, 0.07f, 0.08f, 1.0f), &m_inputBgBrush);
    m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(0.21f, 0.21f, 0.23f, 1.0f), &m_inputBorderBrush);
    m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(0.12f, 0.12f, 0.13f, 1.0f), &m_dropdownBgBrush);
    m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(0.18f, 0.18f, 0.21f, 1.0f), &m_dropdownHoverBrush);
    m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(0.0f, 0.471f, 0.831f, 1.0f), &m_accentBrush);
    m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(0.08f, 0.55f, 0.91f, 1.0f), &m_accentHoverBrush);
    m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(0.18f, 0.18f, 0.18f, 1.0f), &m_btnSecondaryBgBrush);
    m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(0.24f, 0.24f, 0.26f, 1.0f), &m_btnSecondaryBorderBrush);
    m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(0.22f, 0.22f, 0.22f, 1.0f), &m_btnSecondaryHoverBrush);
    m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(1.0f, 1.0f, 1.0f, 1.0f), &m_textPrimaryBrush);
    m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(0.63f, 0.63f, 0.63f, 1.0f), &m_textSecondaryBrush);
    m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(0.204f, 0.780f, 0.349f, 1.0f), &m_greenBrush);
    m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(0.92f, 0.26f, 0.21f, 1.0f), &m_dangerBrush);
    m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(0.29f, 0.29f, 0.32f, 0.8f), &m_scrollThumbBrush);
    m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(0.23f, 0.23f, 0.25f, 1.0f), &m_sliderTrackBgBrush);
    m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(1.0f, 1.0f, 1.0f, 1.0f), &m_sliderThumbBrush);
    m_renderTarget->CreateSolidColorBrush(D2D1::ColorF(1.0f, 1.0f, 1.0f, 1.0f), &m_colorBrush);

    if (!m_roundStroke) {
        D2D1_STROKE_STYLE_PROPERTIES roundProps = D2D1::StrokeStyleProperties(
            D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND,
            D2D1_LINE_JOIN_ROUND, 10.0f, D2D1_DASH_STYLE_SOLID, 0.0f);
        g_d2d.D2DFactory()->CreateStrokeStyle(roundProps, nullptr, 0, &m_roundStroke);
    }

    ApplyThemePalette(m_stagedSettings.theme);

    IDWriteFactory3* factory = g_dwrite.Factory();
    if (!factory) return false;

    if (!m_titleFormat) {
        factory->CreateTextFormat(
            L"Segoe UI", nullptr,
            DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
            14.5f, L"en-us", &m_titleFormat
        );
        if (m_titleFormat) {
            m_titleFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        }
    }

    if (!m_cardTitleFormat) {
        factory->CreateTextFormat(
            L"Segoe UI", nullptr,
            DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
            12.5f, L"en-us", &m_cardTitleFormat
        );
        if (m_cardTitleFormat) {
            m_cardTitleFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        }
    }

    if (!m_descFormat) {
        factory->CreateTextFormat(
            L"Segoe UI", nullptr,
            DWRITE_FONT_WEIGHT_REGULAR, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
            10.0f, L"en-us", &m_descFormat
        );
    }

    if (!m_itemFormat) {
        factory->CreateTextFormat(
            L"Segoe UI", nullptr,
            DWRITE_FONT_WEIGHT_REGULAR, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
            11.0f, L"en-us", &m_itemFormat
        );
        if (m_itemFormat) {
            m_itemFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        }
    }

    if (!m_hotkeyBoldFormat) {
        factory->CreateTextFormat(
            L"Segoe UI", nullptr,
            DWRITE_FONT_WEIGHT_BOLD, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
            13.0f, L"en-us", &m_hotkeyBoldFormat
        );
        if (m_hotkeyBoldFormat) {
            m_hotkeyBoldFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        }
    }

    if (!m_btnFormat) {
        factory->CreateTextFormat(
            L"Segoe UI", nullptr,
            DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
            11.5f, L"en-us", &m_btnFormat
        );
        if (m_btnFormat) {
            m_btnFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
            m_btnFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        }
    }

    if (!m_btnPrimaryFormat) {
        factory->CreateTextFormat(
            L"Segoe UI", nullptr,
            DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
            12.0f, L"en-us", &m_btnPrimaryFormat
        );
        if (m_btnPrimaryFormat) {
            m_btnPrimaryFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
            m_btnPrimaryFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        }
    }

    return true;
}

void SettingsWindow::DiscardD2DResources() noexcept {
    m_titleFormat.Reset();
    m_cardTitleFormat.Reset();
    m_descFormat.Reset();
    m_itemFormat.Reset();
    m_hotkeyBoldFormat.Reset();
    m_btnFormat.Reset();
    m_btnPrimaryFormat.Reset();

    m_sliderThumbBrush.Reset();
    m_colorBrush.Reset();
    m_sliderTrackBgBrush.Reset();
    m_scrollThumbBrush.Reset();
    m_dangerBrush.Reset();
    m_greenBrush.Reset();
    m_textSecondaryBrush.Reset();
    m_textPrimaryBrush.Reset();
    m_btnSecondaryHoverBrush.Reset();
    m_btnSecondaryBorderBrush.Reset();
    m_btnSecondaryBgBrush.Reset();
    m_accentHoverBrush.Reset();
    m_accentBrush.Reset();
    m_dropdownHoverBrush.Reset();
    m_dropdownBgBrush.Reset();
    m_inputBorderBrush.Reset();
    m_inputBgBrush.Reset();
    m_subtleBorderBrush.Reset();
    m_subtleBgBrush.Reset();
    m_cardBorderBrush.Reset();
    m_cardBgBrush.Reset();
    m_bgBrush.Reset();
    m_roundStroke.Reset();
    m_renderTarget.Reset();
}

// ── Rendering ─────────────────────────────────────────────────────────────────

void SettingsWindow::Render() noexcept {
    if (!CreateD2DResources()) return;

    m_renderTarget->BeginDraw();
    m_renderTarget->SetTransform(D2D1::Matrix3x2F::Scale(m_dpiScale, m_dpiScale));
    m_renderTarget->Clear(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.0f));

    D2D1_RECT_F winRect = D2D1::RectF(0.5f, 0.5f, kBaseWidth - 0.5f, kBaseHeight - 0.5f);
    m_renderTarget->FillRoundedRectangle(D2D1::RoundedRect(winRect, 10.0f, 10.0f), m_bgBrush.Get());
    m_renderTarget->DrawRoundedRectangle(D2D1::RoundedRect(winRect, 10.0f, 10.0f), m_cardBorderBrush.Get(), 1.0f);

    // ── Helper Lambdas ──
    auto drawCard = [&](D2D1_RECT_F rect, const WCHAR* iconGlyph, const WCHAR* title, const WCHAR* subtitle) {
        m_renderTarget->FillRoundedRectangle(D2D1::RoundedRect(rect, 8.0f, 8.0f), m_cardBgBrush.Get());
        m_renderTarget->DrawRoundedRectangle(D2D1::RoundedRect(rect, 8.0f, 8.0f), m_cardBorderBrush.Get(), 1.0f);

        D2D1_RECT_F iRect = D2D1::RectF(rect.left + 12.0f, rect.top + 10.0f, rect.left + 30.0f, rect.top + 30.0f);
        m_renderTarget->DrawText(iconGlyph, 1, g_dwrite.IconFont15(), iRect, m_accentBrush.Get());

        D2D1_RECT_F tRect = D2D1::RectF(rect.left + 34.0f, rect.top + 8.0f, rect.right - 12.0f, rect.top + 28.0f);
        m_renderTarget->DrawText(title, static_cast<UINT32>(wcslen(title)), m_cardTitleFormat.Get(), tRect, m_textPrimaryBrush.Get());

        if (subtitle && wcslen(subtitle) > 0) {
            D2D1_RECT_F sRect = D2D1::RectF(rect.left + 12.0f, rect.top + 30.0f, rect.right - 44.0f, rect.top + 64.0f);
            m_renderTarget->DrawText(subtitle, static_cast<UINT32>(wcslen(subtitle)), m_descFormat.Get(), sRect, m_textSecondaryBrush.Get());
        }
    };

    auto drawCheckbox = [&](D2D1_RECT_F box, bool checked) {
        if (checked) {
            m_renderTarget->FillRoundedRectangle(D2D1::RoundedRect(box, 3.5f, 3.5f), m_accentBrush.Get());
            D2D1_RECT_F checkR = D2D1::RectF(box.left, box.top - 1.0f, box.right, box.bottom + 1.0f);
            m_renderTarget->DrawText(L"\uE73E", 1, g_dwrite.IconFontChevron(), checkR, m_sliderThumbBrush.Get());
        } else {
            m_renderTarget->FillRoundedRectangle(D2D1::RoundedRect(box, 3.5f, 3.5f), m_inputBgBrush.Get());
            m_renderTarget->DrawRoundedRectangle(D2D1::RoundedRect(box, 3.5f, 3.5f), m_inputBorderBrush.Get(), 1.0f);
        }
    };

    auto drawComboBox = [&](D2D1_RECT_F r, const WCHAR* text, bool isOpen, bool hover) {
        m_renderTarget->FillRoundedRectangle(D2D1::RoundedRect(r, 6.0f, 6.0f), m_inputBgBrush.Get());
        ID2D1SolidColorBrush* borderBrush = isOpen ? m_accentBrush.Get() : (hover ? m_accentHoverBrush.Get() : m_inputBorderBrush.Get());
        m_renderTarget->DrawRoundedRectangle(D2D1::RoundedRect(r, 6.0f, 6.0f), borderBrush, isOpen ? 1.5f : 1.0f);

        D2D1_RECT_F textR = D2D1::RectF(r.left + 12.0f, r.top, r.right - 26.0f, r.bottom);
        m_renderTarget->DrawText(text, static_cast<UINT32>(wcslen(text)), m_itemFormat.Get(), textR, m_textPrimaryBrush.Get());

        D2D1_RECT_F chevR = D2D1::RectF(r.right - 24.0f, r.top, r.right - 6.0f, r.bottom);
        m_renderTarget->DrawText(L"\uE70D", 1, g_dwrite.IconFontChevron(), chevR, m_textSecondaryBrush.Get());
    };

    m_maxScrollY = 296.0f;

    // Viewport Clipping: Y: 46.0f to 506.0f
    m_renderTarget->PushAxisAlignedClip(
        D2D1::RectF(0.0f, 46.0f, kBaseWidth, 506.0f),
        D2D1_ANTIALIAS_MODE_PER_PRIMITIVE
    );

    D2D1_MATRIX_3X2_F curTransform;
    m_renderTarget->GetTransform(&curTransform);
    m_renderTarget->SetTransform(
        D2D1::Matrix3x2F::Translation(0.0f, 46.0f - m_scrollY) * curTransform
    );

    // ── Card 1: App Theme ──
    D2D1_RECT_F card1 = D2D1::RectF(16.0f, 6.0f, 324.0f, 118.0f);
    drawCard(card1, L"\uE790", L"App Theme", L"Choose application theme appearance or sync with Windows OS.");

    const WCHAR* curThemeText = L"Sync with OS (System)";
    if (m_stagedSettings.theme == L"Light") curThemeText = L"Light Mode";
    else if (m_stagedSettings.theme == L"Dark") curThemeText = L"Dark Mode";
    D2D1_RECT_F combo1 = D2D1::RectF(28.0f, 72.0f, 312.0f, 104.0f);
    drawComboBox(combo1, curThemeText, m_openDropdown == DROPDOWN_THEME, m_hoverId == ID_THEME_COMBO);

    // ── Card 2: Accent Color ──
    D2D1_RECT_F card2 = D2D1::RectF(16.0f, 128.0f, 324.0f, 258.0f);
    drawCard(card2, L"\uE790", L"Accent Color", L"Enable or choose a custom accent color for Snippy.");

    // Toggle Checkbox
    D2D1_RECT_F accToggleBox = D2D1::RectF(284.0f, 155.0f, 302.0f, 173.0f);
    drawCheckbox(accToggleBox, m_stagedSettings.useAccentColor);
    D2D1_RECT_F accToggleLbl = D2D1::RectF(28.0f, 153.0f, 276.0f, 175.0f);
    m_renderTarget->DrawText(L"Use Accent Color", 16, m_itemFormat.Get(), accToggleLbl, m_textPrimaryBrush.Get());

    if (m_stagedSettings.useAccentColor) {
        D2D1_RECT_F paletteHdr = D2D1::RectF(28.0f, 180.0f, 312.0f, 198.0f);
        m_renderTarget->DrawText(L"Select Accent Color:", 20, m_descFormat.Get(), paletteHdr, m_textSecondaryBrush.Get());

        const float colW = 56.8f;
        const float swatchR = 10.5f;

        bool isCustomSelected = false;
        D2D1_COLOR_F customColor = D2D1::ColorF(0.5f, 0.5f, 0.5f, 1.0f);
        if (!m_stagedSettings.accentColor.empty()) {
            bool matchesPreset = false;
            for (const auto& pr : kAccentPresets) {
                if (_wcsicmp(m_stagedSettings.accentColor.c_str(), pr.hex) == 0) {
                    matchesPreset = true;
                    break;
                }
            }
            if (!matchesPreset) {
                isCustomSelected = true;
                ThemeManager::ParseHexColor(m_stagedSettings.accentColor, customColor);
            }
        }

        for (int row = 0; row < 2; ++row) {
            float scy = (row == 0) ? 204.0f : 234.0f;
            for (int col = 0; col < 5; ++col) {
                int idx = row * 5 + col;
                float scx = 28.0f + 28.4f + col * colW;
                D2D1_POINT_2F center = D2D1::Point2F(scx, scy);

                bool isSelected = false;
                bool isHover = false;
                D2D1_COLOR_F fillCol{};

                if (idx == 0) {
                    fillCol = ThemeManager::GetWindowsAccentColor();
                    isSelected = m_stagedSettings.accentColor.empty();
                    isHover = (m_hoverId == ID_ACCENT_SWATCH_SYSTEM);
                } else if (idx >= 1 && idx <= 8) {
                    fillCol = kAccentPresets[idx - 1].color;
                    isSelected = (!m_stagedSettings.accentColor.empty() &&
                                  _wcsicmp(m_stagedSettings.accentColor.c_str(), kAccentPresets[idx - 1].hex) == 0);
                    isHover = (m_hoverId == (ID_ACCENT_SWATCH_PRESET_START + idx - 1));
                } else {
                    fillCol = isCustomSelected ? customColor : D2D1::ColorF(0.32f, 0.32f, 0.36f, 1.0f);
                    isSelected = isCustomSelected;
                    isHover = (m_hoverId == ID_ACCENT_SWATCH_CUSTOM);
                }

                if (m_colorBrush) {
                    m_colorBrush->SetColor(fillCol);
                    m_renderTarget->FillEllipse(D2D1::Ellipse(center, swatchR, swatchR), m_colorBrush.Get());
                }

                if (isSelected) {
                    D2D1_RECT_F checkR = D2D1::RectF(scx - swatchR, scy - swatchR - 1.0f, scx + swatchR, scy + swatchR + 1.0f);
                    m_renderTarget->DrawText(L"\uE73E", 1, g_dwrite.IconFontChevron(), checkR, m_sliderThumbBrush.Get());
                } else if (idx == 0) {
                    D2D1_RECT_F txtR = D2D1::RectF(scx - swatchR, scy - 7.0f, scx + swatchR, scy + 7.0f);
                    m_renderTarget->DrawText(L"OS", 2, m_descFormat.Get(), txtR, m_sliderThumbBrush.Get());
                } else if (idx == 9 && !isCustomSelected) {
                    D2D1_RECT_F plusR = D2D1::RectF(scx - swatchR, scy - 9.0f, scx + swatchR, scy + 9.0f);
                    m_renderTarget->DrawText(L"+", 1, m_itemFormat.Get(), plusR, m_sliderThumbBrush.Get());
                }

                if (isSelected) {
                    m_renderTarget->DrawEllipse(D2D1::Ellipse(center, swatchR + 2.5f, swatchR + 2.5f), m_textPrimaryBrush.Get(), 1.5f);
                } else if (isHover) {
                    m_renderTarget->DrawEllipse(D2D1::Ellipse(center, swatchR + 2.0f, swatchR + 2.0f), m_accentHoverBrush.Get(), 1.0f);
                }
            }
        }
    } else {
        D2D1_RECT_F offMsgR = D2D1::RectF(28.0f, 186.0f, 312.0f, 246.0f);
        m_renderTarget->DrawText(L"Accent color is disabled.\nA neutral monochrome appearance is applied across Snippy.",
                                 72, m_descFormat.Get(), offMsgR, m_textSecondaryBrush.Get());
    }

    // ── Card 3: Snipping Shortcut Key ──
    D2D1_RECT_F card3 = D2D1::RectF(16.0f, 268.0f, 324.0f, 408.0f);
    drawCard(card3, L"\uE765", L"Snipping Shortcut Key", L"Press key combination to set your global hotkey.");

    D2D1_RECT_F recBox = D2D1::RectF(28.0f, 326.0f, 312.0f, 376.0f);
    m_renderTarget->FillRoundedRectangle(D2D1::RoundedRect(recBox, 6.0f, 6.0f), m_subtleBgBrush.Get());
    ID2D1SolidColorBrush* recBoxBorder = m_isRecording ? m_accentBrush.Get() : m_subtleBorderBrush.Get();
    m_renderTarget->DrawRoundedRectangle(D2D1::RoundedRect(recBox, 6.0f, 6.0f), recBoxBorder, m_isRecording ? 1.5f : 1.0f);

    D2D1_RECT_F kbIconR = D2D1::RectF(38.0f, 336.0f, 56.0f, 354.0f);
    m_renderTarget->DrawText(L"\uE765", 1, g_dwrite.IconFont14(), kbIconR, m_accentBrush.Get());

    std::wstring hotkeyStr = m_isRecording ? m_recordingText :
        SettingsManager::FormatHotkey(m_stagedSettings.altModifier, m_stagedSettings.ctrlModifier,
                                      m_stagedSettings.shiftModifier, m_stagedSettings.winModifier,
                                      m_stagedSettings.vk);
    D2D1_RECT_F hkTextR = D2D1::RectF(60.0f, 330.0f, 230.0f, 350.0f);
    m_renderTarget->DrawText(hotkeyStr.c_str(), static_cast<UINT32>(hotkeyStr.size()),
                             m_hotkeyBoldFormat.Get(), hkTextR,
                             m_isRecording ? m_accentBrush.Get() : m_greenBrush.Get());

    std::wstring hkSub;
    ID2D1SolidColorBrush* hkSubBrush = m_textSecondaryBrush.Get();
    if (m_isRecording) {
        if (!m_statusError.empty()) {
            hkSub = m_statusError;
            hkSubBrush = m_dangerBrush.Get();
        } else {
            hkSub = L"Listening for keypress... (Press Esc to cancel)";
        }
    } else {
        hkSub = L"Click to record new keybind";
    }
    D2D1_RECT_F hkSubR = D2D1::RectF(60.0f, 351.0f, 230.0f, 368.0f);
    m_renderTarget->DrawText(hkSub.c_str(), static_cast<UINT32>(hkSub.size()), m_descFormat.Get(), hkSubR, hkSubBrush);

    D2D1_RECT_F recBtnR = D2D1::RectF(232.0f, 338.0f, 304.0f, 364.0f);
    bool recHover = (m_hoverId == ID_HOTKEY_RECORD_BTN);
    ID2D1SolidColorBrush* recBtnBg = m_isRecording ? m_accentBrush.Get() : (recHover ? m_btnSecondaryHoverBrush.Get() : m_btnSecondaryBgBrush.Get());
    m_renderTarget->FillRoundedRectangle(D2D1::RoundedRect(recBtnR, 5.0f, 5.0f), recBtnBg);
    m_renderTarget->DrawRoundedRectangle(D2D1::RoundedRect(recBtnR, 5.0f, 5.0f), m_btnSecondaryBorderBrush.Get(), 1.0f);
    const WCHAR* recBtnLabel = m_isRecording ? L"Listening..." : L"Record";
    m_renderTarget->DrawText(recBtnLabel, static_cast<UINT32>(wcslen(recBtnLabel)), m_btnFormat.Get(), recBtnR,
                             m_isRecording ? m_sliderThumbBrush.Get() : m_textPrimaryBrush.Get());

    D2D1_RECT_F resetBtnPill = D2D1::RectF(146.0f, 381.0f, 312.0f, 401.0f);
    bool resetHkHover = (m_hoverId == ID_HOTKEY_RESET);
    if (resetHkHover) {
        m_renderTarget->FillRoundedRectangle(D2D1::RoundedRect(resetBtnPill, 4.0f, 4.0f), m_dropdownHoverBrush.Get());
    }
    ID2D1SolidColorBrush* resetHkBrush = resetHkHover ? m_textPrimaryBrush.Get() : m_textSecondaryBrush.Get();
    
    D2D1_RECT_F resetIconR = D2D1::RectF(150.0f, 383.0f, 164.0f, 399.0f);
    m_renderTarget->DrawText(L"\uE777", 1, g_dwrite.IconFontChevron(), resetIconR, resetHkBrush);
    D2D1_RECT_F resetLblR = D2D1::RectF(166.0f, 382.0f, 312.0f, 400.0f);
    m_renderTarget->DrawText(L"Reset to default (Alt+S)", 24, m_descFormat.Get(), resetLblR, resetHkBrush);

    // ── Card 4: Debug Logger Console ──
    D2D1_RECT_F card4 = D2D1::RectF(16.0f, 418.0f, 324.0f, 486.0f);
    drawCard(card4, L"\uEBE8", L"Debug Logger Console", L"Open a live debug console window for troubleshooting and error tracing.");
    D2D1_RECT_F cb3 = D2D1::RectF(284.0f, 441.0f, 302.0f, 459.0f);
    drawCheckbox(cb3, m_stagedSettings.enableDebugLogger);

    // ── Card 5: Customize Overlay ──
    D2D1_RECT_F card5 = D2D1::RectF(16.0f, 496.0f, 324.0f, 734.0f);
    drawCard(card5, L"\uE771", L"Customize Overlay", L"Adjust the look and feel of the snipping overlay.");

    D2D1_RECT_F dimLblR = D2D1::RectF(28.0f, 554.0f, 126.0f, 572.0f);
    m_renderTarget->DrawText(L"Background Dim:", 15, m_itemFormat.Get(), dimLblR, m_textPrimaryBrush.Get());

    float trackL = 126.0f, trackR = 246.0f;
    D2D1_RECT_F trackBg1 = D2D1::RectF(trackL, 562.0f, trackR, 566.0f);
    m_renderTarget->FillRoundedRectangle(D2D1::RoundedRect(trackBg1, 2.0f, 2.0f), m_sliderTrackBgBrush.Get());
    float dimNorm = std::clamp(m_stagedSettings.dimmingOpacity / 100.0f, 0.0f, 1.0f);
    float dimThumbX = trackL + (trackR - trackL) * dimNorm;
    D2D1_RECT_F trackFill1 = D2D1::RectF(trackL, 562.0f, dimThumbX, 566.0f);
    m_renderTarget->FillRoundedRectangle(D2D1::RoundedRect(trackFill1, 2.0f, 2.0f), m_accentBrush.Get());
    m_renderTarget->FillEllipse(D2D1::Ellipse(D2D1::Point2F(dimThumbX, 564.0f), 6.5f, 6.5f), m_sliderThumbBrush.Get());
    m_renderTarget->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(dimThumbX, 564.0f), 6.5f, 6.5f), m_accentBrush.Get(), 1.0f);

    std::wstring dimStr = std::to_wstring(m_stagedSettings.dimmingOpacity) + L"%";
    D2D1_RECT_F dimValR = D2D1::RectF(254.0f, 554.0f, 308.0f, 572.0f);
    m_renderTarget->DrawText(dimStr.c_str(), static_cast<UINT32>(dimStr.size()), m_itemFormat.Get(), dimValR, m_textSecondaryBrush.Get());

    D2D1_RECT_F brdLblR = D2D1::RectF(28.0f, 586.0f, 126.0f, 604.0f);
    m_renderTarget->DrawText(L"Selection Border:", 17, m_itemFormat.Get(), brdLblR, m_textPrimaryBrush.Get());

    D2D1_RECT_F trackBg2 = D2D1::RectF(trackL, 594.0f, trackR, 598.0f);
    m_renderTarget->FillRoundedRectangle(D2D1::RoundedRect(trackBg2, 2.0f, 2.0f), m_sliderTrackBgBrush.Get());
    float brdNorm = std::clamp((m_stagedSettings.selectionBorderThickness - 1) / 5.0f, 0.0f, 1.0f);
    float brdThumbX = trackL + (trackR - trackL) * brdNorm;
    D2D1_RECT_F trackFill2 = D2D1::RectF(trackL, 594.0f, brdThumbX, 598.0f);
    m_renderTarget->FillRoundedRectangle(D2D1::RoundedRect(trackFill2, 2.0f, 2.0f), m_accentBrush.Get());
    m_renderTarget->FillEllipse(D2D1::Ellipse(D2D1::Point2F(brdThumbX, 596.0f), 6.5f, 6.5f), m_sliderThumbBrush.Get());
    m_renderTarget->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(brdThumbX, 596.0f), 6.5f, 6.5f), m_accentBrush.Get(), 1.0f);

    std::wstring brdStr = std::to_wstring(m_stagedSettings.selectionBorderThickness) + L"px";
    D2D1_RECT_F brdValR = D2D1::RectF(254.0f, 586.0f, 308.0f, 604.0f);
    m_renderTarget->DrawText(brdStr.c_str(), static_cast<UINT32>(brdStr.size()), m_itemFormat.Get(), brdValR, m_textSecondaryBrush.Get());

    D2D1_RECT_F posLblR = D2D1::RectF(28.0f, 618.0f, 122.0f, 636.0f);
    m_renderTarget->DrawText(L"Toolbar Position:", 17, m_itemFormat.Get(), posLblR, m_textPrimaryBrush.Get());

    const WCHAR* posText = (m_stagedSettings.toolbarPosition == L"Bottom") ? L"Bottom Center" : L"Top Center";
    D2D1_RECT_F comboPos = D2D1::RectF(124.0f, kComboToolbarPosY, 308.0f, kComboToolbarPosY + 28.0f);
    drawComboBox(comboPos, posText, m_openDropdown == DROPDOWN_TOOLBAR_POS, m_hoverId == ID_TOOLBAR_POS_COMBO);

    D2D1_RECT_F toolsHdrR = D2D1::RectF(28.0f, 652.0f, 308.0f, 670.0f);
    m_renderTarget->DrawText(L"Visible Annotation Tools:", 25, m_itemFormat.Get(), toolsHdrR, m_textPrimaryBrush.Get());

    auto isToolVisible = [&](const WCHAR* name) {
        for (const auto& h : m_stagedSettings.hiddenTools) {
            if (_wcsicmp(h.c_str(), name) == 0) return false;
            if (_wcsicmp(name, L"Pen") == 0 || _wcsicmp(name, L"Draw") == 0) {
                if (_wcsicmp(h.c_str(), L"Draw Tools") == 0 || _wcsicmp(h.c_str(), L"DrawTools") == 0) {
                    return false;
                }
            }
        }
        return true;
    };

    auto drawToolCheck = [&](int id, D2D1_RECT_F r, const WCHAR* label, bool checked) {
        bool hover = (m_hoverId == id);
        D2D1_RECT_F box = D2D1::RectF(r.left, r.top + 2.0f, r.left + 15.0f, r.top + 17.0f);
        if (checked) {
            m_renderTarget->FillRoundedRectangle(D2D1::RoundedRect(box, 3.5f, 3.5f), hover ? m_accentHoverBrush.Get() : m_accentBrush.Get());
            D2D1_RECT_F checkR = D2D1::RectF(box.left, box.top - 1.0f, box.right, box.bottom + 1.0f);
            m_renderTarget->DrawText(L"\uE73E", 1, g_dwrite.IconFontChevron(), checkR, m_sliderThumbBrush.Get());
        } else {
            m_renderTarget->FillRoundedRectangle(D2D1::RoundedRect(box, 3.5f, 3.5f), m_inputBgBrush.Get());
            ID2D1SolidColorBrush* border = hover ? m_accentHoverBrush.Get() : m_inputBorderBrush.Get();
            m_renderTarget->DrawRoundedRectangle(D2D1::RoundedRect(box, 3.5f, 3.5f), border, 1.0f);
        }
        D2D1_RECT_F textR = D2D1::RectF(r.left + 20.0f, r.top, r.right, r.bottom);
        m_renderTarget->DrawText(label, static_cast<UINT32>(wcslen(label)), m_itemFormat.Get(), textR,
                                 hover ? m_textPrimaryBrush.Get() : m_textSecondaryBrush.Get());
    };

    drawToolCheck(ID_TOOL_DRAW,    D2D1::RectF(28.0f,  676.0f, 124.0f, 694.0f), L"Draw Tools", isToolVisible(L"Pen") && isToolVisible(L"Draw"));
    drawToolCheck(ID_TOOL_SHAPES,  D2D1::RectF(128.0f, 676.0f, 216.0f, 694.0f), L"Shapes",     isToolVisible(L"Shapes"));
    drawToolCheck(ID_TOOL_BLUR,    D2D1::RectF(220.0f, 676.0f, 308.0f, 694.0f), L"Blur",       isToolVisible(L"Blur"));
    drawToolCheck(ID_TOOL_TEXT,    D2D1::RectF(28.0f,  704.0f, 124.0f, 722.0f), L"Text",       isToolVisible(L"Text"));
    drawToolCheck(ID_TOOL_COUNTER, D2D1::RectF(128.0f, 704.0f, 216.0f, 722.0f), L"Counter",    isToolVisible(L"Counter"));

    m_renderTarget->SetTransform(curTransform);
    m_renderTarget->PopAxisAlignedClip();

    // ── Scrollbar Indicator ──
    if (m_maxScrollY > 0.0f) {
        float trackH = 448.0f;
        float thumbH = std::clamp(trackH * (460.0f / 610.0f), 30.0f, trackH);
        float thumbY = 52.0f + (trackH - thumbH) * (m_scrollY / m_maxScrollY);
        D2D1_RECT_F thumbR = D2D1::RectF(kBaseWidth - 6.0f, thumbY, kBaseWidth - 3.0f, thumbY + thumbH);
        m_renderTarget->FillRoundedRectangle(D2D1::RoundedRect(thumbR, 1.5f, 1.5f), m_scrollThumbBrush.Get());
    }

    // ── Pinned Header (Y: 0.0f to 46.0f) ──
    m_renderTarget->FillRectangle(D2D1::RectF(0.0f, 0.0f, kBaseWidth, 46.0f), m_bgBrush.Get());

    D2D1_RECT_F logoR = D2D1::RectF(16.0f, 13.0f, 36.0f, 33.0f);
    float lw = logoR.right - logoR.left;
    float lh = logoR.bottom - logoR.top;
    float ls = std::min(lw, lh) / 24.0f;
    float lox = logoR.left + (lw - 24.0f * ls) * 0.5f;
    float loy = logoR.top + (lh - 24.0f * ls) * 0.5f;
    auto lpt = [&](float x, float y) {
        return D2D1::Point2F(lox + x * ls, loy + y * ls);
    };
    float lstroke = 2.2f * ls;

    m_renderTarget->DrawEllipse(D2D1::Ellipse(lpt(6.0f, 6.0f), 3.0f * ls, 3.0f * ls), m_accentBrush.Get(), lstroke, m_roundStroke.Get());
    m_renderTarget->DrawEllipse(D2D1::Ellipse(lpt(6.0f, 18.0f), 3.0f * ls, 3.0f * ls), m_accentBrush.Get(), lstroke, m_roundStroke.Get());
    m_renderTarget->DrawLine(lpt(20.0f, 4.0f), lpt(8.12f, 15.88f), m_accentBrush.Get(), lstroke, m_roundStroke.Get());
    m_renderTarget->DrawLine(lpt(8.12f, 8.12f), lpt(12.0f, 12.0f), m_accentBrush.Get(), lstroke, m_roundStroke.Get());
    m_renderTarget->DrawLine(lpt(14.8f, 14.8f), lpt(20.0f, 20.0f), m_accentBrush.Get(), lstroke, m_roundStroke.Get());

    D2D1_RECT_F titleR = D2D1::RectF(44.0f, 11.0f, 260.0f, 35.0f);
    m_renderTarget->DrawText(L"Snippy Settings", 15, m_titleFormat.Get(), titleR, m_textPrimaryBrush.Get());

    D2D1_RECT_F closeR = D2D1::RectF(kBaseWidth - 38.0f, 10.0f, kBaseWidth - 14.0f, 34.0f);
    if (m_hoverId == ID_CLOSE_BTN) {
        m_renderTarget->FillRoundedRectangle(D2D1::RoundedRect(closeR, 4.0f, 4.0f), m_btnSecondaryHoverBrush.Get());
    }
    m_renderTarget->DrawText(L"\uE711", 1, g_dwrite.IconFontChevron(), closeR,
                             (m_hoverId == ID_CLOSE_BTN) ? m_textPrimaryBrush.Get() : m_textSecondaryBrush.Get());

    // ── Pinned Footer (Y: 506.0f to 556.0f) ──
    m_renderTarget->FillRectangle(D2D1::RectF(0.0f, 506.0f, kBaseWidth, 556.0f), m_bgBrush.Get());

    auto drawFooterBtn = [&](int id, D2D1_RECT_F r, const WCHAR* label, bool primary, bool feedback) {
        bool hover = (m_hoverId == id);
        ID2D1SolidColorBrush* bg = nullptr;
        if (feedback) {
            bg = m_greenBrush.Get();
        } else if (primary) {
            bg = hover ? m_accentHoverBrush.Get() : m_accentBrush.Get();
        } else {
            bg = hover ? m_btnSecondaryHoverBrush.Get() : m_btnSecondaryBgBrush.Get();
        }
        m_renderTarget->FillRoundedRectangle(D2D1::RoundedRect(r, 6.0f, 6.0f), bg);
        if (!primary && !feedback) {
            m_renderTarget->DrawRoundedRectangle(D2D1::RoundedRect(r, 6.0f, 6.0f), m_btnSecondaryBorderBrush.Get(), 1.0f);
        }
        m_renderTarget->DrawText(label, static_cast<UINT32>(wcslen(label)),
                                 primary ? m_btnPrimaryFormat.Get() : m_btnFormat.Get(),
                                 r, primary ? m_sliderThumbBrush.Get() : m_textPrimaryBrush.Get());
    };

    drawFooterBtn(ID_BTN_RESET,  D2D1::RectF(16.0f, 512.0f, 92.0f, 546.0f),  L"Reset",        false, false);
    drawFooterBtn(ID_BTN_CANCEL, D2D1::RectF(100.0f, 512.0f, 184.0f, 546.0f), L"Cancel",       false, false);
    const WCHAR* saveLabel = m_saveFeedback ? L"\u2713 Saved!" : L"Save & Apply";
    drawFooterBtn(ID_BTN_SAVE,   D2D1::RectF(192.0f, 512.0f, 324.0f, 546.0f), saveLabel,       true,  m_saveFeedback);

    // ── Dropdown Popups (Rendered on top of everything) ──
    if (m_openDropdown == DROPDOWN_THEME) {
        float topY = 46.0f - m_scrollY + 106.0f;
        D2D1_RECT_F popupR = D2D1::RectF(28.0f, topY, 312.0f, topY + 104.0f);

        m_renderTarget->FillRoundedRectangle(D2D1::RoundedRect(popupR, 6.0f, 6.0f), m_dropdownBgBrush.Get());
        m_renderTarget->DrawRoundedRectangle(D2D1::RoundedRect(popupR, 6.0f, 6.0f), m_inputBorderBrush.Get(), 1.0f);

        const WCHAR* items[3] = { L"Sync with OS (System)", L"Light Mode", L"Dark Mode" };
        bool selected[3] = {
            m_stagedSettings.theme == L"System",
            m_stagedSettings.theme == L"Light",
            m_stagedSettings.theme == L"Dark"
        };

        for (int i = 0; i < 3; ++i) {
            D2D1_RECT_F itemR = D2D1::RectF(32.0f, topY + 4.0f + i * 32.0f, 308.0f, topY + 36.0f + i * 32.0f);
            if (selected[i]) {
                m_renderTarget->FillRoundedRectangle(D2D1::RoundedRect(itemR, 4.0f, 4.0f), m_accentBrush.Get());
            } else if (m_hoverDropdownItem == i) {
                m_renderTarget->FillRoundedRectangle(D2D1::RoundedRect(itemR, 4.0f, 4.0f), m_dropdownHoverBrush.Get());
            }
            D2D1_RECT_F textR = D2D1::RectF(itemR.left + 8.0f, itemR.top, itemR.right, itemR.bottom);
            m_renderTarget->DrawText(items[i], static_cast<UINT32>(wcslen(items[i])), m_itemFormat.Get(), textR,
                                     selected[i] ? m_sliderThumbBrush.Get() : m_textPrimaryBrush.Get());
        }
    } else if (m_openDropdown == DROPDOWN_TOOLBAR_POS) {
        float comboTop = 46.0f - m_scrollY + kComboToolbarPosY;
        float comboBottom = comboTop + 28.0f;
        bool popUpward = (comboBottom + 72.0f > 506.0f);
        float topY = popUpward ? (comboTop - 2.0f - 72.0f) : (comboBottom + 2.0f);
        D2D1_RECT_F popupR = D2D1::RectF(124.0f, topY, 308.0f, topY + 72.0f);

        m_renderTarget->FillRoundedRectangle(D2D1::RoundedRect(popupR, 6.0f, 6.0f), m_dropdownBgBrush.Get());
        m_renderTarget->DrawRoundedRectangle(D2D1::RoundedRect(popupR, 6.0f, 6.0f), m_inputBorderBrush.Get(), 1.0f);

        const WCHAR* items[2] = { L"Top Center", L"Bottom Center" };
        bool selected[2] = {
            m_stagedSettings.toolbarPosition == L"Top",
            m_stagedSettings.toolbarPosition == L"Bottom"
        };

        for (int i = 0; i < 2; ++i) {
            D2D1_RECT_F itemR = D2D1::RectF(128.0f, topY + 4.0f + i * 32.0f, 304.0f, topY + 36.0f + i * 32.0f);
            if (selected[i]) {
                m_renderTarget->FillRoundedRectangle(D2D1::RoundedRect(itemR, 4.0f, 4.0f), m_accentBrush.Get());
            } else if (m_hoverDropdownItem == i) {
                m_renderTarget->FillRoundedRectangle(D2D1::RoundedRect(itemR, 4.0f, 4.0f), m_dropdownHoverBrush.Get());
            }
            D2D1_RECT_F textR = D2D1::RectF(itemR.left + 8.0f, itemR.top, itemR.right, itemR.bottom);
            m_renderTarget->DrawText(items[i], static_cast<UINT32>(wcslen(items[i])), m_itemFormat.Get(), textR,
                                     selected[i] ? m_sliderThumbBrush.Get() : m_textPrimaryBrush.Get());
        }
    }

    HRESULT hr = m_renderTarget->EndDraw();
    if (hr == D2DERR_RECREATE_TARGET) {
        DiscardD2DResources();
    }
}

// ── Hit Testing ───────────────────────────────────────────────────────────────

static int HitTest(float x, float y, float scrollY, bool useAccentColor) {
    if (x >= SettingsWindow::kBaseWidth - 38.0f && x <= SettingsWindow::kBaseWidth - 14.0f && y >= 10.0f && y <= 34.0f) {
        return ID_CLOSE_BTN;
    }

    if (y >= 512.0f && y <= 546.0f) {
        if (x >= 16.0f && x <= 92.0f) return ID_BTN_RESET;
        if (x >= 100.0f && x <= 184.0f) return ID_BTN_CANCEL;
        if (x >= 192.0f && x <= 324.0f) return ID_BTN_SAVE;
    }

    if (y >= 46.0f && y <= 506.0f) {
        float cy = y - 46.0f + scrollY;

        // Card 1: Theme
        if (x >= 28.0f && x <= 312.0f && cy >= 72.0f && cy <= 104.0f) return ID_THEME_COMBO;

        // Card 2: Accent Color
        if (x >= 28.0f && x <= 312.0f && cy >= 152.0f && cy <= 176.0f) return ID_ACCENT_TOGGLE;

        if (useAccentColor && cy >= 190.0f && cy <= 250.0f) {
            for (int r = 0; r < 2; ++r) {
                float scy = (r == 0) ? 204.0f : 234.0f;
                if (std::abs(cy - scy) <= 13.0f) {
                    for (int c = 0; c < 5; ++c) {
                        float scx = 28.0f + 28.4f + c * 56.8f;
                        if (std::abs(x - scx) <= 13.0f) {
                            int idx = r * 5 + c;
                            if (idx == 0) return ID_ACCENT_SWATCH_SYSTEM;
                            if (idx >= 1 && idx <= 8) return ID_ACCENT_SWATCH_PRESET_START + (idx - 1);
                            if (idx == 9) return ID_ACCENT_SWATCH_CUSTOM;
                        }
                    }
                }
            }
        }

        // Card 3: Shortcut Key
        if (x >= 232.0f && x <= 304.0f && cy >= 338.0f && cy <= 364.0f) return ID_HOTKEY_RECORD_BTN;
        if (x >= 28.0f && x <= 312.0f && cy >= 326.0f && cy <= 376.0f) return ID_HOTKEY_RECORD_BOX;
        if (x >= 146.0f && x <= 312.0f && cy >= 380.0f && cy <= 402.0f) return ID_HOTKEY_RESET;

        // Card 4: Debug Logger
        if (x >= 16.0f && x <= 324.0f && cy >= 418.0f && cy <= 486.0f) return ID_DEBUG_LOGGER_CARD;

        // Card 5: Customize Overlay
        if (x >= 120.0f && x <= 256.0f && cy >= 552.0f && cy <= 578.0f) return ID_SLIDER_DIMMING;
        if (x >= 120.0f && x <= 256.0f && cy >= 584.0f && cy <= 610.0f) return ID_SLIDER_BORDER;
        if (x >= 124.0f && x <= 308.0f && cy >= 612.0f && cy <= 640.0f) return ID_TOOLBAR_POS_COMBO;

        if (cy >= 674.0f && cy <= 696.0f) {
            if (x >= 28.0f && x < 126.0f)  return ID_TOOL_DRAW;
            if (x >= 126.0f && x < 218.0f) return ID_TOOL_SHAPES;
            if (x >= 218.0f && x <= 312.0f) return ID_TOOL_BLUR;
        }
        if (cy >= 702.0f && cy <= 724.0f) {
            if (x >= 28.0f && x < 126.0f)  return ID_TOOL_TEXT;
            if (x >= 126.0f && x < 218.0f) return ID_TOOL_COUNTER;
        }
    }

    return ID_NONE;
}

// ── Mouse & Key Handlers ──────────────────────────────────────────────────────

void SettingsWindow::OnLButtonDown(POINT pt) noexcept {
    SetCapture(m_hwnd);

    float x = pt.x / m_dpiScale;
    float y = pt.y / m_dpiScale;

    if (m_openDropdown == DROPDOWN_THEME) {
        float topY = 46.0f - m_scrollY + 106.0f;
        if (x >= 28.0f && x <= 312.0f && y >= topY && y <= topY + 104.0f) {
            int item = static_cast<int>((y - (topY + 4.0f)) / 32.0f);
            if (item == 0) m_stagedSettings.theme = L"System";
            else if (item == 1) m_stagedSettings.theme = L"Light";
            else if (item == 2) m_stagedSettings.theme = L"Dark";
            ApplyThemePalette(m_stagedSettings.theme);
        }
        m_openDropdown = DROPDOWN_NONE;
        InvalidateRect(m_hwnd, nullptr, FALSE);
        return;
    }

    if (m_openDropdown == DROPDOWN_TOOLBAR_POS) {
        float comboTop = 46.0f - m_scrollY + kComboToolbarPosY;
        float comboBottom = comboTop + 28.0f;
        bool popUpward = (comboBottom + 72.0f > 506.0f);
        float topY = popUpward ? (comboTop - 2.0f - 72.0f) : (comboBottom + 2.0f);
        if (x >= 124.0f && x <= 308.0f && y >= topY && y <= topY + 72.0f) {
            int item = static_cast<int>((y - (topY + 4.0f)) / 32.0f);
            if (item == 0) m_stagedSettings.toolbarPosition = L"Top";
            else if (item == 1) m_stagedSettings.toolbarPosition = L"Bottom";
        }
        m_openDropdown = DROPDOWN_NONE;
        InvalidateRect(m_hwnd, nullptr, FALSE);
        return;
    }

    int id = HitTest(x, y, m_scrollY, m_stagedSettings.useAccentColor);

    if (y < 46.0f && id != ID_CLOSE_BTN) {
        ReleaseCapture();
        SendMessageW(m_hwnd, WM_NCLBUTTONDOWN, HTCAPTION, 0);
        return;
    }

    if (id == ID_SLIDER_DIMMING) {
        m_draggingDim = true;
        float trackL = 126.0f, trackR = 246.0f;
        float val = std::clamp((x - trackL) / (trackR - trackL), 0.0f, 1.0f);
        m_stagedSettings.dimmingOpacity = static_cast<int>(std::round(val * 100.0f));
        InvalidateRect(m_hwnd, nullptr, FALSE);
        return;
    }

    if (id == ID_SLIDER_BORDER) {
        m_draggingBrd = true;
        float trackL = 126.0f, trackR = 246.0f;
        float val = std::clamp((x - trackL) / (trackR - trackL), 0.0f, 1.0f);
        m_stagedSettings.selectionBorderThickness = 1 + static_cast<int>(std::round(val * 5.0f));
        InvalidateRect(m_hwnd, nullptr, FALSE);
        return;
    }

    switch (id) {
    case ID_CLOSE_BTN:
    case ID_BTN_CANCEL:
        ApplyThemePalette(SettingsManager::Current().theme);
        Hide();
        break;

    case ID_THEME_COMBO:
        m_openDropdown = (m_openDropdown == DROPDOWN_THEME) ? DROPDOWN_NONE : DROPDOWN_THEME;
        break;

    case ID_TOOLBAR_POS_COMBO:
        m_openDropdown = (m_openDropdown == DROPDOWN_TOOLBAR_POS) ? DROPDOWN_NONE : DROPDOWN_TOOLBAR_POS;
        break;

    case ID_ACCENT_TOGGLE:
        m_stagedSettings.useAccentColor = !m_stagedSettings.useAccentColor;
        ApplyThemePalette(m_stagedSettings.theme);
        break;

    case ID_ACCENT_SWATCH_SYSTEM:
        m_stagedSettings.useAccentColor = true;
        m_stagedSettings.accentColor.clear();
        ApplyThemePalette(m_stagedSettings.theme);
        break;

    case ID_ACCENT_SWATCH_CUSTOM: {
        CHOOSECOLORW cc{};
        static COLORREF customColors[16]{};
        cc.lStructSize = sizeof(cc);
        cc.hwndOwner = m_hwnd;
        cc.lpCustColors = customColors;
        COLORREF initialColor = RGB(0, 120, 212);
        D2D1_COLOR_F c{};
        if (!m_stagedSettings.accentColor.empty() && ThemeManager::ParseHexColor(m_stagedSettings.accentColor, c)) {
            initialColor = RGB(static_cast<BYTE>(c.r * 255.f),
                               static_cast<BYTE>(c.g * 255.f),
                               static_cast<BYTE>(c.b * 255.f));
        }
        cc.rgbResult = initialColor;
        cc.Flags = CC_FULLOPEN | CC_RGBINIT;
        if (ChooseColorW(&cc)) {
            COLORREF res = cc.rgbResult;
            WCHAR hexBuf[16];
            swprintf_s(hexBuf, L"#%02X%02X%02X", GetRValue(res), GetGValue(res), GetBValue(res));
            m_stagedSettings.accentColor = hexBuf;
            m_stagedSettings.useAccentColor = true;
            ApplyThemePalette(m_stagedSettings.theme);
        }
        break;
    }

    case ID_HOTKEY_RECORD_BTN:
        if (m_isRecording) StopRecording();
        else StartRecording();
        break;

    case ID_HOTKEY_RECORD_BOX:
        if (!m_isRecording) StartRecording();
        break;

    case ID_HOTKEY_RESET:
        StopRecording();
        m_stagedSettings.altModifier   = true;
        m_stagedSettings.ctrlModifier  = false;
        m_stagedSettings.shiftModifier = false;
        m_stagedSettings.winModifier   = false;
        m_stagedSettings.vk            = 'S';
        m_stagedSettings.keyName       = L"S";
        m_statusError.clear();
        InvalidateRect(m_hwnd, nullptr, FALSE);
        break;

    case ID_DEBUG_LOGGER_CARD:
        m_stagedSettings.enableDebugLogger = !m_stagedSettings.enableDebugLogger;
        break;

    case ID_TOOL_DRAW: {
        bool currentlyHidden = false;
        for (const auto& h : m_stagedSettings.hiddenTools) {
            if (_wcsicmp(h.c_str(), L"Pen") == 0 || _wcsicmp(h.c_str(), L"Draw") == 0 ||
                _wcsicmp(h.c_str(), L"Draw Tools") == 0 || _wcsicmp(h.c_str(), L"DrawTools") == 0) {
                currentlyHidden = true;
                break;
            }
        }
        if (currentlyHidden) {
            m_stagedSettings.hiddenTools.erase(
                std::remove_if(m_stagedSettings.hiddenTools.begin(), m_stagedSettings.hiddenTools.end(),
                               [](const std::wstring& s) {
                                   return _wcsicmp(s.c_str(), L"Pen") == 0 ||
                                          _wcsicmp(s.c_str(), L"Draw") == 0 ||
                                          _wcsicmp(s.c_str(), L"Draw Tools") == 0 ||
                                          _wcsicmp(s.c_str(), L"DrawTools") == 0;
                               }),
                m_stagedSettings.hiddenTools.end());
        } else {
            m_stagedSettings.hiddenTools.push_back(L"Pen");
            m_stagedSettings.hiddenTools.push_back(L"Draw");
        }
        break;
    }
    case ID_TOOL_SHAPES:
    case ID_TOOL_BLUR:
    case ID_TOOL_TEXT:
    case ID_TOOL_COUNTER: {
        const WCHAR* name = (id == ID_TOOL_SHAPES)  ? L"Shapes" :
                            (id == ID_TOOL_BLUR)    ? L"Blur" :
                            (id == ID_TOOL_TEXT)    ? L"Text" : L"Counter";
        auto it = std::find_if(m_stagedSettings.hiddenTools.begin(), m_stagedSettings.hiddenTools.end(),
                               [&](const std::wstring& s) { return _wcsicmp(s.c_str(), name) == 0; });
        if (it != m_stagedSettings.hiddenTools.end()) m_stagedSettings.hiddenTools.erase(it);
        else m_stagedSettings.hiddenTools.push_back(name);
        break;
    }

    case ID_BTN_RESET:
        ResetToDefaults();
        break;

    case ID_BTN_SAVE:
        SaveAndApply();
        break;

    default:
        if (id >= ID_ACCENT_SWATCH_PRESET_START && id < ID_ACCENT_SWATCH_PRESET_START + 8) {
            int pIdx = id - ID_ACCENT_SWATCH_PRESET_START;
            m_stagedSettings.accentColor = kAccentPresets[pIdx].hex;
            m_stagedSettings.useAccentColor = true;
            ApplyThemePalette(m_stagedSettings.theme);
        }
        break;
    }

    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void SettingsWindow::OnMouseMove(POINT pt) noexcept {
    float x = pt.x / m_dpiScale;
    float y = pt.y / m_dpiScale;
    float trackL = 126.0f, trackR = 246.0f;

    if (m_draggingDim) {
        float val = std::clamp((x - trackL) / (trackR - trackL), 0.0f, 1.0f);
        m_stagedSettings.dimmingOpacity = static_cast<int>(std::round(val * 100.0f));
        InvalidateRect(m_hwnd, nullptr, FALSE);
        return;
    }

    if (m_draggingBrd) {
        float val = std::clamp((x - trackL) / (trackR - trackL), 0.0f, 1.0f);
        m_stagedSettings.selectionBorderThickness = 1 + static_cast<int>(std::round(val * 5.0f));
        InvalidateRect(m_hwnd, nullptr, FALSE);
        return;
    }

    if (m_openDropdown == DROPDOWN_THEME) {
        float topY = 46.0f - m_scrollY + 106.0f;
        int hover = -1;
        if (x >= 28.0f && x <= 312.0f && y >= topY && y <= topY + 104.0f) {
            hover = static_cast<int>((y - (topY + 4.0f)) / 32.0f);
            if (hover < 0 || hover > 2) hover = -1;
        }
        if (hover != m_hoverDropdownItem) {
            m_hoverDropdownItem = hover;
            InvalidateRect(m_hwnd, nullptr, FALSE);
        }
        return;
    }

    if (m_openDropdown == DROPDOWN_TOOLBAR_POS) {
        float comboTop = 46.0f - m_scrollY + kComboToolbarPosY;
        float comboBottom = comboTop + 28.0f;
        bool popUpward = (comboBottom + 72.0f > 506.0f);
        float topY = popUpward ? (comboTop - 2.0f - 72.0f) : (comboBottom + 2.0f);
        int hover = -1;
        if (x >= 124.0f && x <= 308.0f && y >= topY && y <= topY + 72.0f) {
            hover = static_cast<int>((y - (topY + 4.0f)) / 32.0f);
            if (hover < 0 || hover > 1) hover = -1;
        }
        if (hover != m_hoverDropdownItem) {
            m_hoverDropdownItem = hover;
            InvalidateRect(m_hwnd, nullptr, FALSE);
        }
        return;
    }

    int newHover = HitTest(x, y, m_scrollY, m_stagedSettings.useAccentColor);
    if (newHover != m_hoverId) {
        m_hoverId = newHover;
        InvalidateRect(m_hwnd, nullptr, FALSE);
    }
}

void SettingsWindow::OnLButtonUp(POINT pt) noexcept {
    ReleaseCapture();
    m_draggingDim = false;
    m_draggingBrd = false;
}

void SettingsWindow::OnMouseWheel(short delta) noexcept {
    if (m_openDropdown != DROPDOWN_NONE) {
        m_openDropdown = DROPDOWN_NONE;
    }
    m_scrollY -= (delta / 120.0f) * 40.0f;
    m_scrollY = std::clamp(m_scrollY, 0.0f, m_maxScrollY);
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void SettingsWindow::StartRecording() noexcept {
    m_isRecording = true;
    m_recordingText = L"Press shortcut keys...";
    m_statusError.clear();
    SetFocus(m_hwnd);
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void SettingsWindow::StopRecording() noexcept {
    m_isRecording = false;
    m_recordingText = L"Press shortcut keys...";
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

static bool IsModifierVk(WPARAM vk) noexcept {
    return vk == VK_SHIFT || vk == VK_LSHIFT || vk == VK_RSHIFT ||
           vk == VK_CONTROL || vk == VK_LCONTROL || vk == VK_RCONTROL ||
           vk == VK_MENU || vk == VK_LMENU || vk == VK_RMENU ||
           vk == VK_LWIN || vk == VK_RWIN;
}

static bool IsAlphanumericVk(WPARAM vk) noexcept {
    return (vk >= 'A' && vk <= 'Z') ||
           (vk >= '0' && vk <= '9') ||
           (vk >= VK_NUMPAD0 && vk <= VK_NUMPAD9);
}

void SettingsWindow::OnKeyDown(WPARAM vk) noexcept {
    if (!m_isRecording) return;

    if (vk == VK_ESCAPE) {
        StopRecording();
        return;
    }

    bool alt   = (GetKeyState(VK_MENU) & 0x8000) != 0;
    bool ctrl  = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
    bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
    bool win   = ((GetKeyState(VK_LWIN) & 0x8000) != 0) || ((GetKeyState(VK_RWIN) & 0x8000) != 0);

    if (vk == VK_MENU || vk == VK_LMENU || vk == VK_RMENU) alt = true;
    if (vk == VK_CONTROL || vk == VK_LCONTROL || vk == VK_RCONTROL) ctrl = true;
    if (vk == VK_SHIFT || vk == VK_LSHIFT || vk == VK_RSHIFT) shift = true;
    if (vk == VK_LWIN || vk == VK_RWIN) win = true;

    if (IsModifierVk(vk)) {
        std::wstring modStr = SettingsManager::FormatModifiers(alt, ctrl, shift, win);
        if (!modStr.empty()) {
            m_recordingText = modStr + L" + ...";
        } else {
            m_recordingText = L"Press shortcut keys...";
        }
        m_statusError.clear();
        InvalidateRect(m_hwnd, nullptr, FALSE);
        return;
    }

    m_stagedSettings.altModifier   = alt;
    m_stagedSettings.ctrlModifier  = ctrl;
    m_stagedSettings.shiftModifier = shift;
    m_stagedSettings.winModifier   = win;
    m_stagedSettings.vk            = static_cast<UINT>(vk);
    m_stagedSettings.keyName       = SettingsManager::NameFromVk(static_cast<UINT>(vk));
    m_statusError.clear();

    StopRecording();
}

void SettingsWindow::OnKeyUp(WPARAM vk) noexcept {
    if (!m_isRecording) return;

    bool alt   = (GetKeyState(VK_MENU) & 0x8000) != 0;
    bool ctrl  = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
    bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
    bool win   = ((GetKeyState(VK_LWIN) & 0x8000) != 0) || ((GetKeyState(VK_RWIN) & 0x8000) != 0);

    if (vk == VK_MENU || vk == VK_LMENU || vk == VK_RMENU) alt = false;
    if (vk == VK_CONTROL || vk == VK_LCONTROL || vk == VK_RCONTROL) ctrl = false;
    if (vk == VK_SHIFT || vk == VK_LSHIFT || vk == VK_RSHIFT) shift = false;
    if (vk == VK_LWIN || vk == VK_RWIN) win = false;

    std::wstring modStr = SettingsManager::FormatModifiers(alt, ctrl, shift, win);
    if (!modStr.empty()) {
        m_recordingText = modStr + L" + ...";
    } else {
        m_recordingText = L"Press shortcut keys...";
    }
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void SettingsWindow::OnChar(WPARAM ch) noexcept {
}

void SettingsWindow::SaveAndApply() noexcept {
    if (m_isRecording) StopRecording();

    SettingsManager::Current() = m_stagedSettings;
    SettingsManager::SaveSettings();

    m_saveFeedback = true;
    InvalidateRect(m_hwnd, nullptr, FALSE);

    if (OnSettingsChanged) {
        OnSettingsChanged();
    }

    SetTimer(m_hwnd, kTimerSaveFeedback, 400, nullptr);
}

void SettingsWindow::ResetToDefaults() noexcept {
    SettingsManager::ResetToDefaults();
    m_stagedSettings = SettingsManager::Current();
    m_isRecording = false;
    m_statusError.clear();
    ApplyThemePalette(m_stagedSettings.theme);
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

// ── Window Procedure ──────────────────────────────────────────────────────────

LRESULT CALLBACK SettingsWindow::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) noexcept {
    SettingsWindow* self = nullptr;
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = static_cast<SettingsWindow*>(cs->lpCreateParams);
        if (self) self->m_hwnd = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    } else {
        self = reinterpret_cast<SettingsWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }
    if (self) return self->HandleMessage(msg, wParam, lParam);
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

LRESULT SettingsWindow::HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam) noexcept {
    switch (msg) {
    case WM_ERASEBKGND:
        return 1;

    case WM_PAINT: {
        PAINTSTRUCT ps;
        BeginPaint(m_hwnd, &ps);
        Render();
        EndPaint(m_hwnd, &ps);
        return 0;
    }
    case WM_LBUTTONDOWN:
        OnLButtonDown({ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) });
        return 0;

    case WM_MOUSEMOVE:
        OnMouseMove({ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) });
        return 0;

    case WM_LBUTTONUP:
        OnLButtonUp({ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) });
        return 0;

    case WM_MOUSEWHEEL:
        OnMouseWheel(GET_WHEEL_DELTA_WPARAM(wParam));
        return 0;

    case WM_CHAR:
        break;

    case WM_KEYDOWN:
        if (m_isRecording) {
            OnKeyDown(wParam);
            return 0;
        }
        break;

    case WM_SYSKEYDOWN:
        if (m_isRecording) {
            OnKeyDown(wParam);
            return 0;
        }
        break;

    case WM_KEYUP:
        if (m_isRecording) {
            OnKeyUp(wParam);
            return 0;
        }
        break;

    case WM_SYSKEYUP:
        if (m_isRecording) {
            OnKeyUp(wParam);
            return 0;
        }
        break;

    case WM_KILLFOCUS:
        if (m_isRecording) {
            StopRecording();
        }
        break;

    case WM_TIMER:
        if (wParam == kTimerSaveFeedback) {
            KillTimer(m_hwnd, kTimerSaveFeedback);
            m_saveFeedback = false;
            Hide();
            return 0;
        }
        break;

    case WM_DPICHANGED: {
        m_dpiScale = LOWORD(wParam) / 96.0f;
        auto* prc = reinterpret_cast<RECT*>(lParam);
        SetWindowPos(m_hwnd, nullptr, prc->left, prc->top,
                     prc->right - prc->left, prc->bottom - prc->top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        DiscardD2DResources();
        InvalidateRect(m_hwnd, nullptr, FALSE);
        return 0;
    }
    case WM_CLOSE:
        ApplyThemePalette(SettingsManager::Current().theme);
        Hide();
        return 0;
    }

    return DefWindowProcW(m_hwnd, msg, wParam, lParam);
}

} // namespace snippy
