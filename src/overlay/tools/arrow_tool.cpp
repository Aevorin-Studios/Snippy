#include "arrow_tool.h"
#include <cmath>
using namespace D2D1;
namespace snippy {

void ArrowTool_OnDown(ArrowToolState& s, D2D1_POINT_2F pt) {
    s.start = pt; s.active = true;
}

static void DrawArrowHead(ID2D1DeviceContext* dc, D2D1_POINT_2F from, D2D1_POINT_2F to,
                           ID2D1SolidColorBrush* brush, float width) {
    float dx = to.x - from.x, dy = to.y - from.y;
    float len = sqrtf(dx * dx + dy * dy);
    if (len < 1.0f) return;
    float ux = dx / len, uy = dy / len;

    float headLen  = std::max(12.f, width * 4.f);
    float headHalf = headLen * 0.45f;

    D2D1_POINT_2F tip   = to;
    D2D1_POINT_2F base  = { to.x - ux * headLen, to.y - uy * headLen };
    D2D1_POINT_2F left  = { base.x + uy * headHalf, base.y - ux * headHalf };
    D2D1_POINT_2F right = { base.x - uy * headHalf, base.y + ux * headHalf };

    ComPtr<ID2D1PathGeometry> path;
    ComPtr<ID2D1Factory> factory;
    dc->GetFactory(&factory);
    factory->CreatePathGeometry(&path);
    ComPtr<ID2D1GeometrySink> sink;
    path->Open(&sink);
    sink->BeginFigure(tip, D2D1_FIGURE_BEGIN_FILLED);
    sink->AddLine(left);
    sink->AddLine(right);
    sink->EndFigure(D2D1_FIGURE_END_CLOSED);
    sink->Close();
    dc->FillGeometry(path.Get(), brush);

    // Shaft (stop just before the tip to avoid overlap)
    D2D1_POINT_2F shaftEnd = base;
    dc->DrawLine(from, shaftEnd, brush, width);
}

bool ArrowTool_OnUp(ArrowToolState& s, D2D1_POINT_2F pt, Annotation& out,
                    ColorF color, float width) {
    if (!s.active) return false;
    s.active = false;
    float dx = pt.x - s.start.x, dy = pt.y - s.start.y;
    if (sqrtf(dx*dx + dy*dy) < 5.f) return false;
    ArrowAnnotation aa{ s.start, pt, color, width };
    out = Annotation{ AnnotationType::Arrow, aa };
    return true;
}

void ArrowTool_Draw(ID2D1DeviceContext* dc, const ArrowToolState& s, D2D1_POINT_2F cur,
                    ID2D1SolidColorBrush* brush, float width) {
    if (!s.active) return;
    DrawArrowHead(dc, s.start, cur, brush, width);
}

} // namespace snippy
