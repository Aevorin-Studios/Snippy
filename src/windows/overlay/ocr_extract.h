#pragma once

#include <windows.h>
#include <d2d1_3.h>
#include <cstdint>
#include <string>
#include <vector>
#include <functional>

namespace snippy {

struct OcrWordItem {
    std::wstring text;
    D2D1_RECT_F  bounds{}; // relative to the snippet
};

struct OcrCharItem {
    wchar_t     character{ 0 };
    D2D1_RECT_F bounds{}; // relative to the snippet
    int         lineIndex{ 0 };
    int         wordIndex{ 0 };
    int         charIndex{ 0 };
};

struct OcrLineItem {
    std::wstring text;
    D2D1_RECT_F  bounds{}; // relative to the snippet
};

struct OcrInlineResult {
    bool                     success = false;
    std::wstring             fullText;
    std::wstring             errorMessage;
    std::vector<OcrLineItem> lines;
    std::vector<OcrWordItem> words;
    std::vector<OcrCharItem> chars;
};

// Async OCR function: processes packed BGRA8 snippet on a worker thread and invokes callback on completion.
void ExtractInlineTextAsync(std::vector<uint8_t> bgra, uint32_t width, uint32_t height,
                            std::function<void(OcrInlineResult)> callback) noexcept;

// Legacy / fallback async text extract directly to clipboard
void ExtractTextAsync(std::vector<uint8_t> bgra, uint32_t width, uint32_t height,
                      HWND clipboardOwner) noexcept;

} // namespace snippy
