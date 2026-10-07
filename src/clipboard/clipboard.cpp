#include "clipboard.h"
#include "../app.h"
#include <wincodec.h>
#include <wrl/client.h>
#include <cstring>
#include <thread>
#include <vector>

using Microsoft::WRL::ComPtr;

namespace snippy {

// ── CF_DIB fast path ─────────────────────────────────────────────────────────
//
// CF_DIB layout:
//   BITMAPINFOHEADER  (40 bytes)
//   pixel rows        (bottom-up, BGRA)
//
// We write directly into GlobalAlloc memory — no intermediate buffer.

bool Clipboard::WriteDIB(const FrameBuffer& fb, RECT region, HWND owner) noexcept {
    FrameBuffer view = fb.SubRegion(region);
    if (!view.IsValid()) return false;

    const UINT w = view.width;
    const UINT h = view.height;
    const UINT rowBytes = w * 4;

    const SIZE_T dibSize = sizeof(BITMAPINFOHEADER) + static_cast<SIZE_T>(rowBytes) * h;

    HGLOBAL hGlobal = GlobalAlloc(GMEM_MOVEABLE, dibSize);
    if (!hGlobal) return false;

    BYTE* pDst = static_cast<BYTE*>(GlobalLock(hGlobal));
    if (!pDst) {
        GlobalFree(hGlobal);
        return false;
    }

    BITMAPINFOHEADER* bih = reinterpret_cast<BITMAPINFOHEADER*>(pDst);
    bih->biSize          = sizeof(BITMAPINFOHEADER);
    bih->biWidth         = static_cast<LONG>(w);
    bih->biHeight        = -static_cast<LONG>(h);  // negative = top-down (matches our BGRA layout)
    bih->biPlanes        = 1;
    bih->biBitCount      = 32;
    bih->biCompression   = BI_RGB;
    bih->biSizeImage     = rowBytes * h;
    bih->biXPelsPerMeter = 3779;  // 96 DPI
    bih->biYPelsPerMeter = 3779;
    bih->biClrUsed       = 0;
    bih->biClrImportant  = 0;

    // Copy pixel rows directly into the global block — one pass
    BYTE* pPixels = pDst + sizeof(BITMAPINFOHEADER);
    for (UINT row = 0; row < h; ++row) {
        const BYTE* src = view.RowPtr(row);
        BYTE*       dst = pPixels + static_cast<SIZE_T>(row) * rowBytes;
        for (UINT col = 0; col < w; ++col) {
            BYTE b = src[col * 4 + 0];
            BYTE g = src[col * 4 + 1];
            BYTE r = src[col * 4 + 2];
            BYTE a = src[col * 4 + 3];
            if (a == 0) {
                dst[col * 4 + 0] = 255;
                dst[col * 4 + 1] = 255;
                dst[col * 4 + 2] = 255;
                dst[col * 4 + 3] = 0;
            } else {
                dst[col * 4 + 0] = b;
                dst[col * 4 + 1] = g;
                dst[col * 4 + 2] = r;
                dst[col * 4 + 3] = a;
            }
        }
    }

    GlobalUnlock(hGlobal);

    if (!OpenClipboard(owner)) {
        GlobalFree(hGlobal);
        return false;
    }
    EmptyClipboard();
    if (!SetClipboardData(CF_DIB, hGlobal)) {
        GlobalFree(hGlobal);
        CloseClipboard();
        return false;
    }
    // Ownership transferred to clipboard — do NOT free hGlobal
    CloseClipboard();
    return true;
}

// ── CF_PNG async path ────────────────────────────────────────────────────────
//
// Called after WriteDIB returns. Encodes PNG on a worker thread and
// updates the clipboard with CF_PNG when done.
// We make a private copy of the cropped pixels so the hot-path frame buffer
// can be released immediately.

void Clipboard::WritePNGAsync(const FrameBuffer& fb, RECT region) noexcept {
    FrameBuffer view = fb.SubRegion(region);
    if (!view.IsValid()) return;

    const UINT w = view.width;
    const UINT h = view.height;
    const UINT rowBytes = w * 4;

    // Make a private pixel copy (the staging texture will be reused)
    std::vector<BYTE> pixels(static_cast<size_t>(rowBytes) * h);
    for (UINT row = 0; row < h; ++row) {
        std::memcpy(pixels.data() + static_cast<size_t>(row) * rowBytes,
                    view.RowPtr(row), rowBytes);
    }

    // Launch encode thread (detached — updates clipboard when done)
    std::thread([pixels = std::move(pixels), w, h, rowBytes]() mutable {
        CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

        ComPtr<IWICImagingFactory> wicFactory;
        if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr,
                                    CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wicFactory)))) {
            CoUninitialize();
            return;
        }

        ComPtr<IStream> stream;
        if (FAILED(CreateStreamOnHGlobal(nullptr, TRUE, &stream))) {
            CoUninitialize();
            return;
        }

        ComPtr<IWICBitmapEncoder> encoder;
        if (FAILED(wicFactory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder))) {
            CoUninitialize();
            return;
        }
        if (FAILED(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache))) {
            CoUninitialize();
            return;
        }

        ComPtr<IWICBitmapFrameEncode> frame;
        ComPtr<IPropertyBag2> props;
        encoder->CreateNewFrame(&frame, &props);
        frame->Initialize(props.Get());
        frame->SetSize(w, h);

        WICPixelFormatGUID fmt = GUID_WICPixelFormat32bppBGRA;
        frame->SetPixelFormat(&fmt);
        frame->WritePixels(h, rowBytes, static_cast<UINT>(pixels.size()), pixels.data());
        frame->Commit();
        encoder->Commit();

        HGLOBAL hMem = nullptr;
        GetHGlobalFromStream(stream.Get(), &hMem);

        // CF_PNG is a registered format (as opposed to a built-in CF_*) —
        // required for Discord, browsers, and modern paste targets.
        UINT cfPng = RegisterClipboardFormatW(L"PNG");
        if (cfPng && OpenClipboard(nullptr)) {
            HGLOBAL hCopy = GlobalAlloc(GMEM_MOVEABLE, GlobalSize(hMem));
            if (hCopy) {
                void* src = GlobalLock(hMem);
                void* dst = GlobalLock(hCopy);
                if (src && dst) std::memcpy(dst, src, GlobalSize(hMem));
                if (src) GlobalUnlock(hMem);
                if (dst) GlobalUnlock(hCopy);
                SetClipboardData(cfPng, hCopy);
            }
            CloseClipboard();
        }

        pixels.clear();
        pixels.shrink_to_fit();

        CoUninitialize();
        App::ScheduleTrimMemory(50);
    }).detach();
}

bool Clipboard::WriteText(const wchar_t* text, HWND owner) noexcept {
    if (!text) return false;
    const size_t bytes = (wcslen(text) + 1) * sizeof(wchar_t);
    HGLOBAL hGlobal = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (!hGlobal) return false;
    void* dst = GlobalLock(hGlobal);
    if (!dst) {
        GlobalFree(hGlobal);
        return false;
    }
    memcpy(dst, text, bytes);
    GlobalUnlock(hGlobal);

    if (!OpenClipboard(owner)) {
        GlobalFree(hGlobal);
        return false;
    }
    EmptyClipboard();
    if (!SetClipboardData(CF_UNICODETEXT, hGlobal)) {
        GlobalFree(hGlobal);
        CloseClipboard();
        return false;
    }
    CloseClipboard();
    return true;
}

} // namespace snippy
