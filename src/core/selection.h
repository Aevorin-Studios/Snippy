#pragma once

//
// selection.h — Overlay selection rectangle state machine.
//
// Pure value type — no allocations, no Win32 calls.
//

#include <windows.h>
#include <algorithm>

namespace snippy {

struct SelectionState {
    enum class Phase { Idle, Dragging, Editing, Confirmed };

    Phase  phase   = Phase::Idle;
    POINT  anchor  = {};
    POINT  current = {};

    void BeginDrag(POINT pt) noexcept {
        anchor  = pt;
        current = pt;
        phase   = Phase::Dragging;
    }

    void UpdateDrag(POINT pt) noexcept {
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

    // Returns a normalized rect (left <= right, top <= bottom), in pixels.
    [[nodiscard]] RECT NormalizedRect() const noexcept {
        RECT r{};
        r.left   = std::min(anchor.x, current.x);
        r.top    = std::min(anchor.y, current.y);
        r.right  = std::max(anchor.x, current.x);
        r.bottom = std::max(anchor.y, current.y);
        return r;
    }

    // Returns true if the selection has a usable area (>= 4×4 px)
    [[nodiscard]] bool IsValid() const noexcept {
        if (phase == Phase::Idle) return false;
        RECT r = NormalizedRect();
        return (r.right - r.left) >= 4 && (r.bottom - r.top) >= 4;
    }

    [[nodiscard]] int Width()  const noexcept { auto r = NormalizedRect(); return r.right  - r.left; }
    [[nodiscard]] int Height() const noexcept { auto r = NormalizedRect(); return r.bottom - r.top;  }
};

} // namespace snippy
