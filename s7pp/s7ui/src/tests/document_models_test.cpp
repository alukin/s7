#include "documents/diagram_document.hpp"
#include "documents/plot_document.hpp"

#include <cassert>

using namespace s7ui::documents;

int main()
{
    {
        const auto a4Landscape = DiagramPageLayout::defaults(PaperStandard::A4, PageOrientation::Landscape);
        assert(a4Landscape.widthMm == 297.0);
        assert(a4Landscape.heightMm == 210.0);
        assert(a4Landscape.standardName() == "A4");
        assert(a4Landscape.orientationName() == "landscape");
    }

    {
        const auto a3Portrait = DiagramPageLayout::defaults(PaperStandard::A3, PageOrientation::Portrait);
        assert(a3Portrait.widthMm == 297.0);
        assert(a3Portrait.heightMm == 420.0);
        assert(a3Portrait.orientationName() == "portrait");
    }

    {
        DiagramPageLayout malformed;
        malformed.standard = PaperStandard::A2;
        malformed.orientation = PageOrientation::Portrait;
        malformed.widthMm = 594.0;
        malformed.heightMm = 420.0;

        const auto normalized = malformed.normalized();
        assert(normalized.widthMm == 420.0);
        assert(normalized.heightMm == 594.0);
    }

    {
        assert(DiagramPageLayout::standardFromName("a1") == PaperStandard::A1);
        assert(DiagramPageLayout::standardFromName("A5") == PaperStandard::A4);
        assert(DiagramPageLayout::orientationFromName("portrait") == PageOrientation::Portrait);
        assert(DiagramPageLayout::orientationFromName("anything") == PageOrientation::Landscape);
    }

    {
        PlotDocument plotDoc;
        PlotPage page;
        page.id = "page_1";
        page.text.title = "Probe Run";
        page.source.path = "/tmp/run.json";
        plotDoc.pages.push_back(page);

        assert(plotDoc.pages.size() == 1);
        assert(plotDoc.pages[0].text.title == "Probe Run");
        assert(plotDoc.pages[0].source.format == "json");
    }

    return 0;
}

