#pragma once
#include "../annotation.h"
#include <d2d1_3.h>
namespace snippy {
struct ArrowToolState { D2D1_POINT_2F start{}; bool active = false; };
void ArrowTool_OnDown(ArrowToolState& s, D2D1_POINT_2F pt);
bool ArrowTool_OnUp  (ArrowToolState& s, D2D1_POINT_2F pt, Annotation& out, ColorF color, float width);
void ArrowTool_Draw  (ID2D1DeviceContext* dc, const ArrowToolState& s, D2D1_POINT_2F cur,
                      ID2D1SolidColorBrush* brush, float width);
} // namespace snippy
