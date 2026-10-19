#pragma once

//
// clipboard.h — High-performance clipboard writer.
//
// WriteDIB:     Synchronous. CF_DIB from raw BGRA. No encoding. < 1 ms typical.
// WritePNGAsync: Asynchronous. WIC PNG encode on worker thread. Non-blocking.
//

#include <windows.h>
#include "core/frame_buffer.h"

namespace snippy {

class Clipboard {
public:
    // Write CF_DIB (raw Device-Independent Bitmap) to clipboard.
    // This is the FASTEST clipboard format — no PNG/JPEG encoding required.
    // Compatible with all Windows applications (Paint, Word, Slack, Discord, etc.)
    //
    // Copies the sub-region defined by 'region' from 'fb'.
    // region is in virtual desktop coordinates; fb must contain that region.
    // owner: the HWND to associate with the clipboard open (typically overlay HWND).
    //
    // Returns true on success.
    static bool WriteDIB(const FrameBuffer& fb, RECT region, HWND owner) noexcept;

    // Write CF_PNG asynchronously on a worker thread.
    // Also adds CF_DIBV5 (DIBv5 with alpha) which supports more apps.
    // Does not block the caller. Clipboard is updated when encode completes.
    // Caller must ensure 'fb' data remains valid until the thread completes.
    // Use a copied buffer (pixelData) — this function takes ownership of it.
    static void WritePNGAsync(const FrameBuffer& fb, RECT region) noexcept;

    static bool WriteText(const wchar_t* text, HWND owner) noexcept;
};

} // namespace snippy
