#pragma once

//
// perf_counter.h — Zero-overhead high-resolution performance instrumentation.
//
// In Release builds (SNIPPY_PERF not defined), all macros compile to nothing.
// In Perf builds (cmake --preset perf), macros record QPC timestamps.
//
// Usage:
//   PERF_SESSION_BEGIN();
//   ... do work ...
//   PERF_MARK(capture_done);
//   ... more work ...
//   PERF_REPORT();
//

#include <windows.h>
#include <cstdint>
#include <cstdio>

namespace snippy::perf {

// Cached QPC frequency (calls QueryPerformanceFrequency once at startup).
inline LARGE_INTEGER g_qpcFreq{};

inline void Initialize() noexcept {
    QueryPerformanceFrequency(&g_qpcFreq);
}

inline double TicksToMs(LONGLONG ticks) noexcept {
    return static_cast<double>(ticks) * 1000.0 /
           static_cast<double>(g_qpcFreq.QuadPart);
}

inline LARGE_INTEGER Now() noexcept {
    LARGE_INTEGER t{};
    QueryPerformanceCounter(&t);
    return t;
}

struct Session {
    LARGE_INTEGER t_hotkey{};
    LARGE_INTEGER t_capture_begin{};
    LARGE_INTEGER t_capture_done{};
    LARGE_INTEGER t_overlay_shown{};
    LARGE_INTEGER t_selection_confirmed{};
    LARGE_INTEGER t_crop_done{};
    LARGE_INTEGER t_clipboard_done{};
    LARGE_INTEGER t_editor_shown{};
    uint64_t      alloc_count = 0;
    uint64_t      alloc_bytes = 0;

    void Reset() noexcept { *this = Session{}; }

    void Report() const noexcept {
        char buf[1024];
        auto ms = [&](const LARGE_INTEGER& a, const LARGE_INTEGER& b) -> double {
            return TicksToMs(b.QuadPart - a.QuadPart);
        };
        ::snprintf(buf, sizeof(buf),
            "\n[PERF] ─────────────────────────────────────\n"
            "[PERF]  Hotkey received         t=0.000 ms\n"
            "[PERF]  Capture begin:        %+8.3f ms\n"
            "[PERF]  Capture done:         %+8.3f ms\n"
            "[PERF]  Overlay shown:        %+8.3f ms\n"
            "[PERF]  Selection confirmed:  %+8.3f ms\n"
            "[PERF]  Crop done:            %+8.3f ms\n"
            "[PERF]  Clipboard written:    %+8.3f ms\n"
            "[PERF]  Editor shown:         %+8.3f ms\n"
            "[PERF]  Allocations:          %llu  (%llu bytes)\n"
            "[PERF] ─────────────────────────────────────\n",
            ms(t_hotkey, t_capture_begin),
            ms(t_hotkey, t_capture_done),
            ms(t_hotkey, t_overlay_shown),
            ms(t_hotkey, t_selection_confirmed),
            ms(t_hotkey, t_crop_done),
            ms(t_hotkey, t_clipboard_done),
            ms(t_hotkey, t_editor_shown),
            alloc_count, alloc_bytes
        );
        OutputDebugStringA(buf);
    }
};

// Global session — reset on each screenshot
inline Session g_session{};

} // namespace snippy::perf

// ── Macros ────────────────────────────────────────────────────────────────────
#if defined(SNIPPY_PERF)
#   define PERF_INIT()                  snippy::perf::Initialize()
#   define PERF_RESET()                 snippy::perf::g_session.Reset()
#   define PERF_MARK(field)             snippy::perf::g_session.field = snippy::perf::Now()
#   define PERF_ALLOC(count, bytes)     do { snippy::perf::g_session.alloc_count += (count); \
                                             snippy::perf::g_session.alloc_bytes += (bytes); } while(0)
#   define PERF_REPORT()                snippy::perf::g_session.Report()
#else
#   define PERF_INIT()                  ((void)0)
#   define PERF_RESET()                 ((void)0)
#   define PERF_MARK(field)             ((void)0)
#   define PERF_ALLOC(count, bytes)     ((void)0)
#   define PERF_REPORT()                ((void)0)
#endif
