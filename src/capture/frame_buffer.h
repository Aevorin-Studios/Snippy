#pragma once

//
// frame_buffer.h — Zero-copy view into a captured desktop frame.
//
// A FrameBuffer never owns its memory. It is a non-owning view (like std::span)
// over a pre-allocated staging texture that the DxgiCapture module owns.
//
// SubRegion() performs only pointer arithmetic — no copy, no allocation.
//

#include <windows.h>
#include <cstdint>
#include <cassert>
#include <span>
#include <algorithm>

namespace snippy {

struct FrameBuffer {
    const uint8_t* data   = nullptr;  // BGRA row-major pixels
    uint32_t       stride = 0;        // bytes per row (may include padding)
    uint32_t       width  = 0;        // pixels
    uint32_t       height = 0;        // pixels

    [[nodiscard]] bool IsValid() const noexcept {
        return data != nullptr && width > 0 && height > 0 && stride >= width * 4;
    }

    // Returns a non-owning view over a sub-rectangle of this buffer.
    // The returned FrameBuffer shares the same underlying memory — no copy.
    [[nodiscard]] FrameBuffer SubRegion(RECT r) const noexcept {
        r.left   = std::max(r.left,   0L);
        r.top    = std::max(r.top,    0L);
        r.right  = std::min(r.right,  static_cast<LONG>(width));
        r.bottom = std::min(r.bottom, static_cast<LONG>(height));

        if (r.right <= r.left || r.bottom <= r.top) {
            return FrameBuffer{};
        }

        return FrameBuffer{
            .data   = data + static_cast<size_t>(r.top)  * stride
                           + static_cast<size_t>(r.left) * 4,
            .stride = stride,
            .width  = static_cast<uint32_t>(r.right  - r.left),
            .height = static_cast<uint32_t>(r.bottom - r.top),
        };
    }

    [[nodiscard]] size_t ByteSpan() const noexcept {
        if (!IsValid()) return 0;
        return static_cast<size_t>(stride) * height;
    }

    [[nodiscard]] size_t PixelBytes() const noexcept {
        if (!IsValid()) return 0;
        return static_cast<size_t>(width) * 4 * height;
    }

    [[nodiscard]] const uint8_t* RowPtr(uint32_t row) const noexcept {
        assert(row < height);
        return data + static_cast<size_t>(row) * stride;
    }

    [[nodiscard]] const uint8_t* PixelPtr(uint32_t x, uint32_t y) const noexcept {
        assert(x < width && y < height);
        return data + static_cast<size_t>(y) * stride + static_cast<size_t>(x) * 4;
    }
};

} // namespace snippy
