#include "dwrite_context.h"

namespace snippy {

DWriteContext g_dwrite;

bool DWriteContext::Initialize() noexcept {
    if (m_initialized) return true;

    HRESULT hr = DWriteCreateFactory(
        DWRITE_FACTORY_TYPE_SHARED,
        __uuidof(IDWriteFactory3),
        reinterpret_cast<IUnknown**>(m_factory.GetAddressOf())
    );
    if (FAILED(hr)) return false;

    if (!CreateFont(L"Segoe UI",  12.5f, DWRITE_FONT_WEIGHT_NORMAL, &m_uiFont))      return false;
    m_uiFont->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    if (!CreateFont(L"Segoe UI",  10.0f, DWRITE_FONT_WEIGHT_NORMAL, &m_uiFontSmall)) return false;
    if (!CreateFont(L"Segoe UI",  10.0f, DWRITE_FONT_WEIGHT_NORMAL, &m_uiFontSmallCenter)) return false;
    m_uiFontSmallCenter->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    m_uiFontSmallCenter->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    if (!CreateFont(L"Segoe UI",  12.0f, DWRITE_FONT_WEIGHT_BOLD,   &m_uiFontBold))  return false;
    if (!CreateFont(L"Consolas",  11.0f, DWRITE_FONT_WEIGHT_NORMAL, &m_monoFont))    return false;

    // ── Determine Icon Font (Windows 11 Fluent Icons vs Windows 10 Segoe MDL2 Assets) ──
    DWORD buildNumber = 0;
    HMODULE hNtdll = GetModuleHandleW(L"ntdll.dll");
    if (hNtdll) {
        typedef LONG (WINAPI *RtlGetVersionFn)(RTL_OSVERSIONINFOW*);
        auto pRtlGetVersion = reinterpret_cast<RtlGetVersionFn>(GetProcAddress(hNtdll, "RtlGetVersion"));
        if (pRtlGetVersion) {
            RTL_OSVERSIONINFOW osInfo{};
            osInfo.dwOSVersionInfoSize = sizeof(osInfo);
            if (pRtlGetVersion(&osInfo) == 0) {
                buildNumber = osInfo.dwBuildNumber;
            }
        }
    }

    m_isWin11 = (buildNumber >= 22000);

    // On Windows 11 (build >= 22000), verify that "Segoe Fluent Icons" actually exists
    // in the DirectWrite system font collection.
    // On Windows 10 (or if Fluent Icons is missing), use "Segoe MDL2 Assets".
    bool useFluentIcons = false;
    if (m_isWin11) {
        ComPtr<IDWriteFontCollection> fontCollection;
        if (SUCCEEDED(m_factory->GetSystemFontCollection(&fontCollection))) {
            UINT32 index = 0;
            BOOL exists = FALSE;
            if (SUCCEEDED(fontCollection->FindFamilyName(L"Segoe Fluent Icons", &index, &exists))) {
                useFluentIcons = (exists == TRUE);
            }
        }
    }

    m_iconFontFamily = useFluentIcons ? L"Segoe Fluent Icons" : L"Segoe MDL2 Assets";
    DWRITE_FONT_WEIGHT iconWeight = useFluentIcons ? DWRITE_FONT_WEIGHT_MEDIUM : DWRITE_FONT_WEIGHT_NORMAL;

    if (!CreateFont(m_iconFontFamily, 16.0f, iconWeight, &m_iconFont)) {
        // Fallback to Segoe MDL2 Assets if Segoe Fluent Icons failed to create
        m_iconFontFamily = L"Segoe MDL2 Assets";
        iconWeight = DWRITE_FONT_WEIGHT_NORMAL;
        if (!CreateFont(m_iconFontFamily, 16.0f, iconWeight, &m_iconFont)) return false;
    }
    m_iconFont->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    m_iconFont->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

    if (!CreateFont(m_iconFontFamily, 15.0f, iconWeight, &m_iconFont15)) return false;
    m_iconFont15->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    m_iconFont15->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

    if (!CreateFont(m_iconFontFamily, 14.0f, iconWeight, &m_iconFont14)) return false;
    m_iconFont14->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    m_iconFont14->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

    if (!CreateFont(m_iconFontFamily, 10.0f, iconWeight, &m_iconFont10)) return false;
    m_iconFont10->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    m_iconFont10->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

    m_initialized = true;
    return true;
}

void DWriteContext::Shutdown() noexcept {
    m_iconFont10.Reset();
    m_iconFont14.Reset();
    m_iconFont15.Reset();
    m_iconFont.Reset();
    m_monoFont.Reset();
    m_uiFontBold.Reset();
    m_uiFontSmallCenter.Reset();
    m_uiFontSmall.Reset();
    m_uiFont.Reset();
    m_factory.Reset();
    m_initialized = false;
}

bool DWriteContext::CreateFont(
    const WCHAR*         family,
    FLOAT                size,
    DWRITE_FONT_WEIGHT   weight,
    IDWriteTextFormat**  ppFormat) noexcept
{
    HRESULT hr = m_factory->CreateTextFormat(
        family,
        nullptr,
        weight,
        DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL,
        size,
        L"en-us",
        ppFormat
    );
    return SUCCEEDED(hr);
}

} // namespace snippy
