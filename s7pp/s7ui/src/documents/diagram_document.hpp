#pragma once

#include <QString>

namespace s7ui::documents {

enum class PaperStandard
{
    A4,
    A3,
    A2,
    A1
};

enum class PageOrientation
{
    Landscape,
    Portrait
};

struct DiagramPageLayout
{
    PaperStandard standard = PaperStandard::A4;
    PageOrientation orientation = PageOrientation::Landscape;
    double widthMm = 297.0;
    double heightMm = 210.0;

    static auto defaults(PaperStandard standard,
                         PageOrientation orientation = PageOrientation::Landscape) -> DiagramPageLayout;
    auto normalized() const -> DiagramPageLayout;

    [[nodiscard]] auto standardName() const -> QString;
    [[nodiscard]] auto orientationName() const -> QString;

    static auto standardFromName(const QString& text) -> PaperStandard;
    static auto orientationFromName(const QString& text) -> PageOrientation;
};

struct DiagramDocument
{
    QString id = QStringLiteral("s7ui_schema");
    double dt = 0.001;
    DiagramPageLayout page;
};

} // namespace s7ui::documents

