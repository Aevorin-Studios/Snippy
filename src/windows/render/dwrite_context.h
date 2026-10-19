#pragma once

//
// dwrite_context.h — DirectWrite factory + pre-loaded font resources.
//
// All fonts loaded once at startup. Never loaded during the screenshot hot path.
//

#include <windows.h>
#include <dwrite_3.h>
#include <wrl/client.h>

using Microsoft::WRL::ComPtr;

namespace snippy {

class DWriteContext {
public:
    bool Initialize() noexcept;
    void Shutdown() noexcept;

    [[nodiscard]] IDWriteFactory3*    Factory()     const noexcept { return m_factory.Get(); }
    [[nodiscard]] IDWriteTextFormat*  UIFont()      const noexcept { return m_uiFont.Get(); }
    [[nodiscard]] IDWriteTextFormat*  UIFontSmall()       const noexcept { return m_uiFontSmall.Get(); }
    [[nodiscard]] IDWriteTextFormat*  UIFontSmallCenter() const noexcept { return m_uiFontSmallCenter.Get(); }
    [[nodiscard]] IDWriteTextFormat*  UIFontBold()        const noexcept { return m_uiFontBold.Get(); }
    [[nodiscard]] IDWriteTextFormat*  MonoFont()          const noexcept { return m_monoFont.Get(); }
    [[nodiscard]] IDWriteTextFormat*  IconFont()          const noexcept { return m_iconFont.Get(); }
    [[nodiscard]] IDWriteTextFormat*  IconFont15()        const noexcept { return m_iconFont15.Get(); }
    [[nodiscard]] IDWriteTextFormat*  IconFont14()        const noexcept { return m_iconFont14.Get(); }
    [[nodiscard]] IDWriteTextFormat*  IconFontChevron()   const noexcept { return m_iconFont10.Get(); }

    [[nodiscard]] const WCHAR*        IconFontFamily()       const noexcept { return m_iconFontFamily; }
    [[nodiscard]] bool                IsWindows11OrGreater() const noexcept { return m_isWin11; }
    [[nodiscard]] bool                IsInitialized()        const noexcept { return m_initialized; }

private:
    bool CreateFont(const WCHAR* family, FLOAT size, DWRITE_FONT_WEIGHT weight,
                    IDWriteTextFormat** ppFormat) noexcept;

    ComPtr<IDWriteFactory3>    m_factory;
    ComPtr<IDWriteTextFormat>  m_uiFont;             // Segoe UI 12pt regular
    ComPtr<IDWriteTextFormat>  m_uiFontSmall;        // Segoe UI 10pt regular
    ComPtr<IDWriteTextFormat>  m_uiFontSmallCenter;  // Segoe UI 10pt regular centered
    ComPtr<IDWriteTextFormat>  m_uiFontBold;         // Segoe UI 12pt bold
    ComPtr<IDWriteTextFormat>  m_monoFont;     // Consolas 11pt regular (perf output)
    ComPtr<IDWriteTextFormat>  m_iconFont;     // Icon Font 16pt (Segoe Fluent Icons or Segoe MDL2 Assets)
    ComPtr<IDWriteTextFormat>  m_iconFont15;   // Icon Font 15pt
    ComPtr<IDWriteTextFormat>  m_iconFont14;   // Icon Font 14pt
    ComPtr<IDWriteTextFormat>  m_iconFont10;   // Icon Font 10pt
    const WCHAR*               m_iconFontFamily = L"Segoe MDL2 Assets";
    bool                       m_isWin11 = false;
    bool                       m_initialized = false;
};

extern DWriteContext g_dwrite;

} // namespace snippy
