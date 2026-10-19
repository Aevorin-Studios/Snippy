#pragma once
//
// annotation.h — Shared annotation data types for the editor.
//

#include "core/types.h"
#include <windows.h>
#include <d2d1_3.h>
#include <d2d1_3helper.h>
#include <dwrite_3.h>
#include <wrl/client.h>
#include <string>
#include <variant>
#include <vector>

using Microsoft::WRL::ComPtr;

namespace snippy {

enum class AnnotationType { Rect, Ellipse, Arrow, Text, Blur, FreeDraw, Counter };
enum class ToolMode       { Select, Rect, FilledRect, Ellipse, Line, Arrow, Text, Blur, FreeDraw, Highlighter, Eraser, Counter };
enum class CaptureMode    { Region, Fullscreen, Freeform, ColorPicker, TextExtract };
enum class BlurStyle      { Gaussian, Mosaic, Invert };

inline D2D1_COLOR_F ToD2D(ColorF c) { return D2D1::ColorF(c.r, c.g, c.b, c.a); }

struct RectAnnotation {
    D2D1_RECT_F rect{};
    ColorF      strokeColor{ 1.f, 0.2f, 0.2f, 1.f };
    ColorF      fillColor  { 1.f, 0.2f, 0.2f, 0.f };
    float       strokeWidth{ 2.5f };
};

struct EllipseAnnotation {
    D2D1_ELLIPSE ellipse{};
    ColorF       strokeColor{ 1.f, 0.2f, 0.2f, 1.f };
    ColorF       fillColor  { 1.f, 0.2f, 0.2f, 0.f };
    float        strokeWidth{ 2.5f };
};

struct ArrowAnnotation {
    D2D1_POINT_2F start{};
    D2D1_POINT_2F end{};
    ColorF        color{ 1.f, 0.2f, 0.2f, 1.f };
    float         width{ 3.0f };
    bool          arrowHead{ true };
};

struct TextAnnotation {
    D2D1_POINT_2F origin{};
    std::wstring  text;
    ColorF        color{ 1.f, 1.f, 1.f, 1.f };
    float         fontSize{ 16.f };
    std::wstring  fontFamily{ L"Segoe UI" };
    float         scaleX{ 1.0f };
    float         scaleY{ 1.0f };
};

struct BlurAnnotation {
    D2D1_RECT_F rect{};
    float       radius{ 8.0f };
    BlurStyle   style{ BlurStyle::Gaussian };
};

struct FreeDrawAnnotation {
    std::vector<D2D1_POINT_2F> points;
    ColorF                     color{ 1.f, 0.2f, 0.2f, 1.f };
    float                      width{ 3.0f };
    bool                       isHighlighter{ false };
};

struct CounterAnnotation {
    D2D1_POINT_2F center{};
    D2D1_POINT_2F pointer{};
    bool          hasPointer = false;
    int           number = 1;
    ColorF        color{ 1.f, 0.23f, 0.23f, 1.f };
};

struct Annotation {
    AnnotationType type;
    std::variant<
        RectAnnotation,
        EllipseAnnotation,
        ArrowAnnotation,
        TextAnnotation,
        BlurAnnotation,
        FreeDrawAnnotation,
        CounterAnnotation
    > data;
};

} // namespace snippy
