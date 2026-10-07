#include "ocr_extract.h"
#include "../clipboard/clipboard.h"

#include <windows.h>
#include <inspectable.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Globalization.h>
#include <winrt/Windows.Graphics.Imaging.h>
#include <winrt/Windows.Media.Ocr.h>
#include <cstring>
#include <thread>
#include <algorithm>

// IMemoryBufferByteAccess — copy pixels into a WinRT SoftwareBitmap.
struct __declspec(uuid("5b0d3235-4dba-4d44-865e-8f1d0e4fd04d"))
IMemoryBufferByteAccess : ::IUnknown {
    virtual HRESULT __stdcall GetBuffer(BYTE** value, UINT32* capacity) = 0;
};

namespace snippy {

void ExtractInlineTextAsync(std::vector<uint8_t> bgra, uint32_t width, uint32_t height,
                            std::function<void(OcrInlineResult)> callback) noexcept {
    std::thread([pixels = std::move(bgra), width, height, cb = std::move(callback)]() mutable {
        OcrInlineResult res{};
        try {
            winrt::init_apartment(winrt::apartment_type::multi_threaded);

            using winrt::Windows::Graphics::Imaging::BitmapAlphaMode;
            using winrt::Windows::Graphics::Imaging::BitmapBufferAccessMode;
            using winrt::Windows::Graphics::Imaging::BitmapPixelFormat;
            using winrt::Windows::Graphics::Imaging::SoftwareBitmap;
            using winrt::Windows::Media::Ocr::OcrEngine;

            SoftwareBitmap bitmap(BitmapPixelFormat::Bgra8,
                                  static_cast<int32_t>(width),
                                  static_cast<int32_t>(height),
                                  BitmapAlphaMode::Ignore);

            {
                auto buffer = bitmap.LockBuffer(BitmapBufferAccessMode::Write);
                auto plane  = buffer.GetPlaneDescription(0);
                auto reference = buffer.CreateReference();
                auto access = reference.as<IMemoryBufferByteAccess>();
                BYTE* dst = nullptr;
                UINT32 cap = 0;
                if (FAILED(access->GetBuffer(&dst, &cap)) || !dst) {
                    reference.Close();
                    buffer.Close();
                    res.success = false;
                    res.errorMessage = L"Failed to access bitmap memory buffer.";
                    if (cb) cb(std::move(res));
                    return;
                }

                const uint32_t srcStride = width * 4;
                for (uint32_t y = 0; y < height; ++y) {
                    memcpy(dst + static_cast<size_t>(y) * plane.Stride,
                           pixels.data() + static_cast<size_t>(y) * srcStride,
                           srcStride);
                }

                reference.Close();
                buffer.Close();
            }

            auto engine = OcrEngine::TryCreateFromUserProfileLanguages();
            if (!engine) engine = OcrEngine::TryCreateFromLanguage(
                winrt::Windows::Globalization::Language{ L"en-US" });
            if (!engine) {
                auto available = OcrEngine::AvailableRecognizerLanguages();
                if (available.Size() > 0) {
                    engine = OcrEngine::TryCreateFromLanguage(available.GetAt(0));
                }
            }
            if (!engine) {
                res.success = false;
                res.errorMessage = L"No supported OCR language recognizer found on this system.";
                if (cb) cb(std::move(res));
                return;
            }

            auto ocrResult = engine.RecognizeAsync(bitmap).get();
            auto lines = ocrResult.Lines();

            int lineIdx = 0;
            int globalWordIdx = 0;
            for (const auto& line : lines) {
                lineIdx++;
                auto words = line.Words();
                if (words.Size() == 0) continue;

                float minX = 1e9f, minY = 1e9f;
                float maxX = -1e9f, maxY = -1e9f;
                std::wstring lineText;

                for (uint32_t w = 0; w < words.Size(); ++w) {
                    auto word = words.GetAt(w);
                    globalWordIdx++;
                    std::wstring wordStr = word.Text().c_str();
                    auto rect = word.BoundingRect();

                    if (w > 0) {
                        auto prevWord = words.GetAt(w - 1);
                        float gap = rect.X - (prevWord.BoundingRect().X + prevWord.BoundingRect().Width);
                        float avgCharW = prevWord.BoundingRect().Width / std::max(1.0f, static_cast<float>(prevWord.Text().size()));
                        if (gap >= avgCharW * 0.25f) {
                            lineText += L' ';
                        }
                    }
                    lineText += wordStr;

                    if (rect.X < minX) minX = rect.X;
                    if (rect.Y < minY) minY = rect.Y;
                    if (rect.X + rect.Width > maxX) maxX = rect.X + rect.Width;
                    if (rect.Y + rect.Height > maxY) maxY = rect.Y + rect.Height;

                    OcrWordItem wItem{};
                    wItem.text = wordStr;
                    wItem.bounds = D2D1::RectF(rect.X, rect.Y, rect.X + rect.Width, rect.Y + rect.Height);
                    res.words.push_back(wItem);

                    float charW = wordStr.empty() ? 0.f : (rect.Width / static_cast<float>(wordStr.size()));
                    for (size_t ci = 0; ci < wordStr.size(); ++ci) {
                        OcrCharItem cItem{};
                        cItem.character = wordStr[ci];
                        cItem.bounds = D2D1::RectF(rect.X + ci * charW, rect.Y, rect.X + (ci + 1) * charW, rect.Y + rect.Height);
                        cItem.lineIndex = lineIdx;
                        cItem.wordIndex = globalWordIdx;
                        cItem.charIndex = static_cast<int>(ci);
                        res.chars.push_back(cItem);
                    }
                }

                if (maxX > minX && maxY > minY) {
                    OcrLineItem lItem{};
                    lItem.text = lineText;
                    lItem.bounds = D2D1::RectF(minX, minY, maxX, maxY);
                    res.lines.push_back(lItem);
                }
            }

            std::wstring fullText = ocrResult.Text().c_str();
            if (fullText.empty() && !res.lines.empty()) {
                for (size_t i = 0; i < res.lines.size(); ++i) {
                    if (i > 0) fullText += L"\r\n";
                    fullText += res.lines[i].text;
                }
            }

            res.fullText = std::move(fullText);
            res.success = true;
            if (cb) cb(std::move(res));
        } catch (const std::exception& ex) {
            res.success = false;
            std::string msg = ex.what();
            res.errorMessage = std::wstring(msg.begin(), msg.end());
            if (cb) cb(std::move(res));
        } catch (...) {
            res.success = false;
            res.errorMessage = L"An unknown error occurred during OCR recognition.";
            if (cb) cb(std::move(res));
        }
    }).detach();
}

void ExtractTextAsync(std::vector<uint8_t> bgra, uint32_t width, uint32_t height,
                      HWND /*clipboardOwner*/) noexcept {
    ExtractInlineTextAsync(std::move(bgra), width, height, [](OcrInlineResult res) {
        if (res.success && !res.fullText.empty()) {
            Clipboard::WriteText(res.fullText.c_str(), nullptr);
            MessageBeep(MB_OK);
        } else {
            Clipboard::WriteText(L"", nullptr);
        }
    });
}

} // namespace snippy
