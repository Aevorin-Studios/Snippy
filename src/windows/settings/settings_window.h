#pragma once

#include <windows.h>
#include <d2d1.h>
#include <dwrite.h>
#include <wrl/client.h>
#include <functional>
#include <string>
#include "settings_manager.h"

using Microsoft::WRL::ComPtr;

namespace snippy {

enum OpenDropdown {
    DROPDOWN_NONE = 0,
    DROPDOWN_THEME,
    DROPDOWN_TOOLBAR_POS
};

class SettingsWindow {
public:
    SettingsWindow() noexcept;
    ~SettingsWindow() noexcept;

    bool Initialize(HINSTANCE hInstance) noexcept;
    void Show() noexcept;
    void Hide() noexcept;
    void Shutdown() noexcept;

    [[nodiscard]] bool IsVisible() const noexcept;
    [[nodiscard]] HWND Hwnd() const noexcept { return m_hwnd; }

    static constexpr float kBaseWidth  = 340.0f;
    static constexpr float kBaseHeight = 556.0f;

    std::function<void()> OnSettingsChanged;

private:
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) noexcept;
    LRESULT HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam) noexcept;

    bool CreateD2DResources() noexcept;
    void DiscardD2DResources() noexcept;
    void Render() noexcept;

    void OnLButtonDown(POINT pt) noexcept;
    void OnMouseMove(POINT pt) noexcept;
    void OnLButtonUp(POINT pt) noexcept;
    void OnKeyDown(WPARAM vk) noexcept;
    void OnKeyUp(WPARAM vk) noexcept;
    void OnChar(WPARAM ch) noexcept;
    void OnMouseWheel(short delta) noexcept;

    void StartRecording() noexcept;
    void StopRecording() noexcept;
    void StartEditingVoiceKeyword() noexcept;
    void StopEditingVoiceKeyword() noexcept;
    void SaveAndApply() noexcept;
    void ResetToDefaults() noexcept;
    void ApplyThemePalette(const std::wstring& themeSetting) noexcept;

    HWND      m_hwnd      = nullptr;
    HINSTANCE m_hInstance = nullptr;

    ComPtr<ID2D1HwndRenderTarget> m_renderTarget;
    ComPtr<ID2D1SolidColorBrush>  m_bgBrush;
    ComPtr<ID2D1SolidColorBrush>  m_cardBgBrush;
    ComPtr<ID2D1SolidColorBrush>  m_cardBorderBrush;
    ComPtr<ID2D1SolidColorBrush>  m_subtleBgBrush;
    ComPtr<ID2D1SolidColorBrush>  m_subtleBorderBrush;
    ComPtr<ID2D1SolidColorBrush>  m_inputBgBrush;
    ComPtr<ID2D1SolidColorBrush>  m_inputBorderBrush;
    ComPtr<ID2D1SolidColorBrush>  m_dropdownBgBrush;
    ComPtr<ID2D1SolidColorBrush>  m_dropdownHoverBrush;
    ComPtr<ID2D1SolidColorBrush>  m_accentBrush;
    ComPtr<ID2D1SolidColorBrush>  m_accentHoverBrush;
    ComPtr<ID2D1SolidColorBrush>  m_btnSecondaryBgBrush;
    ComPtr<ID2D1SolidColorBrush>  m_btnSecondaryBorderBrush;
    ComPtr<ID2D1SolidColorBrush>  m_btnSecondaryHoverBrush;
    ComPtr<ID2D1SolidColorBrush>  m_textPrimaryBrush;
    ComPtr<ID2D1SolidColorBrush>  m_textSecondaryBrush;
    ComPtr<ID2D1SolidColorBrush>  m_greenBrush;
    ComPtr<ID2D1SolidColorBrush>  m_dangerBrush;
    ComPtr<ID2D1SolidColorBrush>  m_scrollThumbBrush;
    ComPtr<ID2D1SolidColorBrush>  m_sliderTrackBgBrush;
    ComPtr<ID2D1SolidColorBrush>  m_sliderThumbBrush;
    ComPtr<ID2D1StrokeStyle>      m_roundStroke;

    ComPtr<IDWriteTextFormat>     m_titleFormat;
    ComPtr<IDWriteTextFormat>     m_cardTitleFormat;
    ComPtr<IDWriteTextFormat>     m_descFormat;
    ComPtr<IDWriteTextFormat>     m_itemFormat;
    ComPtr<IDWriteTextFormat>     m_hotkeyBoldFormat;
    ComPtr<IDWriteTextFormat>     m_btnFormat;
    ComPtr<IDWriteTextFormat>     m_btnPrimaryFormat;

    AppSettings   m_stagedSettings{};
    bool          m_isRecording   = false;
    std::wstring  m_recordingText = L"Press shortcut keys...";
    bool          m_editingVoiceKeyword = false;
    bool          m_caretVisible        = false;
    ULONGLONG     m_lastVoiceInputTime  = 0;
    bool          m_draggingDim   = false;
    bool          m_draggingBrd   = false;
    bool          m_saveFeedback  = false;
    UINT_PTR      m_feedbackTimer = 0;

    std::wstring  m_statusError;

    // Scrolling & Dropdowns
    float         m_scrollY       = 0.0f;
    float         m_maxScrollY    = 0.0f;
    OpenDropdown  m_openDropdown  = DROPDOWN_NONE;
    int           m_hoverDropdownItem = -1;

    // Hover detection
    int           m_hoverId       = 0;

    float         m_dpiScale      = 1.0f;
};

extern SettingsWindow g_settingsWindow;

} // namespace snippy
