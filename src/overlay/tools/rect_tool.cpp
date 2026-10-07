#include "rect_tool.h"
#include <algorithm>
using namespace D2D1;
namespace snippy {

void RectTool_OnDown(RectToolState& s, D2D1_POINT_2F pt) {
    s.anchor = pt; s.active = true;
}

bool RectTool_OnMove(RectToolState& s, D2D1_POINT_2F pt, RectAnnotation& out) {
    if (!s.active) return false;
    out.rect = RectF(std::min(s.anchor.x, pt.x), std::min(s.anchor.y, pt.y),
                     std::max(s.anchor.x, pt.x), std::max(s.anchor.y, pt.y));
    return true;
}

bool RectTool_OnUp(RectToolState& s, D2D1_POINT_2F pt, Annotation& out,
                   ColorF stroke, float strokeW) {
    if (!s.active) return false;
    s.active = false;
    RectAnnotation ra{};
    ra.rect = RectF(std::min(s.anchor.x, pt.x), std::min(s.anchor.y, pt.y),
                    std::max(s.anchor.x, pt.x), std::max(s.anchor.y, pt.y));
    ra.strokeColor = stroke;
    ra.strokeWidth = strokeW;
    if (ra.rect.right - ra.rect.left < 2 || ra.rect.bottom - ra.rect.top < 2) return false;
    out = Annotation{ AnnotationType::Rect, ra };
    return true;
}

void RectTool_Draw(ID2D1DeviceContext* dc, const RectToolState& s, D2D1_POINT_2F cur,
                   ID2D1SolidColorBrush* brush, float strokeW) {
    if (!s.active) return;
    auto r = RectF(std::min(s.anchor.x, cur.x), std::min(s.anchor.y, cur.y),
                   std::max(s.anchor.x, cur.x), std::max(s.anchor.y, cur.y));
    dc->DrawRectangle(r, brush, strokeW);
}

} // namespace snippy
