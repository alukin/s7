#include "print_render_service.hpp"

#include <qcp.h>
#include "painter.h"

#include <QGraphicsScene>
#include <QPainter>
#include <QPrintEngine>
#include <QPrinter>
#include <QRectF>

namespace s7ui::printing {
namespace {

[[nodiscard]] auto sourceRectForScene(const QGraphicsScene& scene) -> QRectF
{
    const QRectF sceneRect = scene.sceneRect();
    if (!sceneRect.isEmpty()) {
        return sceneRect;
    }
    return scene.itemsBoundingRect();
}

[[nodiscard]] auto paintRectForPrinter(const QPrinter& printer) -> QSize
{
    return printer.pageLayout().paintRectPixels(printer.resolution()).size();
}

} // namespace

QPageLayout pageLayout(const PageSpec& spec)
{
    QPageLayout layout;
    layout.setPageSize(QPageSize(spec.pageSizeId));
    layout.setOrientation(spec.orientation);
    layout.setUnits(QPageLayout::Millimeter);
    layout.setMargins(spec.marginsMm);
    layout.setMode(QPageLayout::StandardMode);
    return layout;
}

QSize paintRectPixels(const QPageLayout& layout, const int dpi)
{
    return layout.paintRectPixels(dpi).size();
}

void configurePrinter(QPrinter& printer,
                      const PageSpec& spec,
                      const QString& outputFileName)
{
    printer.setOutputFormat(outputFileName.isEmpty() ? QPrinter::NativeFormat : QPrinter::PdfFormat);
    if (!outputFileName.isEmpty()) {
        printer.setOutputFileName(outputFileName);
    }
    printer.setResolution(spec.resolutionDpi);
    printer.setFullPage(true);
    printer.setPageLayout(pageLayout(spec));
}

bool renderSceneToPrinter(QGraphicsScene& scene, QPrinter& printer)
{
    QPainter painter(&printer);
    if (!painter.isActive()) {
        return false;
    }

    const QRectF src = sourceRectForScene(scene);
    const QSize dstSize = paintRectForPrinter(printer);
    scene.render(&painter,
                 QRectF(0.0, 0.0, dstSize.width(), dstSize.height()),
                 src,
                 Qt::IgnoreAspectRatio);
    painter.end();
    return true;
}

bool renderScenePdf(QGraphicsScene& scene,
                    const QString& fileName,
                    const PageSpec& spec,
                    const QString& creator,
                    const QString& title)
{
    if (fileName.isEmpty()) {
        return false;
    }

    QPrinter printer(QPrinter::ScreenResolution);
    configurePrinter(printer, spec, fileName);
    printer.printEngine()->setProperty(QPrintEngine::PPK_Creator, creator);
    printer.printEngine()->setProperty(QPrintEngine::PPK_DocumentName, title);
    return renderSceneToPrinter(scene, printer);
}

bool renderPlotToPrinter(QCustomPlot& plot, QPrinter& printer, const bool nonCosmeticPens)
{
    QCPPainter painter(&printer);
    if (!painter.isActive()) {
        return false;
    }

    painter.setMode(QCPPainter::pmVectorized);
    painter.setMode(QCPPainter::pmNoCaching);
    painter.setMode(QCPPainter::pmNonCosmetic, nonCosmeticPens);

    const QSize dstSize = paintRectForPrinter(printer);
    plot.toPainter(&painter, dstSize.width(), dstSize.height());
    painter.end();
    return true;
}

bool renderPlotPdf(QCustomPlot& plot,
                   const QString& fileName,
                   const PageSpec& spec,
                   const bool nonCosmeticPens,
                   const QString& creator,
                   const QString& title)
{
    if (fileName.isEmpty()) {
        return false;
    }

    QPrinter printer(QPrinter::ScreenResolution);
    configurePrinter(printer, spec, fileName);
    printer.printEngine()->setProperty(QPrintEngine::PPK_Creator, creator);
    printer.printEngine()->setProperty(QPrintEngine::PPK_DocumentName, title);
    return renderPlotToPrinter(plot, printer, nonCosmeticPens);
}

} // namespace s7ui::printing



