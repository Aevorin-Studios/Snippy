#pragma once
#include "../annotation.h"
#include <d2d1_3.h>
#include <dwrite_3.h>
#include <string>

namespace snippy {

struct TextToolState {
    D2D1_POINT_2F origin{};
    std::wstring  text;
    bool          active = false;
    bool          editing = false;
    float         fontSize = 18.0f;
    ColorF        color{ 1.f, 1.f, 1.f, 1.f };
    std::wstring  fontFamily{ L"Segoe UI" };
    float         scaleX = 1.0f;
    float         scaleY = 1.0f;

    // Caret & selection
    size_t        caretPos = 0;
    size_t        selStart = 0;
    size_t        selEnd = 0;
    ULONGLONG     lastInteractionTime = 0;
};

// Hit test click inside text box. Returns true if pt is inside the text box bounds, and updates caret position.
bool TextTool_OnClick(TextToolState& s, D2D1_POINT_2F pt, IDWriteFactory* dwriteFactory = nullptr);

void TextTool_Start(TextToolState& s, D2D1_POINT_2F pt, ColorF color, float sz,
                    const std::wstring& family, const std::wstring& initialText = L"",
                    float scaleX = 1.0f, float scaleY = 1.0f);

// Handle key down (Enter, Escape, Backspace, Delete, Arrows, Home, End, Ctrl+A/C/V/X).
// Returns true if an annotation was committed (on unshifted Enter).
bool TextTool_OnKeyDown(TextToolState& s, WPARAM vk, bool shift, bool ctrl, HWND hwnd,
                        Annotation& out, IDWriteFactory* dwriteFactory = nullptr);

// Handle WM_CHAR for printable characters
bool TextTool_OnChar(TextToolState& s, wchar_t ch, Annotation& out);

bool TextTool_Commit(TextToolState& s, Annotation& out);
void TextTool_Cancel(TextToolState& s);

D2D1_RECT_F TextTool_GetBounds(const TextToolState& s, IDWriteFactory* dwriteFactory = nullptr);
D2D1_RECT_F TextTool_GetBounds(D2D1_POINT_2F origin, const std::wstring& text, float fontSize,
                               const std::wstring& fontFamily, IDWriteFactory* dwriteFactory = nullptr,
                               float scaleX = 1.0f, float scaleY = 1.0f);

void TextTool_Draw(ID2D1DeviceContext* dc, const TextToolState& s,
                   IDWriteFactory* dwriteFactory,
                   ID2D1SolidColorBrush* textBrush,
                   ID2D1SolidColorBrush* borderBrush);

} // namespace snippy
