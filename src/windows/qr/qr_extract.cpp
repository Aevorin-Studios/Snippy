#include "qr_extract.h"
#include "quirc.h"

#include <thread>
#include <algorithm>
#include <vector>

namespace snippy {

namespace {

std::wstring Utf8ToWide(const uint8_t* data, size_t len) noexcept {
    if (!data || len == 0) return {};
    int needed = MultiByteToWideChar(CP_UTF8, 0, reinterpret_cast<const char*>(data), static_cast<int>(len), nullptr, 0);
    if (needed <= 0) {
        needed = MultiByteToWideChar(CP_ACP, 0, reinterpret_cast<const char*>(data), static_cast<int>(len), nullptr, 0);
        if (needed <= 0) return {};
        std::wstring out(needed, L'\0');
        MultiByteToWideChar(CP_ACP, 0, reinterpret_cast<const char*>(data), static_cast<int>(len), out.data(), needed);
        return out;
    }
    std::wstring out(needed, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, reinterpret_cast<const char*>(data), static_cast<int>(len), out.data(), needed);
    return out;
}

bool CheckIfUrl(const std::wstring& text, std::wstring& outUrl) noexcept {
    auto startsWith = [](const std::wstring& str, const std::wstring& prefix) {
        if (str.size() < prefix.size()) return false;
        return _wcsnicmp(str.c_str(), prefix.c_str(), prefix.size()) == 0;
    };

    if (startsWith(text, L"http://") || startsWith(text, L"https://") ||
        startsWith(text, L"ftp://")   || startsWith(text, L"file://")) {
        outUrl = text;
        return true;
    }
    if (startsWith(text, L"www.")) {
        outUrl = L"https://" + text;
        return true;
    }
    size_t colon = text.find(L"://");
    if (colon != std::wstring::npos && colon > 1 && colon < 16) {
        outUrl = text;
        return true;
    }
    if (startsWith(text, L"mailto:") || startsWith(text, L"tel:")) {
        outUrl = text;
        return true;
    }
    return false;
}

bool TryScanPass(struct quirc* q, int w, int h, float scaleFactor, QrResult& out) noexcept {
    quirc_end(q);
    int count = quirc_count(q);
    for (int i = 0; i < count; ++i) {
        struct quirc_code code;
        struct quirc_data data;
        quirc_extract(q, i, &code);
        if (quirc_decode(&code, &data) == QUIRC_SUCCESS && data.payload_len > 0) {
            float minX = static_cast<float>(code.corners[0].x);
            float maxX = minX;
            float minY = static_cast<float>(code.corners[0].y);
            float maxY = minY;
            for (int c = 1; c < 4; ++c) {
                minX = std::min(minX, static_cast<float>(code.corners[c].x));
                maxX = std::max(maxX, static_cast<float>(code.corners[c].x));
                minY = std::min(minY, static_cast<float>(code.corners[c].y));
                maxY = std::max(maxY, static_cast<float>(code.corners[c].y));
            }
            out.bounds = D2D1::RectF(minX * scaleFactor, minY * scaleFactor,
                                     maxX * scaleFactor, maxY * scaleFactor);
            out.payload = Utf8ToWide(data.payload, data.payload_len);
            out.isUrl = CheckIfUrl(out.payload, out.url);
            out.success = true;
            return true;
        }
    }
    return false;
}

} // namespace

QrResult ScanQrFromBgra(const uint8_t* bgra, uint32_t width, uint32_t height) noexcept {
    QrResult res{};
    if (!bgra || width < 16 || height < 16) {
        res.errorMessage = L"Image is too small or invalid.";
        return res;
    }

    struct quirc* q = quirc_new();
    if (!q) {
        res.errorMessage = L"Failed to allocate QR decoder context.";
        return res;
    }

    int w = static_cast<int>(width);
    int h = static_cast<int>(height);

    if (quirc_resize(q, w, h) < 0) {
        quirc_destroy(q);
        res.errorMessage = L"Failed to resize QR decoder buffer.";
        return res;
    }

    // Pass 1: Standard grayscale
    int qw = 0, qh = 0;
    uint8_t* img = quirc_begin(q, &qw, &qh);
    for (int y = 0; y < h; ++y) {
        const uint8_t* src = bgra + static_cast<size_t>(y) * width * 4;
        uint8_t* dst = img + static_cast<size_t>(y) * qw;
        for (int x = 0; x < w; ++x) {
            dst[x] = static_cast<uint8_t>((src[x * 4 + 2] * 77 + src[x * 4 + 1] * 150 + src[x * 4 + 0] * 29) >> 8);
        }
    }
    if (TryScanPass(q, qw, qh, 1.0f, res)) {
        quirc_destroy(q);
        return res;
    }

    // Pass 2: Inverted grayscale (for dark mode QR codes)
    img = quirc_begin(q, &qw, &qh);
    for (int y = 0; y < h; ++y) {
        const uint8_t* src = bgra + static_cast<size_t>(y) * width * 4;
        uint8_t* dst = img + static_cast<size_t>(y) * qw;
        for (int x = 0; x < w; ++x) {
            uint8_t g = static_cast<uint8_t>((src[x * 4 + 2] * 77 + src[x * 4 + 1] * 150 + src[x * 4 + 0] * 29) >> 8);
            dst[x] = 255 - g;
        }
    }
    if (TryScanPass(q, qw, qh, 1.0f, res)) {
        quirc_destroy(q);
        return res;
    }

    // Pass 3: 2x upscaling for small QR codes (under 300px)
    if (w < 300 && h < 300) {
        int w2 = w * 2;
        int h2 = h * 2;
        if (quirc_resize(q, w2, h2) == 0) {
            img = quirc_begin(q, &qw, &qh);
            for (int y = 0; y < h2; ++y) {
                const uint8_t* src = bgra + static_cast<size_t>(y / 2) * width * 4;
                uint8_t* dst = img + static_cast<size_t>(y) * qw;
                for (int x = 0; x < w2; ++x) {
                    int sx = x / 2;
                    dst[x] = static_cast<uint8_t>((src[sx * 4 + 2] * 77 + src[sx * 4 + 1] * 150 + src[sx * 4 + 0] * 29) >> 8);
                }
            }
            if (TryScanPass(q, qw, qh, 0.5f, res)) {
                quirc_destroy(q);
                return res;
            }
        }
    }

    quirc_destroy(q);
    res.errorMessage = L"No QR code found in selection.";
    return res;
}

void ScanQrAsync(std::vector<uint8_t> bgra, uint32_t width, uint32_t height,
                 std::function<void(QrResult)> callback) noexcept {
    std::thread([pixels = std::move(bgra), width, height, cb = std::move(callback)]() mutable {
        QrResult res = ScanQrFromBgra(pixels.data(), width, height);
        if (cb) cb(std::move(res));
    }).detach();
}

} // namespace snippy
