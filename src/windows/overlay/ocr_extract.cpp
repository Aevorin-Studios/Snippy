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

#include <cmath>

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
        if (width == 0 || height == 0 || pixels.empty()) {
            res.success = false;
            res.errorMessage = L"Invalid image buffer dimensions.";
            if (cb) cb(std::move(res));
            return;
        }

        try {
            winrt::init_apartment(winrt::apartment_type::multi_threaded);

            using winrt::Windows::Graphics::Imaging::BitmapAlphaMode;
            using winrt::Windows::Graphics::Imaging::BitmapBufferAccessMode;
            using winrt::Windows::Graphics::Imaging::BitmapPixelFormat;
            using winrt::Windows::Graphics::Imaging::SoftwareBitmap;
            using winrt::Windows::Media::Ocr::OcrEngine;

            // Compute optimal upscale factor:
            // Screen fonts (10-14px) are below Windows OCR's optimal recognition threshold (~24-36px).
            // Adaptive upscaling brings small/code text into prime recognition range.
            float scale = 2.0f;
            if (width <= 400 || height <= 300) {
                scale = 3.0f;
            } else if (width <= 1200 && height <= 800) {
                scale = 2.5f;
            } else {
                scale = 2.0f;
            }

            // Keep scaled dimensions well within WinRT MaxImageDimension limits (<= 6000px)
            if (static_cast<float>(width) * scale > 6000.0f) {
                scale = 6000.0f / static_cast<float>(width);
            }
            if (static_cast<float>(height) * scale > 6000.0f) {
                scale = std::min(scale, 6000.0f / static_cast<float>(height));
            }
            if (scale < 1.0f) scale = 1.0f;

            const uint32_t dstW = std::max(1u, static_cast<uint32_t>(std::round(static_cast<float>(width) * scale)));
            const uint32_t dstH = std::max(1u, static_cast<uint32_t>(std::round(static_cast<float>(height) * scale)));
            const float invScaleX = static_cast<float>(width) / static_cast<float>(dstW);
            const float invScaleY = static_cast<float>(height) / static_cast<float>(dstH);

            // Precompute horizontal sampling weights
            struct XWeight {
                int x0, x1;
                float fx, ifx;
            };
            std::vector<XWeight> xWeights(dstW);
            for (uint32_t dx = 0; dx < dstW; ++dx) {
                float srcX = (static_cast<float>(dx) + 0.5f) * invScaleX - 0.5f;
                int x0 = std::clamp(static_cast<int>(std::floor(srcX)), 0, static_cast<int>(width - 1));
                int x1 = std::clamp(x0 + 1, 0, static_cast<int>(width - 1));
                float fx = srcX - std::floor(srcX);
                if (fx < 0.0f) fx = 0.0f;
                if (fx > 1.0f) fx = 1.0f;
                xWeights[dx] = { x0, x1, fx, 1.0f - fx };
            }

            const size_t srcStride = static_cast<size_t>(width) * 4;
            const size_t dstStride = static_cast<size_t>(dstW) * 4;
            std::vector<uint8_t> scaledPixels(dstStride * dstH);

            // Bilinear upscale + dynamic contrast expansion (1.45x boost centered at 128)
            for (uint32_t dy = 0; dy < dstH; ++dy) {
                float srcY = (static_cast<float>(dy) + 0.5f) * invScaleY - 0.5f;
                int y0 = std::clamp(static_cast<int>(std::floor(srcY)), 0, static_cast<int>(height - 1));
                int y1 = std::clamp(y0 + 1, 0, static_cast<int>(height - 1));
                float fy = srcY - std::floor(srcY);
                if (fy < 0.0f) fy = 0.0f;
                if (fy > 1.0f) fy = 1.0f;
                float ify = 1.0f - fy;

                const uint8_t* row0 = pixels.data() + static_cast<size_t>(y0) * srcStride;
                const uint8_t* row1 = pixels.data() + static_cast<size_t>(y1) * srcStride;
                uint8_t* dstRow = scaledPixels.data() + static_cast<size_t>(dy) * dstStride;

                for (uint32_t dx = 0; dx < dstW; ++dx) {
                    const auto& xw = xWeights[dx];
                    const uint8_t* p00 = row0 + xw.x0 * 4;
                    const uint8_t* p10 = row0 + xw.x1 * 4;
                    const uint8_t* p01 = row1 + xw.x0 * 4;
                    const uint8_t* p11 = row1 + xw.x1 * 4;
                    uint8_t* d = dstRow + dx * 4;

                    float w00 = ify * xw.ifx;
                    float w10 = ify * xw.fx;
                    float w01 = fy * xw.ifx;
                    float w11 = fy * xw.fx;

                    for (int c = 0; c < 3; ++c) {
                        float interp = p00[c] * w00 + p10[c] * w10 + p01[c] * w01 + p11[c] * w11;
                        float contrasted = (interp - 128.0f) * 1.45f + 128.0f;
                        d[c] = static_cast<uint8_t>(std::clamp(contrasted, 0.0f, 255.0f));
                    }
                    d[3] = 255;
                }
            }

            // High-pass 3x3 Laplacian edge sharpening filter to counteract font antialiasing blur
            std::vector<uint8_t> processedPixels = scaledPixels;
            if (dstW >= 3 && dstH >= 3) {
                constexpr float kSharpenStrength = 0.35f;
                for (uint32_t y = 1; y + 1 < dstH; ++y) {
                    const size_t prevRow = static_cast<size_t>(y - 1) * dstStride;
                    const size_t currRow = static_cast<size_t>(y) * dstStride;
                    const size_t nextRow = static_cast<size_t>(y + 1) * dstStride;

                    for (uint32_t x = 1; x + 1 < dstW; ++x) {
                        const size_t idx = currRow + x * 4;
                        for (int c = 0; c < 3; ++c) {
                            float center = scaledPixels[idx + c];
                            float u = scaledPixels[prevRow + x * 4 + c];
                            float d = scaledPixels[nextRow + x * 4 + c];
                            float l = scaledPixels[currRow + (x - 1) * 4 + c];
                            float r = scaledPixels[currRow + (x + 1) * 4 + c];
                            float val = center + kSharpenStrength * (4.0f * center - u - d - l - r);
                            processedPixels[idx + c] = static_cast<uint8_t>(std::clamp(val, 0.0f, 255.0f));
                        }
                    }
                }
            }

            SoftwareBitmap bitmap(BitmapPixelFormat::Bgra8,
                                  static_cast<int32_t>(dstW),
                                  static_cast<int32_t>(dstH),
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

                for (uint32_t y = 0; y < dstH; ++y) {
                    memcpy(dst + static_cast<size_t>(y) * plane.Stride,
                           processedPixels.data() + static_cast<size_t>(y) * dstStride,
                           dstStride);
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

                    // Rescale bounding coordinates back to the original unscaled image space
                    const float rx = rect.X * invScaleX;
                    const float ry = rect.Y * invScaleY;
                    const float rw = rect.Width * invScaleX;
                    const float rh = rect.Height * invScaleY;

                    if (w > 0) {
                        auto prevWord = words.GetAt(w - 1);
                        float gap = (rect.X - (prevWord.BoundingRect().X + prevWord.BoundingRect().Width)) * invScaleX;
                        float avgCharW = rw / std::max(1.0f, static_cast<float>(wordStr.size()));
                        if (gap >= avgCharW * 0.25f) {
                            lineText += L' ';
                        }
                    }
                    lineText += wordStr;

                    if (rx < minX) minX = rx;
                    if (ry < minY) minY = ry;
                    if (rx + rw > maxX) maxX = rx + rw;
                    if (ry + rh > maxY) maxY = ry + rh;

                    OcrWordItem wItem{};
                    wItem.text = wordStr;
                    wItem.bounds = D2D1::RectF(rx, ry, rx + rw, ry + rh);
                    res.words.push_back(wItem);

                    float charW = wordStr.empty() ? 0.f : (rw / static_cast<float>(wordStr.size()));
                    for (size_t ci = 0; ci < wordStr.size(); ++ci) {
                        OcrCharItem cItem{};
                        cItem.character = wordStr[ci];
                        cItem.bounds = D2D1::RectF(rx + ci * charW, ry, rx + (ci + 1) * charW, ry + rh);
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
        } else {
            Clipboard::WriteText(L"", nullptr);
        }
    });
}

} // namespace snippy
