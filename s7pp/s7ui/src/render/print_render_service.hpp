#pragma once

#include <QMarginsF>
#include <QPageLayout>
#include <QPageSize>
#include <QSize>
#include <QString>

class QCustomPlot;
class QGraphicsScene;
class QPrinter;

namespace s7ui::printing {

struct PageSpec
{
    QPageSize::PageSizeId pageSizeId = QPageSize::A4;
    QPageLayout::Orientation orientation = QPageLayout::Landscape;
    QMarginsF marginsMm = QMarginsF(0.0, 0.0, 0.0, 0.0);
    int resolutionDpi = 96;
};

[[nodiscard]] QPageLayout pageLayout(const PageSpec& spec);
[[nodiscard]] QSize paintRectPixels(const QPageLayout& layout, int dpi);

void configurePrinter(QPrinter& printer,
                      const PageSpec& spec,
                      const QString& outputFileName = QString());

[[nodiscard]] bool renderSceneToPrinter(QGraphicsScene& scene, QPrinter& printer);
[[nodiscard]] bool renderScenePdf(QGraphicsScene& scene,
                                  const QString& fileName,
                                  const PageSpec& spec,
                                  const QString& creator = QString(),
                                  const QString& title = QString());

[[nodiscard]] bool renderPlotToPrinter(QCustomPlot& plot, QPrinter& printer, bool nonCosmeticPens);
[[nodiscard]] bool renderPlotPdf(QCustomPlot& plot,
                                 const QString& fileName,
                                 const PageSpec& spec,
                                 bool nonCosmeticPens = true,
                                 const QString& creator = QString(),
                                 const QString& title = QString());

} // namespace s7ui::printing






