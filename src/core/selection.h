#pragma once

#include "types.h"
#include <algorithm>

namespace snippy {

struct SelectionState {
    enum class Phase { Idle, Dragging, Editing, Confirmed };

    Phase phase   = Phase::Idle;
    Point anchor  = {};
    Point current = {};

    void BeginDrag(Point pt) noexcept {
        anchor  = pt;
        current = pt;
        phase   = Phase::Dragging;
    }

    void UpdateDrag(Point pt) noexcept {
        current = pt;
    }

    void Confirm() noexcept {
        if (phase == Phase::Dragging) {
            phase = Phase::Confirmed;
        }
    }

    void Reset() noexcept {
        phase   = Phase::Idle;
        anchor  = {};
        current = {};
    }

    [[nodiscard]] Rect NormalizedRect() const noexcept {
        Rect r{};
        r.left   = std::min(anchor.x, current.x);
        r.top    = std::min(anchor.y, current.y);
        r.right  = std::max(anchor.x, current.x);
        r.bottom = std::max(anchor.y, current.y);
        return r;
    }

    [[nodiscard]] bool IsValid() const noexcept {
        if (phase == Phase::Idle) return false;
        Rect r = NormalizedRect();
        return (r.right - r.left) >= 4 && (r.bottom - r.top) >= 4;
    }

    [[nodiscard]] int32_t Width()  const noexcept { auto r = NormalizedRect(); return r.right  - r.left; }
    [[nodiscard]] int32_t Height() const noexcept { auto r = NormalizedRect(); return r.bottom - r.top;  }
};

} // namespace snippy
