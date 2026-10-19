#include "blur_tool.h"
#include <algorithm>
using namespace D2D1;
namespace snippy {

void BlurTool_OnDown(BlurToolState& s, D2D1_POINT_2F pt) {
    s.anchor = pt; s.active = true;
}

bool BlurTool_OnUp(BlurToolState& s, D2D1_POINT_2F pt, Annotation& out, float radius) {
    if (!s.active) return false;
    s.active = false;
    auto r = RectF(std::min(s.anchor.x, pt.x), std::min(s.anchor.y, pt.y),
                   std::max(s.anchor.x, pt.x), std::max(s.anchor.y, pt.y));
    if (r.right - r.left < 2 || r.bottom - r.top < 2) return false;
    BlurAnnotation ba{ r, radius };
    out = Annotation{ AnnotationType::Blur, ba };
    return true;
}

void BlurTool_Draw(ID2D1DeviceContext* dc, const BlurToolState& s, D2D1_POINT_2F cur,
                   ID2D1SolidColorBrush* brush) {
    if (!s.active) return;
    auto r = RectF(std::min(s.anchor.x, cur.x), std::min(s.anchor.y, cur.y),
                   std::max(s.anchor.x, cur.x), std::max(s.anchor.y, cur.y));
    // Dashed outline while drawing the blur region
    dc->DrawRectangle(r, brush, 2.0f);
}

} // namespace snippy
