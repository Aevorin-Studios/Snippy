#pragma once
#include "../annotation.h"
#include <d2d1_3.h>
namespace snippy {
struct BlurToolState { D2D1_POINT_2F anchor{}; bool active = false; };
void BlurTool_OnDown(BlurToolState& s, D2D1_POINT_2F pt);
bool BlurTool_OnUp  (BlurToolState& s, D2D1_POINT_2F pt, Annotation& out, float radius);
void BlurTool_Draw  (ID2D1DeviceContext* dc, const BlurToolState& s, D2D1_POINT_2F cur,
                     ID2D1SolidColorBrush* brush);
} // namespace snippy
