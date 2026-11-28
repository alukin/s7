// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-S7-plugin-permission

// Viewer printing (docs/STUDIO_PRINTING_0.2.0.md, P02/P03/P05/P07): the
// vector re-render of LinePlotView data, axis ticks as on screen, pale trace
// colours on white paper, the viewer page and the preview window.

#include "printing/preview_dialog.hpp"
#include "printing/print_document.hpp"
#include "viewers/line_plot_view.hpp"
#include "viewers/plot_print.hpp"
#include "viewers/viewer_print.hpp"

#include <QApplication>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QLineSeries>
#include <QPainter>
#include <QProcess>
#include <QRegularExpression>
#include <QScatterSeries>
#include <QStandardPaths>
#include <QTemporaryDir>

#include <cmath>
#include <iostream>
#include <numbers>
#include <stdexcept>
#include <string>

namespace {
using namespace s7studio;
using namespace s7studio::viewers;

void require(bool condition, const std::string& message)
{
    if (!condition) throw std::runtime_error(message);
}

bool near(double a, double b) { return std::abs(a - b) < 1e-9; }

void checkTicks()
{
    require(near(niceTickStep(10, 5), 2) && near(niceTickStep(1, 4), 0.25) && near(niceTickStep(7, 6), 2) && near(niceTickStep(0.03, 3), 0.01),
            "1-2-2.5-5 tick steps");
    const auto ticks = axisTicks(-0.1, 1.05, 0.25, 0);
    require(ticks.size() == 5 && near(ticks.front(), 0) && near(ticks.back(), 1), "ticks anchored at 0 inside the range");
    const auto divisions = axisTicks(2, 3, 0.1, 2);
    require(divisions.size() == 11 && near(divisions.front(), 2) && near(divisions.back(), 3), "oscilloscope divisions from the range start");
    require(axisTicks(0, 1, 0, 0).empty() && axisTicks(1, 0, 0.1, 0).empty(), "invalid tick requests give no ticks");
}

void checkColours()
{
    float h = 0, s = 0, l = 0;
    printColor(QColor(QStringLiteral("#ffd400"))).toHsl().getHslF(&h, &s, &l);
    require(l <= 0.45f, "a yellow trace stays pale on paper");
    printColor(QColor(QStringLiteral("#54b8ff"))).toHsl().getHslF(&h, &s, &l);
    require(l <= 0.45f, "an instrument light blue stays pale on paper");
    require(printColor(QColor(QStringLiteral("#1f77b4"))) == QColor(QStringLiteral("#1f77b4")), "a dark trace colour changed");
}

int inkOfColour(const QImage& image, const QColor& colour, const QRect& area)
{
    int count = 0;
    for (int y = area.top(); y <= area.bottom(); ++y)
        for (int x = area.left(); x <= area.right(); ++x) {
            const QColor pixel = image.pixelColor(x, y);
            if (std::abs(pixel.red() - colour.red()) + std::abs(pixel.green() - colour.green()) + std::abs(pixel.blue() - colour.blue()) < 60) ++count;
        }
    return count;
}

int pdfPages(const QString& path)
{
    QFile file(path);
    require(file.open(QIODevice::ReadOnly), "PDF missing: " + path.toStdString());
    return static_cast<int>(QString::fromLatin1(file.readAll()).count(QRegularExpression(QStringLiteral("/Type\\s*/Page(?![s\\w])"))));
}

QString pdfText(const QString& path)
{
    const auto tool = QStandardPaths::findExecutable(QStringLiteral("pdftotext"));
    if (tool.isEmpty()) return {};
    QProcess process;
    process.start(tool, {QStringLiteral("-enc"), QStringLiteral("UTF-8"), path, QStringLiteral("-")});
    require(process.waitForFinished(30000) && process.exitCode() == 0, "pdftotext failed");
    return QString::fromUtf8(process.readAllStandardOutput());
}

void checkPlot(const QString& out)
{
    LinePlotView view;
    view.resize(800, 500);
    const QColor blue(QStringLiteral("#1f77b4")), yellow(QStringLiteral("#ffd400"));
    auto* sine = view.addSeries(QStringLiteral("sine"), {blue});
    auto* hidden = view.addSeries(QStringLiteral("hidden"), {QColor(Qt::red)});
    auto* overlay = view.addSeries(QStringLiteral("previous run"), {yellow, 1.2, true});
    auto* marker = view.addScatterSeries(QStringLiteral("Newest pair"), QColor(QStringLiteral("#d62728")));
    QList<QPointF> points, shifted;
    for (int i = 0; i <= 400; ++i) {
        const double t = i / 400.0;
        points.append({t, std::sin(2 * std::numbers::pi * t)});
        shifted.append({t, 0.5 * std::sin(2 * std::numbers::pi * t)});
    }
    points[200] = {std::nan(""), std::nan("")};  // a recording gap
    LinePlotView::setPoints(sine, points);
    LinePlotView::setPoints(hidden, shifted);
    LinePlotView::setPoints(overlay, shifted);
    LinePlotView::setScatterPoints(marker, {QPointF(0.25, 1.0)});
    view.setSeriesVisible(hidden, false);
    view.setAxisTitles(QStringLiteral("model time (s)"), QStringLiteral("value (V)"));
    view.setLegendVisible(true);
    view.setXRange(0, 1);
    view.setYRange(-1.2, 1.2);

    auto plot = snapshotPlot(view);
    require(plot.series.size() == 3, "visible series only: " + std::to_string(plot.series.size()));
    require(plot.series[0].name == QStringLiteral("sine") && plot.series[0].points.size() == 401 && plot.series[0].kind == PlotSnapshot::Series::Kind::Line,
            "line series copied");
    require(plot.series[1].overlay && plot.series[2].kind == PlotSnapshot::Series::Kind::Markers, "overlay and marker kinds copied");
    require(plot.legend.size() == 3 && plot.legendVisible && plot.legend[2].marker && !plot.legend[0].marker, "legend lists the visible series");
    require(near(plot.xMin, 0) && near(plot.xMax, 1) && near(plot.yMin, -1.2) && near(plot.yMax, 1.2) && plot.xTitle == QStringLiteral("model time (s)"),
            "ranges and titles copied");

    // A later change to the view does not change the snapshot.
    LinePlotView::setPoints(sine, {});
    require(plot.series[0].points.size() == 401, "the snapshot shares the view's points");

    // Painted into a sub-rectangle: the trace colour is there, nothing leaks out.
    QImage image(1000, 700, QImage::Format_ARGB32);
    image.setDotsPerMeterX(static_cast<int>(std::lround(96 / 0.0254)));
    image.setDotsPerMeterY(static_cast<int>(std::lround(96 / 0.0254)));
    image.fill(Qt::white);
    const QRect area(100, 100, 800, 500);
    {
        QPainter painter(&image);
        paintPlot(painter, area, plot);
    }
    image.save(out + QStringLiteral("/plot.png"));
    require(inkOfColour(image, blue, area) > 400, "the sine trace was not drawn");
    // The overlay is a dashed, translucent line in the darkened yellow: count
    // saturated pixels of its hue (antialiasing lightens thin lines).
    float overlayHue = 0, unused = 0;
    printColor(yellow).toHsl().getHslF(&overlayHue, &unused, &unused);
    int overlayInk = 0;
    for (int y = area.top(); y <= area.bottom(); ++y)
        for (int x = area.left(); x <= area.right(); ++x) {
            float hue = 0, saturation = 0, lightness = 0;
            image.pixelColor(x, y).toHsl().getHslF(&hue, &saturation, &lightness);
            if (saturation > 0.4f && lightness < 0.85f && std::abs(hue - overlayHue) < 0.04f) ++overlayInk;
        }
    require(overlayInk > 100, "the overlay was not drawn in its darkened colour: " + std::to_string(overlayInk));
    for (int y = 0; y < image.height(); ++y)
        for (int x = 0; x < image.width(); ++x)
            if (!area.adjusted(-1, -1, 1, 1).contains(x, y) && image.pixel(x, y) != qRgb(255, 255, 255))
                throw std::runtime_error("the plot painted outside its area");

    // The viewer page: one page, the header names it, the plot is text and vectors.
    ViewerPrintSnapshot snapshot;
    snapshot.title = QStringLiteral("Scope · test");
    snapshot.notes << QStringLiteral("Visible range: model time 0 s to 1 s.");
    snapshot.plots.push_back(plot);
    const auto pdf = out + QStringLiteral("/viewer.pdf");
    const auto layout = viewerPageLayout();
    require(printing::savePdf([&](const QPageLayout&) { return viewerDocument(snapshot, QStringLiteral("footer")); }, layout, pdf).isEmpty(),
            "viewer PDF failed");
    require(pdfPages(pdf) == 1, "viewer PDF page count");
    if (const auto text = pdfText(pdf); !text.isEmpty()) {
        for (const auto* expected : {"Scope · test", "Visible range: model time 0 s to 1 s.", "model time (s)", "value (V)", "sine",
                                     "previous run (overlay)", "0.5", "-1.2", "Page 1 of 1"})
            require(text.contains(QString::fromUtf8(expected)), std::string("viewer PDF lacks: ") + expected);
    } else {
        std::cout << "pdftotext not found: PDF text not inspected\n";
    }

    // The preview window: pages, PDF-only mode and a missing builder.
    const printing::DocumentBuilder build = [&](const QPageLayout&) { return viewerDocument(snapshot, QStringLiteral("footer")); };
    printing::PreviewDialog preview(build, layout, {});
    require(preview.pageCount() == 1, "preview page count");
    printing::PreviewDialog pdfOnly(build, layout, {}, printing::PreviewDialog::Mode::PdfOnly);
    require(pdfOnly.pageCount() == 1, "PDF-only page count");
    const auto saved = pdfOnly.savePdfTo(out + QStringLiteral("/pdf-only.pdf"));
    require(saved.done() && pdfPages(saved.message) == 1, "PDF-only save failed");
    const auto failed = pdfOnly.savePdfTo(out + QStringLiteral("/no/such/dir/x.pdf"));
    require(failed.outcome == printing::PrintResult::Outcome::Failed, "an unwritable PDF reported success");
    printing::PreviewDialog empty({}, layout, {});
    require(empty.pageCount() == 0 && empty.result().outcome == printing::PrintResult::Outcome::Cancelled, "an empty preview");
}

} // namespace

int main(int argc, char** argv)
try {
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);
    QApplication::setOrganizationName(QStringLiteral("s7-plot-print-test"));
    QTemporaryDir temporary;
    require(temporary.isValid(), "no temporary directory");
    const auto out = qEnvironmentVariable("S7_PRINT_ARTIFACT_DIR", temporary.path());
    checkTicks();
    checkColours();
    checkPlot(out);
    std::cout << "plot printing checks passed\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
}
