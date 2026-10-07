#pragma once
#include "../annotation.h"
#include <d2d1_3.h>
namespace snippy {
struct RectToolState { D2D1_POINT_2F anchor{}; bool active = false; };
void RectTool_OnDown (RectToolState& s, D2D1_POINT_2F pt);
bool RectTool_OnMove (RectToolState& s, D2D1_POINT_2F pt, RectAnnotation& out);
bool RectTool_OnUp   (RectToolState& s, D2D1_POINT_2F pt, Annotation& out, ColorF stroke, float strokeW);
void RectTool_Draw   (ID2D1DeviceContext* dc, const RectToolState& s, D2D1_POINT_2F cur,
                      ID2D1SolidColorBrush* brush, float strokeW);
} // namespace snippy
