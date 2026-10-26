#pragma once

#include <windows.h>
#include <d2d1_3.h>
#include <cstdint>
#include <string>
#include <vector>
#include <functional>

namespace snippy {

struct QrResult {
    bool         success = false;
    std::wstring payload;
    bool         isUrl = false;
    std::wstring url;
    D2D1_RECT_F  bounds{};
    std::wstring errorMessage;
};

QrResult ScanQrFromBgra(const uint8_t* bgra, uint32_t width, uint32_t height) noexcept;

void ScanQrAsync(std::vector<uint8_t> bgra, uint32_t width, uint32_t height,
                 std::function<void(QrResult)> callback) noexcept;

} // namespace snippy
