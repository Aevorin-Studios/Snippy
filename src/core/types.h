#pragma once

#include <cstdint>

#if defined(_WIN32)
#include <windows.h>
#endif

namespace snippy {

struct Point {
    int32_t x = 0;
    int32_t y = 0;

#if defined(_WIN32)
    constexpr Point() noexcept = default;
    constexpr Point(int32_t px, int32_t py) noexcept : x(px), y(py) {}
    constexpr Point(POINT pt) noexcept : x(pt.x), y(pt.y) {}
    constexpr operator POINT() const noexcept { return POINT{ x, y }; }
#endif
};

struct Rect {
    int32_t left = 0;
    int32_t top = 0;
    int32_t right = 0;
    int32_t bottom = 0;

    [[nodiscard]] constexpr int32_t Width() const noexcept { return right - left; }
    [[nodiscard]] constexpr int32_t Height() const noexcept { return bottom - top; }

#if defined(_WIN32)
    constexpr Rect() noexcept = default;
    constexpr Rect(int32_t l, int32_t t, int32_t r, int32_t b) noexcept : left(l), top(t), right(r), bottom(b) {}
    constexpr Rect(RECT r) noexcept : left(r.left), top(r.top), right(r.right), bottom(r.bottom) {}
    constexpr operator RECT() const noexcept { return RECT{ left, top, right, bottom }; }
#endif
};

struct ColorF {
    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;
    float a = 1.0f;
};

} // namespace snippy
