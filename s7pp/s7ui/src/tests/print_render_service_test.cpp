#include "render/print_render_service.hpp"

#include <cassert>

int main()
{
    using namespace s7ui::printing;

    const PageSpec a4Landscape{};
    const auto layout = pageLayout(a4Landscape);

    assert(layout.orientation() == QPageLayout::Landscape);
    assert(layout.pageSize().id() == QPageSize::A4);
    assert(layout.margins(QPageLayout::Millimeter) == QMarginsF(0.0, 0.0, 0.0, 0.0));

    const auto paintPixels = paintRectPixels(layout, a4Landscape.resolutionDpi);
    assert(paintPixels.width() > paintPixels.height());
    assert(paintPixels.width() > 0);
    assert(paintPixels.height() > 0);

    return 0;
}


