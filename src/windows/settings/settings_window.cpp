#include "settings_window.h"
#include "../render/d2d_context.h"
#include "../render/dwrite_context.h"
#include "../theme/theme_manager.h"
#include "../update/update_checker.h"
#include "../resources/resource.h"
#include <dwmapi.h>
#include <windowsx.h>
#include <algorithm>
#include <cmath>

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
    ID_HOTKEY_RECORD_BOX,
    ID_HOTKEY_RECORD_BTN,
    ID_HOTKEY_RESET,
    ID_VOICE_ACTIVATION_CARD,
    ID_VOICE_KEYWORD_BOX,
    ID_VOICE_KEYWORD_RESET,
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
    ThemePalette p = ThemeManager::GetPalette(themeSetting, m_stagedSettings.accentColor);

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
    m_editingVoiceKeyword = false;
    m_caretVisible = false;
    m_lastVoiceInputTime = 0;
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
        StopEditingVoiceKeyword();
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

    m_maxScrollY = 270.0f;

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

    // ── Card 2: Snipping Shortcut Key ──
    D2D1_RECT_F card2 = D2D1::RectF(16.0f, 128.0f, 324.0f, 268.0f);
    drawCard(card2, L"\uE765", L"Snipping Shortcut Key", L"Press key combination to set your global hotkey.");

    D2D1_RECT_F recBox = D2D1::RectF(28.0f, 186.0f, 312.0f, 236.0f);
    m_renderTarget->FillRoundedRectangle(D2D1::RoundedRect(recBox, 6.0f, 6.0f), m_subtleBgBrush.Get());
    ID2D1SolidColorBrush* recBoxBorder = m_isRecording ? m_accentBrush.Get() : m_subtleBorderBrush.Get();
    m_renderTarget->DrawRoundedRectangle(D2D1::RoundedRect(recBox, 6.0f, 6.0f), recBoxBorder, m_isRecording ? 1.5f : 1.0f);

    D2D1_RECT_F kbIconR = D2D1::RectF(38.0f, 196.0f, 56.0f, 214.0f);
    m_renderTarget->DrawText(L"\uE765", 1, g_dwrite.IconFont14(), kbIconR, m_accentBrush.Get());

    std::wstring hotkeyStr = m_isRecording ? m_recordingText :
        SettingsManager::FormatHotkey(m_stagedSettings.altModifier, m_stagedSettings.ctrlModifier,
                                      m_stagedSettings.shiftModifier, m_stagedSettings.winModifier,
                                      m_stagedSettings.vk);
    D2D1_RECT_F hkTextR = D2D1::RectF(60.0f, 190.0f, 230.0f, 210.0f);
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
    D2D1_RECT_F hkSubR = D2D1::RectF(60.0f, 211.0f, 230.0f, 228.0f);
    m_renderTarget->DrawText(hkSub.c_str(), static_cast<UINT32>(hkSub.size()), m_descFormat.Get(), hkSubR, hkSubBrush);

    D2D1_RECT_F recBtnR = D2D1::RectF(232.0f, 198.0f, 304.0f, 224.0f);
    bool recHover = (m_hoverId == ID_HOTKEY_RECORD_BTN);
    ID2D1SolidColorBrush* recBtnBg = m_isRecording ? m_accentBrush.Get() : (recHover ? m_btnSecondaryHoverBrush.Get() : m_btnSecondaryBgBrush.Get());
    m_renderTarget->FillRoundedRectangle(D2D1::RoundedRect(recBtnR, 5.0f, 5.0f), recBtnBg);
    m_renderTarget->DrawRoundedRectangle(D2D1::RoundedRect(recBtnR, 5.0f, 5.0f), m_btnSecondaryBorderBrush.Get(), 1.0f);
    const WCHAR* recBtnLabel = m_isRecording ? L"Listening..." : L"Record";
    m_renderTarget->DrawText(recBtnLabel, static_cast<UINT32>(wcslen(recBtnLabel)), m_btnFormat.Get(), recBtnR,
                             m_isRecording ? m_sliderThumbBrush.Get() : m_textPrimaryBrush.Get());

    D2D1_RECT_F resetBtnPill = D2D1::RectF(146.0f, 241.0f, 312.0f, 261.0f);
    bool resetHkHover = (m_hoverId == ID_HOTKEY_RESET);
    if (resetHkHover) {
        m_renderTarget->FillRoundedRectangle(D2D1::RoundedRect(resetBtnPill, 4.0f, 4.0f), m_dropdownHoverBrush.Get());
    }
    ID2D1SolidColorBrush* resetHkBrush = resetHkHover ? m_textPrimaryBrush.Get() : m_textSecondaryBrush.Get();
    
    D2D1_RECT_F resetIconR = D2D1::RectF(150.0f, 243.0f, 164.0f, 259.0f);
    m_renderTarget->DrawText(L"\uE777", 1, g_dwrite.IconFontChevron(), resetIconR, resetHkBrush);

    const WCHAR* resetText = L"Reset to default (Alt + S)";
    D2D1_RECT_F resetTextR = D2D1::RectF(166.0f, 242.0f, 312.0f, 260.0f);
    m_renderTarget->DrawText(resetText, static_cast<UINT32>(wcslen(resetText)), m_descFormat.Get(), resetTextR, resetHkBrush);

    // ── Card 3: Voice Activation ──
    D2D1_RECT_F cardVoice = D2D1::RectF(16.0f, 278.0f, 324.0f, 388.0f);
    drawCard(cardVoice, L"\uE720", L"Voice Activation", L"Trigger screen capture hands-free with your voice.");
    D2D1_RECT_F cbVoice = D2D1::RectF(284.0f, 288.0f, 302.0f, 306.0f);
    drawCheckbox(cbVoice, m_stagedSettings.enableVoiceActivation);

    D2D1_RECT_F kwBox = D2D1::RectF(28.0f, 328.0f, 246.0f, 356.0f);
    m_renderTarget->FillRoundedRectangle(D2D1::RoundedRect(kwBox, 5.0f, 5.0f), m_inputBgBrush.Get());
    ID2D1SolidColorBrush* kwBorder = m_editingVoiceKeyword ? m_accentBrush.Get() :
        (m_hoverId == ID_VOICE_KEYWORD_BOX ? m_accentHoverBrush.Get() : m_inputBorderBrush.Get());
    m_renderTarget->DrawRoundedRectangle(D2D1::RoundedRect(kwBox, 5.0f, 5.0f), kwBorder, m_editingVoiceKeyword ? 1.5f : 1.0f);

    D2D1_RECT_F kwIconR = D2D1::RectF(36.0f, 334.0f, 50.0f, 350.0f);
    m_renderTarget->DrawText(L"\uE720", 1, g_dwrite.IconFontChevron(), kwIconR, m_accentBrush.Get());

    std::wstring displayKw = m_stagedSettings.voiceKeyword;
    if (m_editingVoiceKeyword && m_caretVisible) {
        displayKw += L"|";
    }
    D2D1_RECT_F kwTextR = D2D1::RectF(54.0f, 329.0f, 242.0f, 355.0f);
    m_renderTarget->DrawText(displayKw.c_str(), static_cast<UINT32>(displayKw.size()), m_itemFormat.Get(), kwTextR, m_textPrimaryBrush.Get());

    D2D1_RECT_F kwResetR = D2D1::RectF(252.0f, 328.0f, 312.0f, 356.0f);
    bool resetHover = (m_hoverId == ID_VOICE_KEYWORD_RESET);
    m_renderTarget->FillRoundedRectangle(D2D1::RoundedRect(kwResetR, 5.0f, 5.0f),
                                         resetHover ? m_btnSecondaryHoverBrush.Get() : m_btnSecondaryBgBrush.Get());
    m_renderTarget->DrawRoundedRectangle(D2D1::RoundedRect(kwResetR, 5.0f, 5.0f), m_btnSecondaryBorderBrush.Get(), 1.0f);
    m_renderTarget->DrawText(L"Default", 7, m_btnFormat.Get(), kwResetR,
                             resetHover ? m_textPrimaryBrush.Get() : m_textSecondaryBrush.Get());

    std::wstring hint = L"Say \"" + (m_stagedSettings.voiceKeyword.empty() ? L"Snippy snip it" : m_stagedSettings.voiceKeyword) + L"\" to snip";
    if (m_editingVoiceKeyword) hint += L" (Press Enter to apply)";
    D2D1_RECT_F hintR = D2D1::RectF(28.0f, 362.0f, 312.0f, 380.0f);
    m_renderTarget->DrawText(hint.c_str(), static_cast<UINT32>(hint.size()), m_descFormat.Get(), hintR, m_textSecondaryBrush.Get());

    // ── Card 4: Debug Logger Console ──
    D2D1_RECT_F card3 = D2D1::RectF(16.0f, 398.0f, 324.0f, 466.0f);
    drawCard(card3, L"\uEBE8", L"Debug Logger Console", L"Open a live debug console window for troubleshooting and error tracing.");
    D2D1_RECT_F cb3 = D2D1::RectF(284.0f, 421.0f, 302.0f, 439.0f);
    drawCheckbox(cb3, m_stagedSettings.enableDebugLogger);

    // ── Card 5: Customize Overlay ──
    D2D1_RECT_F card4 = D2D1::RectF(16.0f, 476.0f, 324.0f, 714.0f);
    drawCard(card4, L"\uE771", L"Customize Overlay", L"Adjust the look and feel of the snipping overlay.");

    D2D1_RECT_F dimLblR = D2D1::RectF(28.0f, 534.0f, 126.0f, 552.0f);
    m_renderTarget->DrawText(L"Background Dim:", 15, m_itemFormat.Get(), dimLblR, m_textPrimaryBrush.Get());

    float trackL = 126.0f, trackR = 246.0f;
    D2D1_RECT_F trackBg1 = D2D1::RectF(trackL, 542.0f, trackR, 546.0f);
    m_renderTarget->FillRoundedRectangle(D2D1::RoundedRect(trackBg1, 2.0f, 2.0f), m_sliderTrackBgBrush.Get());
    float dimNorm = std::clamp(m_stagedSettings.dimmingOpacity / 100.0f, 0.0f, 1.0f);
    float dimThumbX = trackL + (trackR - trackL) * dimNorm;
    D2D1_RECT_F trackFill1 = D2D1::RectF(trackL, 542.0f, dimThumbX, 546.0f);
    m_renderTarget->FillRoundedRectangle(D2D1::RoundedRect(trackFill1, 2.0f, 2.0f), m_accentBrush.Get());
    m_renderTarget->FillEllipse(D2D1::Ellipse(D2D1::Point2F(dimThumbX, 544.0f), 6.5f, 6.5f), m_sliderThumbBrush.Get());
    m_renderTarget->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(dimThumbX, 544.0f), 6.5f, 6.5f), m_accentBrush.Get(), 1.0f);

    std::wstring dimStr = std::to_wstring(m_stagedSettings.dimmingOpacity) + L"%";
    D2D1_RECT_F dimValR = D2D1::RectF(254.0f, 534.0f, 308.0f, 552.0f);
    m_renderTarget->DrawText(dimStr.c_str(), static_cast<UINT32>(dimStr.size()), m_itemFormat.Get(), dimValR, m_textSecondaryBrush.Get());

    D2D1_RECT_F brdLblR = D2D1::RectF(28.0f, 566.0f, 126.0f, 584.0f);
    m_renderTarget->DrawText(L"Selection Border:", 17, m_itemFormat.Get(), brdLblR, m_textPrimaryBrush.Get());

    D2D1_RECT_F trackBg2 = D2D1::RectF(trackL, 574.0f, trackR, 578.0f);
    m_renderTarget->FillRoundedRectangle(D2D1::RoundedRect(trackBg2, 2.0f, 2.0f), m_sliderTrackBgBrush.Get());
    float brdNorm = std::clamp((m_stagedSettings.selectionBorderThickness - 1) / 5.0f, 0.0f, 1.0f);
    float brdThumbX = trackL + (trackR - trackL) * brdNorm;
    D2D1_RECT_F trackFill2 = D2D1::RectF(trackL, 574.0f, brdThumbX, 578.0f);
    m_renderTarget->FillRoundedRectangle(D2D1::RoundedRect(trackFill2, 2.0f, 2.0f), m_accentBrush.Get());
    m_renderTarget->FillEllipse(D2D1::Ellipse(D2D1::Point2F(brdThumbX, 576.0f), 6.5f, 6.5f), m_sliderThumbBrush.Get());
    m_renderTarget->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(brdThumbX, 576.0f), 6.5f, 6.5f), m_accentBrush.Get(), 1.0f);

    std::wstring brdStr = std::to_wstring(m_stagedSettings.selectionBorderThickness) + L"px";
    D2D1_RECT_F brdValR = D2D1::RectF(254.0f, 566.0f, 308.0f, 584.0f);
    m_renderTarget->DrawText(brdStr.c_str(), static_cast<UINT32>(brdStr.size()), m_itemFormat.Get(), brdValR, m_textSecondaryBrush.Get());

    D2D1_RECT_F posLblR = D2D1::RectF(28.0f, 598.0f, 122.0f, 616.0f);
    m_renderTarget->DrawText(L"Toolbar Position:", 17, m_itemFormat.Get(), posLblR, m_textPrimaryBrush.Get());

    const WCHAR* posText = (m_stagedSettings.toolbarPosition == L"Bottom") ? L"Bottom Center" : L"Top Center";
    D2D1_RECT_F comboPos = D2D1::RectF(124.0f, 592.0f, 308.0f, 620.0f);
    drawComboBox(comboPos, posText, m_openDropdown == DROPDOWN_TOOLBAR_POS, m_hoverId == ID_TOOLBAR_POS_COMBO);

    D2D1_RECT_F toolsHdrR = D2D1::RectF(28.0f, 632.0f, 308.0f, 650.0f);
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

    drawToolCheck(ID_TOOL_DRAW,    D2D1::RectF(28.0f,  656.0f, 124.0f, 674.0f), L"Draw Tools", isToolVisible(L"Pen") && isToolVisible(L"Draw"));
    drawToolCheck(ID_TOOL_SHAPES,  D2D1::RectF(128.0f, 656.0f, 216.0f, 674.0f), L"Shapes",     isToolVisible(L"Shapes"));
    drawToolCheck(ID_TOOL_BLUR,    D2D1::RectF(220.0f, 656.0f, 308.0f, 674.0f), L"Blur",       isToolVisible(L"Blur"));
    drawToolCheck(ID_TOOL_TEXT,    D2D1::RectF(28.0f,  682.0f, 124.0f, 700.0f), L"Text",       isToolVisible(L"Text"));
    drawToolCheck(ID_TOOL_COUNTER, D2D1::RectF(128.0f, 682.0f, 216.0f, 700.0f), L"Counter",    isToolVisible(L"Counter"));

    m_renderTarget->SetTransform(curTransform);
    m_renderTarget->PopAxisAlignedClip();

    // ── Scrollbar Indicator ──
    if (m_maxScrollY > 0.0f) {
        float trackH = 448.0f;
        float thumbH = std::clamp(trackH * (460.0f / 730.0f), 30.0f, trackH);
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
        float comboTop = 46.0f - m_scrollY + 550.0f;
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

static int HitTest(float x, float y, float scrollY) {
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

        if (x >= 28.0f && x <= 312.0f && cy >= 72.0f && cy <= 104.0f) return ID_THEME_COMBO;

        if (x >= 232.0f && x <= 304.0f && cy >= 198.0f && cy <= 224.0f) return ID_HOTKEY_RECORD_BTN;
        if (x >= 28.0f && x <= 312.0f && cy >= 186.0f && cy <= 236.0f) return ID_HOTKEY_RECORD_BOX;
        if (x >= 146.0f && x <= 312.0f && cy >= 240.0f && cy <= 262.0f) return ID_HOTKEY_RESET;

        if (x >= 28.0f && x <= 246.0f && cy >= 328.0f && cy <= 356.0f) return ID_VOICE_KEYWORD_BOX;
        if (x >= 252.0f && x <= 312.0f && cy >= 328.0f && cy <= 356.0f) return ID_VOICE_KEYWORD_RESET;
        if (x >= 16.0f && x <= 324.0f && cy >= 278.0f && cy <= 388.0f) return ID_VOICE_ACTIVATION_CARD;

        if (x >= 16.0f && x <= 324.0f && cy >= 398.0f && cy <= 466.0f) return ID_DEBUG_LOGGER_CARD;

        if (x >= 120.0f && x <= 256.0f && cy >= 528.0f && cy <= 558.0f) return ID_SLIDER_DIMMING;
        if (x >= 120.0f && x <= 256.0f && cy >= 560.0f && cy <= 590.0f) return ID_SLIDER_BORDER;
        if (x >= 124.0f && x <= 308.0f && cy >= 592.0f && cy <= 620.0f) return ID_TOOLBAR_POS_COMBO;

        if (cy >= 650.0f && cy <= 676.0f) {
            if (x >= 28.0f && x < 126.0f)  return ID_TOOL_DRAW;
            if (x >= 126.0f && x < 218.0f) return ID_TOOL_SHAPES;
            if (x >= 218.0f && x <= 312.0f) return ID_TOOL_BLUR;
        }
        if (cy >= 678.0f && cy <= 704.0f) {
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
        float comboTop = 46.0f - m_scrollY + 592.0f;
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

    int id = HitTest(x, y, m_scrollY);

    if (id != ID_VOICE_KEYWORD_BOX && id != ID_VOICE_KEYWORD_RESET && m_editingVoiceKeyword) {
        StopEditingVoiceKeyword();
    }

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

    case ID_VOICE_ACTIVATION_CARD:
        m_stagedSettings.enableVoiceActivation = !m_stagedSettings.enableVoiceActivation;
        StopEditingVoiceKeyword();
        break;

    case ID_VOICE_KEYWORD_BOX:
        StartEditingVoiceKeyword();
        break;

    case ID_VOICE_KEYWORD_RESET:
        m_stagedSettings.voiceKeyword = L"Snippy snip it";
        StopEditingVoiceKeyword();
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
        const WCHAR* name = (id == ID_TOOL_SHAPES) ? L"Shapes" :
                            (id == ID_TOOL_BLUR)   ? L"Blur" :
                            (id == ID_TOOL_TEXT)   ? L"Text" : L"Counter";
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
        float comboTop = 46.0f - m_scrollY + 592.0f;
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

    int newHover = HitTest(x, y, m_scrollY);
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

void SettingsWindow::StartEditingVoiceKeyword() noexcept {
    m_editingVoiceKeyword = true;
    m_caretVisible = true;
    m_lastVoiceInputTime = GetTickCount64();
    if (m_hwnd) {
        SetTimer(m_hwnd, kTimerCaretBlink, GetCaretBlinkInterval(), nullptr);
    }
    if (m_isRecording) {
        StopRecording();
    }
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void SettingsWindow::StopEditingVoiceKeyword() noexcept {
    if (m_editingVoiceKeyword) {
        m_editingVoiceKeyword = false;
        m_caretVisible = false;
        if (m_hwnd) {
            KillTimer(m_hwnd, kTimerCaretBlink);
        }
        if (m_stagedSettings.voiceKeyword.empty()) {
            m_stagedSettings.voiceKeyword = L"Snippy snip it";
        }
        InvalidateRect(m_hwnd, nullptr, FALSE);
    }
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
    if (m_editingVoiceKeyword) {
        if (vk == VK_RETURN || vk == VK_ESCAPE) {
            StopEditingVoiceKeyword();
            return;
        }
        if (vk == VK_BACK) {
            if (!m_stagedSettings.voiceKeyword.empty()) {
                m_stagedSettings.voiceKeyword.pop_back();
                m_caretVisible = true;
                m_lastVoiceInputTime = GetTickCount64();
                if (m_hwnd) {
                    SetTimer(m_hwnd, kTimerCaretBlink, GetCaretBlinkInterval(), nullptr);
                }
                InvalidateRect(m_hwnd, nullptr, FALSE);
            }
            return;
        }
        return;
    }

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
    if (!m_editingVoiceKeyword) return;
    if (ch >= 32 && ch != 127) {
        if (m_stagedSettings.voiceKeyword.size() < 30) {
            m_stagedSettings.voiceKeyword.push_back(static_cast<wchar_t>(ch));
            m_caretVisible = true;
            m_lastVoiceInputTime = GetTickCount64();
            if (m_hwnd) {
                SetTimer(m_hwnd, kTimerCaretBlink, GetCaretBlinkInterval(), nullptr);
            }
            InvalidateRect(m_hwnd, nullptr, FALSE);
        }
    }
}

void SettingsWindow::SaveAndApply() noexcept {
    if (m_isRecording) StopRecording();
    StopEditingVoiceKeyword();

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
    StopEditingVoiceKeyword();
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
        if (m_editingVoiceKeyword) {
            OnChar(wParam);
            return 0;
        }
        break;

    case WM_KEYDOWN:
        if (m_editingVoiceKeyword) {
            OnKeyDown(wParam);
            return 0;
        }
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
        if (m_editingVoiceKeyword) {
            StopEditingVoiceKeyword();
        }
        break;

    case WM_TIMER:
        if (wParam == kTimerSaveFeedback) {
            KillTimer(m_hwnd, kTimerSaveFeedback);
            m_saveFeedback = false;
            Hide();
            return 0;
        }
        if (wParam == kTimerCaretBlink) {
            if (!m_editingVoiceKeyword) {
                KillTimer(m_hwnd, kTimerCaretBlink);
                return 0;
            }
            ULONGLONG now = GetTickCount64();
            UINT blinkInterval = GetCaretBlinkInterval();
            if (now - m_lastVoiceInputTime < blinkInterval) {
                if (!m_caretVisible) {
                    m_caretVisible = true;
                    InvalidateRect(m_hwnd, nullptr, FALSE);
                }
            } else {
                m_caretVisible = !m_caretVisible;
                InvalidateRect(m_hwnd, nullptr, FALSE);
            }
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
