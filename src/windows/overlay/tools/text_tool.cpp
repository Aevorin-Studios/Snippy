#include "text_tool.h"
#include <algorithm>
#include <cmath>
#include <vector>

using namespace D2D1;

namespace snippy {

static void SetClipboardWideText(const std::wstring& str, HWND hwnd) {
    if (str.empty()) return;
    if (!OpenClipboard(hwnd)) return;
    EmptyClipboard();
    size_t bytes = (str.size() + 1) * sizeof(wchar_t);
    HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (hMem) {
        void* dst = GlobalLock(hMem);
        if (dst) {
            memcpy(dst, str.c_str(), bytes);
            GlobalUnlock(hMem);
            SetClipboardData(CF_UNICODETEXT, hMem);
        } else {
            GlobalFree(hMem);
        }
    }
    CloseClipboard();
}

static std::wstring GetClipboardWideText(HWND hwnd) {
    std::wstring result;
    if (OpenClipboard(hwnd)) {
        HANDLE hData = GetClipboardData(CF_UNICODETEXT);
        if (hData) {
            const wchar_t* pText = static_cast<const wchar_t*>(GlobalLock(hData));
            if (pText) {
                for (const wchar_t* p = pText; *p; ++p) {
                    if (*p != L'\r') result += *p;
                }
                GlobalUnlock(hData);
            }
        }
        CloseClipboard();
    }
    return result;
}

D2D1_RECT_F TextTool_GetBounds(D2D1_POINT_2F origin, const std::wstring& text, float fontSize,
                               const std::wstring& fontFamily, IDWriteFactory* dwriteFactory,
                               float scaleX, float scaleY) {
    float width = 0.0f;
    float height = fontSize * 1.35f;

    if (dwriteFactory) {
        Microsoft::WRL::ComPtr<IDWriteTextFormat> fmt;
        if (SUCCEEDED(dwriteFactory->CreateTextFormat(
                fontFamily.c_str(), nullptr,
                DWRITE_FONT_WEIGHT_BOLD, DWRITE_FONT_STYLE_NORMAL,
                DWRITE_FONT_STRETCH_NORMAL, fontSize, L"en-us", &fmt))) {
            fmt->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
            const std::wstring measure = text.empty() ? L" " : text;
            Microsoft::WRL::ComPtr<IDWriteTextLayout> layout;
            if (SUCCEEDED(dwriteFactory->CreateTextLayout(
                    measure.c_str(), static_cast<UINT32>(measure.size()),
                    fmt.Get(), 10000.0f, 10000.0f, &layout))) {
                DWRITE_TEXT_METRICS metrics{};
                if (SUCCEEDED(layout->GetMetrics(&metrics))) {
                    width = text.empty() ? 0.0f : metrics.widthIncludingTrailingWhitespace;
                    height = std::max(height, metrics.height);
                }
            }
        }
    }
    if (width <= 0.0f && !text.empty()) {
        width = static_cast<float>(text.size()) * fontSize * 0.65f;
    }

    const float padX = 4.0f;
    const float padY = 2.0f;
    const float minWidth = 60.0f;
    float boxWidth = std::max(minWidth, width + padX * 2.0f) * scaleX;
    float boxHeight = (height + padY * 2.0f) * scaleY;

    return RectF(origin.x, origin.y, origin.x + boxWidth, origin.y + boxHeight);
}

D2D1_RECT_F TextTool_GetBounds(const TextToolState& s, IDWriteFactory* dwriteFactory) {
    return TextTool_GetBounds(s.origin, s.text, s.fontSize, s.fontFamily, dwriteFactory, s.scaleX, s.scaleY);
}

bool TextTool_OnClick(TextToolState& s, D2D1_POINT_2F pt, IDWriteFactory* dwriteFactory) {
    if (!s.editing) return false;
    D2D1_RECT_F b = TextTool_GetBounds(s, dwriteFactory);
    if (pt.x < b.left || pt.x > b.right || pt.y < b.top || pt.y > b.bottom) {
        return false;
    }

    s.lastInteractionTime = GetTickCount64();
    if (s.text.empty() || !dwriteFactory) {
        s.caretPos = 0;
        s.selStart = s.selEnd = 0;
        return true;
    }

    Microsoft::WRL::ComPtr<IDWriteTextFormat> fmt;
    if (SUCCEEDED(dwriteFactory->CreateTextFormat(
            s.fontFamily.c_str(), nullptr,
            DWRITE_FONT_WEIGHT_BOLD, DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL, s.fontSize, L"en-us", &fmt))) {
        fmt->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        Microsoft::WRL::ComPtr<IDWriteTextLayout> layout;
        if (SUCCEEDED(dwriteFactory->CreateTextLayout(
                s.text.c_str(), static_cast<UINT32>(s.text.size()),
                fmt.Get(), 10000.0f, 10000.0f, &layout))) {
            float sX = (s.scaleX > 0.001f) ? s.scaleX : 1.0f;
            float sY = (s.scaleY > 0.001f) ? s.scaleY : 1.0f;
            float relX = (pt.x - s.origin.x) / sX - 4.0f;
            float relY = (pt.y - s.origin.y) / sY - 2.0f;
            BOOL isTrailingHit = FALSE, isInside = FALSE;
            DWRITE_HIT_TEST_METRICS htm{};
            if (SUCCEEDED(layout->HitTestPoint(relX, relY, &isTrailingHit, &isInside, &htm))) {
                s.caretPos = isTrailingHit ? (htm.textPosition + htm.length) : htm.textPosition;
                s.selStart = s.selEnd = s.caretPos;
                return true;
            }
        }
    }

    s.caretPos = s.text.size();
    s.selStart = s.selEnd = s.caretPos;
    return true;
}

void TextTool_Start(TextToolState& s, D2D1_POINT_2F pt, ColorF color, float sz,
                    const std::wstring& family, const std::wstring& initialText,
                    float scaleX, float scaleY) {
    s.origin = pt;
    s.text = initialText;
    s.color = color;
    s.fontSize = sz > 8.0f ? sz : 18.0f;
    s.fontFamily = family.empty() ? L"Segoe UI" : family;
    s.scaleX = (scaleX > 0.01f) ? scaleX : 1.0f;
    s.scaleY = (scaleY > 0.01f) ? scaleY : 1.0f;
    s.active = true;
    s.editing = true;
    s.caretPos = initialText.size();
    s.selStart = 0;
    s.selEnd = 0;
    s.lastInteractionTime = GetTickCount64();
}

bool TextTool_OnKeyDown(TextToolState& s, WPARAM vk, bool shift, bool ctrl, HWND hwnd,
                        Annotation& out, IDWriteFactory* dwriteFactory) {
    if (!s.editing) return false;
    s.lastInteractionTime = GetTickCount64();

    if (vk == VK_ESCAPE) {
        TextTool_Cancel(s);
        return false;
    }

    if (vk == VK_RETURN) {
        if (shift) {
            if (s.selStart != s.selEnd) {
                size_t start = std::min(s.selStart, s.selEnd);
                size_t end = std::max(s.selStart, s.selEnd);
                s.text.erase(start, end - start);
                s.caretPos = start;
                s.selStart = s.selEnd = start;
            }
            if (s.caretPos > s.text.size()) s.caretPos = s.text.size();
            s.text.insert(s.caretPos, 1, L'\n');
            s.caretPos++;
            s.selStart = s.selEnd = s.caretPos;
            return false;
        } else {
            return TextTool_Commit(s, out);
        }
    }

    if (ctrl) {
        if (vk == 'A' || vk == 'a') {
            s.selStart = 0;
            s.selEnd = s.text.size();
            s.caretPos = s.text.size();
            return false;
        }
        if (vk == 'C' || vk == 'c') {
            std::wstring copyText;
            if (s.selStart != s.selEnd) {
                size_t start = std::min(s.selStart, s.selEnd);
                size_t count = std::max(s.selStart, s.selEnd) - start;
                copyText = s.text.substr(start, count);
            } else {
                copyText = s.text;
            }
            if (!copyText.empty()) {
                SetClipboardWideText(copyText, hwnd);
            }
            return false;
        }
        if (vk == 'X' || vk == 'x') {
            if (s.selStart != s.selEnd) {
                size_t start = std::min(s.selStart, s.selEnd);
                size_t count = std::max(s.selStart, s.selEnd) - start;
                std::wstring copyText = s.text.substr(start, count);
                SetClipboardWideText(copyText, hwnd);
                s.text.erase(start, count);
                s.caretPos = start;
                s.selStart = s.selEnd = start;
            }
            return false;
        }
        if (vk == 'V' || vk == 'v') {
            std::wstring clipText = GetClipboardWideText(hwnd);
            if (!clipText.empty()) {
                if (s.selStart != s.selEnd) {
                    size_t start = std::min(s.selStart, s.selEnd);
                    size_t count = std::max(s.selStart, s.selEnd) - start;
                    s.text.erase(start, count);
                    s.caretPos = start;
                    s.selStart = s.selEnd = start;
                }
                if (s.caretPos > s.text.size()) s.caretPos = s.text.size();
                s.text.insert(s.caretPos, clipText);
                s.caretPos += clipText.size();
                s.selStart = s.selEnd = s.caretPos;
            }
            return false;
        }
        return false;
    }

    if (vk == VK_BACK) {
        if (s.selStart != s.selEnd) {
            size_t start = std::min(s.selStart, s.selEnd);
            size_t count = std::max(s.selStart, s.selEnd) - start;
            s.text.erase(start, count);
            s.caretPos = start;
            s.selStart = s.selEnd = start;
        } else if (s.caretPos > 0 && !s.text.empty()) {
            if (s.caretPos > s.text.size()) s.caretPos = s.text.size();
            s.text.erase(s.caretPos - 1, 1);
            s.caretPos--;
            s.selStart = s.selEnd = s.caretPos;
        }
        return false;
    }

    if (vk == VK_DELETE) {
        if (s.selStart != s.selEnd) {
            size_t start = std::min(s.selStart, s.selEnd);
            size_t count = std::max(s.selStart, s.selEnd) - start;
            s.text.erase(start, count);
            s.caretPos = start;
            s.selStart = s.selEnd = start;
        } else if (s.caretPos < s.text.size()) {
            s.text.erase(s.caretPos, 1);
            s.selStart = s.selEnd = s.caretPos;
        }
        return false;
    }

    if (vk == VK_LEFT) {
        if (shift) {
            if (s.selStart == s.selEnd) s.selStart = s.caretPos;
            if (s.caretPos > 0) s.caretPos--;
            s.selEnd = s.caretPos;
        } else {
            if (s.selStart != s.selEnd) {
                s.caretPos = std::min(s.selStart, s.selEnd);
                s.selStart = s.selEnd = s.caretPos;
            } else if (s.caretPos > 0) {
                s.caretPos--;
                s.selStart = s.selEnd = s.caretPos;
            }
        }
        return false;
    }

    if (vk == VK_RIGHT) {
        if (shift) {
            if (s.selStart == s.selEnd) s.selStart = s.caretPos;
            if (s.caretPos < s.text.size()) s.caretPos++;
            s.selEnd = s.caretPos;
        } else {
            if (s.selStart != s.selEnd) {
                s.caretPos = std::max(s.selStart, s.selEnd);
                s.selStart = s.selEnd = s.caretPos;
            } else if (s.caretPos < s.text.size()) {
                s.caretPos++;
                s.selStart = s.selEnd = s.caretPos;
            }
        }
        return false;
    }

    if (vk == VK_HOME) {
        if (shift) {
            if (s.selStart == s.selEnd) s.selStart = s.caretPos;
            s.caretPos = 0;
            s.selEnd = s.caretPos;
        } else {
            s.caretPos = 0;
            s.selStart = s.selEnd = 0;
        }
        return false;
    }

    if (vk == VK_END) {
        if (shift) {
            if (s.selStart == s.selEnd) s.selStart = s.caretPos;
            s.caretPos = s.text.size();
            s.selEnd = s.caretPos;
        } else {
            s.caretPos = s.text.size();
            s.selStart = s.selEnd = s.caretPos;
        }
        return false;
    }

    return false;
}

bool TextTool_OnChar(TextToolState& s, wchar_t ch, Annotation& out) {
    if (!s.editing) return false;
    s.lastInteractionTime = GetTickCount64();

    if (ch == L'\r' || ch == L'\n' || ch == L'\b' || ch == 0x1B ||
        ch == 0x01 || ch == 0x03 || ch == 0x16 || ch == 0x18) {
        return false;
    }

    if (ch >= 0x20 || ch == L'\t') {
        if (s.selStart != s.selEnd) {
            size_t start = std::min(s.selStart, s.selEnd);
            size_t count = std::max(s.selStart, s.selEnd) - start;
            s.text.erase(start, count);
            s.caretPos = start;
            s.selStart = s.selEnd = start;
        }
        if (s.caretPos > s.text.size()) s.caretPos = s.text.size();
        s.text.insert(s.caretPos, 1, ch);
        s.caretPos++;
        s.selStart = s.selEnd = s.caretPos;
    }
    return false;
}

bool TextTool_Commit(TextToolState& s, Annotation& out) {
    if (!s.editing) return false;
    bool hadText = !s.text.empty();
    if (hadText) {
        TextAnnotation ta{};
        ta.origin = s.origin;
        ta.text = s.text;
        ta.color = s.color;
        ta.fontSize = s.fontSize;
        ta.fontFamily = s.fontFamily;
        ta.scaleX = s.scaleX;
        ta.scaleY = s.scaleY;
        out = Annotation{ AnnotationType::Text, ta };
    }
    s.text = L"";
    s.editing = false;
    s.active = false;
    s.scaleX = 1.0f;
    s.scaleY = 1.0f;
    s.caretPos = 0;
    s.selStart = s.selEnd = 0;
    return hadText;
}

void TextTool_Cancel(TextToolState& s) {
    s.text = L"";
    s.editing = false;
    s.active = false;
    s.scaleX = 1.0f;
    s.scaleY = 1.0f;
    s.caretPos = 0;
    s.selStart = s.selEnd = 0;
}

void TextTool_Draw(ID2D1DeviceContext* dc, const TextToolState& s,
                   IDWriteFactory* dwriteFactory,
                   ID2D1SolidColorBrush* textBrush,
                   ID2D1SolidColorBrush* borderBrush) {
    if (!s.editing) return;
    if (!dwriteFactory) return;

    Microsoft::WRL::ComPtr<IDWriteTextFormat> fmt;
    if (FAILED(dwriteFactory->CreateTextFormat(
            s.fontFamily.c_str(), nullptr,
            DWRITE_FONT_WEIGHT_BOLD, DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL, s.fontSize, L"en-us", &fmt))) {
        return;
    }
    fmt->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);

    const std::wstring measureText = s.text.empty() ? L" " : s.text;
    Microsoft::WRL::ComPtr<IDWriteTextLayout> layout;
    if (FAILED(dwriteFactory->CreateTextLayout(
            measureText.c_str(), static_cast<UINT32>(measureText.size()),
            fmt.Get(), 10000.0f, 10000.0f, &layout))) {
        return;
    }

    DWRITE_TEXT_METRICS metrics{};
    float unscaledW = 0.0f;
    float unscaledH = s.fontSize * 1.35f;
    if (SUCCEEDED(layout->GetMetrics(&metrics))) {
        unscaledW = s.text.empty() ? 0.0f : metrics.widthIncludingTrailingWhitespace;
        unscaledH = std::max(unscaledH, metrics.height);
    }
    const float padX = 4.0f;
    const float padY = 2.0f;
    const float minWidth = 60.0f;
    float localBoxW = std::max(minWidth, unscaledW + padX * 2.0f);
    float localBoxH = unscaledH + padY * 2.0f;

    D2D1_MATRIX_3X2_F oldTrans;
    dc->GetTransform(&oldTrans);
    dc->SetTransform(
        D2D1::Matrix3x2F::Scale(s.scaleX, s.scaleY, D2D1::Point2F(0, 0)) *
        D2D1::Matrix3x2F::Translation(s.origin.x, s.origin.y) *
        oldTrans
    );

    if (borderBrush) {
        D2D1_ROUNDED_RECT roundedBox = RoundedRect(D2D1::RectF(0.0f, 0.0f, localBoxW, localBoxH), 4.0f, 4.0f);
        dc->DrawRoundedRectangle(roundedBox, borderBrush, 1.5f);
    }

    if (s.selStart != s.selEnd && !s.text.empty()) {
        size_t selMin = std::min(s.selStart, s.selEnd);
        size_t selMax = std::max(s.selStart, s.selEnd);
        UINT32 count = 0;
        layout->HitTestTextRange(static_cast<UINT32>(selMin),
                                 static_cast<UINT32>(selMax - selMin),
                                 0, 0, nullptr, 0, &count);
        if (count > 0) {
            std::vector<DWRITE_HIT_TEST_METRICS> htm(count);
            if (SUCCEEDED(layout->HitTestTextRange(static_cast<UINT32>(selMin),
                                                   static_cast<UINT32>(selMax - selMin),
                                                   0, 0, htm.data(), count, &count))) {
                Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> selBrush;
                D2D1_COLOR_F accentCol = borderBrush ? borderBrush->GetColor() : D2D1::ColorF(0.0f, 0.471f, 0.831f, 1.0f);
                accentCol.a = 0.35f;
                if (SUCCEEDED(dc->CreateSolidColorBrush(accentCol, &selBrush))) {
                    for (UINT32 i = 0; i < count; ++i) {
                        D2D1_RECT_F r = RectF(
                            padX + htm[i].left,
                            padY + htm[i].top,
                            padX + htm[i].left + htm[i].width,
                            padY + htm[i].top + htm[i].height
                        );
                        dc->FillRectangle(r, selBrush.Get());
                    }
                }
            }
        }
    }

    if (!s.text.empty() && textBrush) {
        textBrush->SetColor(ToD2D(s.color));
        dc->DrawTextLayout(D2D1::Point2F(padX, padY), layout.Get(), textBrush);
    }

    ULONGLONG now = GetTickCount64();
    bool isBlinkOn = ((now - s.lastInteractionTime) < 500) || (((now / 500) % 2) == 0);
    if (isBlinkOn && textBrush) {
        float caretX = 0.0f;
        float caretY = 0.0f;
        float caretH = s.fontSize * 1.25f;

        if (s.text.empty()) {
            caretX = 0.0f;
            caretY = 0.0f;
            DWRITE_TEXT_METRICS tm{};
            if (SUCCEEDED(layout->GetMetrics(&tm))) {
                caretH = tm.height;
            }
        } else {
            if (s.caretPos == 0) {
                BOOL isTrailing = FALSE;
                DWRITE_HIT_TEST_METRICS htm{};
                if (SUCCEEDED(layout->HitTestTextPosition(0, isTrailing, &caretX, &caretY, &htm))) {
                    caretH = htm.height;
                }
            } else {
                UINT32 hitPos = static_cast<UINT32>(std::min(s.caretPos - 1, s.text.size() - 1));
                BOOL isTrailing = TRUE;
                DWRITE_HIT_TEST_METRICS htm{};
                if (SUCCEEDED(layout->HitTestTextPosition(hitPos, isTrailing, &caretX, &caretY, &htm))) {
                    caretH = htm.height;
                }
            }
        }

        textBrush->SetColor(ToD2D(s.color));
        D2D1_POINT_2F p0 = Point2F(padX + caretX, padY + caretY);
        D2D1_POINT_2F p1 = Point2F(padX + caretX, padY + caretY + caretH);
        dc->DrawLine(p0, p1, textBrush, 1.5f);
    }

    dc->SetTransform(oldTrans);
}

} // namespace snippy
