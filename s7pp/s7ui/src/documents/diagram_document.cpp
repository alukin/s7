#include "documents/diagram_document.hpp"

#include <algorithm>

namespace s7ui::documents {
namespace {

struct PaperSizeMm
{
    double width;
    double height;
};

[[nodiscard]] auto paperSize(PaperStandard standard) -> PaperSizeMm
{
    switch (standard) {
    case PaperStandard::A1: return {594.0, 841.0};
    case PaperStandard::A2: return {420.0, 594.0};
    case PaperStandard::A3: return {297.0, 420.0};
    case PaperStandard::A4:
    default: return {210.0, 297.0};
    }
}

} // namespace

auto DiagramPageLayout::defaults(const PaperStandard standard,
                                 const PageOrientation orientation) -> DiagramPageLayout
{
    const auto size = paperSize(standard);
    DiagramPageLayout layout;
    layout.standard = standard;
    layout.orientation = orientation;
    if (orientation == PageOrientation::Landscape) {
        layout.widthMm = std::max(size.width, size.height);
        layout.heightMm = std::min(size.width, size.height);
    } else {
        layout.widthMm = std::min(size.width, size.height);
        layout.heightMm = std::max(size.width, size.height);
    }
    return layout;
}

auto DiagramPageLayout::normalized() const -> DiagramPageLayout
{
    DiagramPageLayout out = *this;
    if (out.widthMm <= 0.0 || out.heightMm <= 0.0) {
        out = defaults(out.standard, out.orientation);
    }

    if (out.orientation == PageOrientation::Landscape && out.heightMm > out.widthMm) {
        std::swap(out.widthMm, out.heightMm);
    }
    if (out.orientation == PageOrientation::Portrait && out.widthMm > out.heightMm) {
        std::swap(out.widthMm, out.heightMm);
    }
    return out;
}

auto DiagramPageLayout::standardName() const -> QString
{
    switch (standard) {
    case PaperStandard::A1: return QStringLiteral("A1");
    case PaperStandard::A2: return QStringLiteral("A2");
    case PaperStandard::A3: return QStringLiteral("A3");
    case PaperStandard::A4:
    default: return QStringLiteral("A4");
    }
}

auto DiagramPageLayout::orientationName() const -> QString
{
    return orientation == PageOrientation::Portrait
               ? QStringLiteral("portrait")
               : QStringLiteral("landscape");
}

auto DiagramPageLayout::standardFromName(const QString& text) -> PaperStandard
{
    const auto s = text.trimmed().toUpper();
    if (s == QStringLiteral("A1")) return PaperStandard::A1;
    if (s == QStringLiteral("A2")) return PaperStandard::A2;
    if (s == QStringLiteral("A3")) return PaperStandard::A3;
    return PaperStandard::A4;
}

auto DiagramPageLayout::orientationFromName(const QString& text) -> PageOrientation
{
    const auto s = text.trimmed().toLower();
    if (s == QStringLiteral("portrait")) return PageOrientation::Portrait;
    return PageOrientation::Landscape;
}

} // namespace s7ui::documents

