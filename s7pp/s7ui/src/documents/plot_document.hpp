#pragma once

#include "documents/diagram_document.hpp"

#include <QString>
#include <QVector>

namespace s7ui::documents {

struct PlotTextAttributes
{
    QString title;
    QString subtitle;
    QString xAxisLabel;
    QString yAxisLabel;
    QString units;
    QString comment;
};

struct PlotDataSource
{
    QString path;
    QString format = QStringLiteral("json");
};

struct PlotPage
{
    QString id;
    PlotTextAttributes text;
    PlotDataSource source;
    DiagramPageLayout page = DiagramPageLayout::defaults(PaperStandard::A4, PageOrientation::Portrait);
};

struct PlotDocument
{
    QString id = QStringLiteral("plot_document");
    QVector<PlotPage> pages;
};

} // namespace s7ui::documents

