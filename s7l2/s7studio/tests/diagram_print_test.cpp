// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-S7-plugin-permission

// Diagram printing (docs/STUDIO_PRINTING_0.2.0.md, P01 and P07): PDF pages,
// paper geometry, fit and tiling, selection, nested subsystems, snapshot
// isolation, non-ASCII text and failure paths.

#include "diagram/diagram_scene.hpp"
#include "diagram/items.hpp"
#include "diagram_print.hpp"
#include "printing/print_actions.hpp"
#include "printing/print_document.hpp"
#include "scene_serializer.hpp"

#include <s7/builtin_element_registry.hpp>
#include <s7/element_registry.hpp>

#include <boost/json.hpp>

#include <QApplication>
#include <QBuffer>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QPdfWriter>
#include <QPrinter>
#include <QPrintPreviewWidget>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTemporaryDir>

#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
using namespace s7studio;
using namespace s7studio::diagram;

void require(bool condition, const std::string& message)
{
    if (!condition) throw std::runtime_error(message);
}

boost::json::value readJson(const QString& path)
{
    QFile file(path);
    require(file.open(QIODevice::ReadOnly), "cannot read " + path.toStdString());
    const auto bytes = file.readAll();
    return boost::json::parse(std::string_view(bytes.constData(), static_cast<std::size_t>(bytes.size())));
}

// Embedded subsystem schemas at all depths, counted from the file itself.
int nestedSchemas(const boost::json::object& schema)
{
    int count = 0;
    for (const auto& element : schema.at("elements").as_array()) {
        if (const auto* nested = element.as_object().if_contains("schema"); nested && nested->is_object())
            count += 1 + nestedSchemas(nested->as_object());
    }
    return count;
}

struct PdfInfo {
    int pages = 0;
    QSizeF mediaBox;  // points, of the first page
};

// Qt writes page dictionaries uncompressed: count "/Type /Page" (not /Pages)
// and read the first MediaBox.
PdfInfo inspectPdf(const QString& path)
{
    QFile file(path);
    require(file.open(QIODevice::ReadOnly), "PDF was not written: " + path.toStdString());
    const auto text = QString::fromLatin1(file.readAll());
    PdfInfo info;
    info.pages = static_cast<int>(text.count(QRegularExpression(QStringLiteral("/Type\\s*/Page(?![s\\w])"))));
    const auto box = QRegularExpression(QStringLiteral("/MediaBox\\s*\\[\\s*([\\d.]+)\\s+([\\d.]+)\\s+([\\d.]+)\\s+([\\d.]+)")).match(text);
    if (box.hasMatch()) info.mediaBox = {box.captured(3).toDouble() - box.captured(1).toDouble(), box.captured(4).toDouble() - box.captured(2).toDouble()};
    return info;
}

bool close(double actual, double expected, double tolerance) { return std::abs(actual - expected) <= tolerance; }

// Non-white pixels in a rectangle of an image.
int inkIn(const QImage& image, const QRect& area)
{
    int ink = 0;
    const auto clipped = area.intersected(image.rect());
    for (int y = clipped.top(); y <= clipped.bottom(); ++y)
        for (int x = clipped.left(); x <= clipped.right(); ++x)
            if (qGray(image.pixel(x, y)) < 200) ++ink;
    return ink;
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

QByteArray sceneJson(const DiagramScene& scene)
{
    return QByteArray::fromStdString(boost::json::serialize(SceneSerializer{}.serialize(scene, {}).toJson()));
}

const QPageLayout kLetterPortrait(QPageSize(QPageSize::Letter), QPageLayout::Portrait, QMarginsF(10, 10, 10, 10), QPageLayout::Millimeter);

void checkDiagram(const QString& examples, const QDir& out)
{
    const auto json = readJson(examples + QStringLiteral("/double_pendulum_full.s7diagram"));
    const auto document = DiagramDocument::fromJson(json);
    DiagramScene live;
    SceneSerializer{}.deserialize(document, live);
    const auto before = sceneJson(live);
    const auto undoIndex = live.undoStack()->index();

    DiagramPrintJob job(live, document.pageLayout, QStringLiteral("Double pendulum"), {}, QStringLiteral("double_pendulum_full.s7diagram"));
    require(sceneJson(live) == before && live.undoStack()->index() == undoIndex, "creating a print job changed the diagram");
    require(job.subsystemError().isEmpty(), "subsystems failed: " + job.subsystemError().toStdString());
    require(job.subsystemCount() == nestedSchemas(json.at("schema").as_object()) && job.subsystemCount() == 4,
            "nested subsystem count differs from the file");
    require(!job.hasSelection(), "a fresh diagram has no selection");

    // The diagram's own sheet is the default paper: 490 x 320 mm landscape.
    const auto sheet = job.sheetLayout();
    require(sheet.orientation() == QPageLayout::Landscape && close(sheet.fullRect(QPageLayout::Millimeter).width(), 490, 0.5) &&
            close(sheet.fullRect(QPageLayout::Millimeter).height(), 320, 0.5), "sheet paper differs from the diagram sheet");
    require(printing::describe(sheet) == QStringLiteral("490 \u00D7 320 mm landscape") &&
            printing::describe(kLetterPortrait) == QPageSize(QPageSize::Letter).name() + QStringLiteral(" portrait"), "paper descriptions: " + printing::describe(sheet).toStdString());

    const QString footer = QStringLiteral("S7 Studio test");
    const auto fit = [&](const QPageLayout&, DiagramPrintOptions options = {}) {
        return [&job, footer, options](const QPageLayout& used) { return job.build(used, options, footer); };
    };

    // Fit on the sheet: one page of the sheet's size.
    const auto fitPdf = out.filePath(QStringLiteral("fit.pdf"));
    require(printing::savePdf(fit(sheet), sheet, fitPdf).isEmpty(), "fit PDF failed");
    auto info = inspectPdf(fitPdf);
    require(info.pages == 1, "fit PDF has " + std::to_string(info.pages) + " pages, expected 1");
    require(close(info.mediaBox.width(), 490 / 25.4 * 72, 2) && close(info.mediaBox.height(), 320 / 25.4 * 72, 2),
            "fit PDF page is not the 490 x 320 mm sheet");

    // Where the drawing lands: every element inside the content area, the
    // region centred, nothing drawn between the content and the footer.
    const auto pages = job.build(sheet, {}, footer);
    const auto image = printing::renderPage(pages, sheet, 0);
    const auto area = printing::contentArea(sheet, pages.pages[0]).translated(sheet.paintRectPixels(printing::kPrintDpi).topLeft());
    const auto region = DiagramPrintJob::diagramBounds(live, document.pageLayout);
    const double scale = std::min(area.width() / region.width(), area.height() / region.height());
    const QPointF origin(area.left() + (area.width() - region.width() * scale) / 2, area.top() + (area.height() - region.height() * scale) / 2);
    int nodes = 0;
    for (const auto& node : live.elementNodes()) {
        const auto rect = node->sceneBoundingRect();
        require(region.contains(rect), "element outside the printed region: " + node->id());
        const QRectF mapped(origin + (rect.topLeft() - region.topLeft()) * scale, rect.size() * scale);
        require(area.contains(mapped), "element mapped outside the content area: " + node->id());
        require(inkIn(image, mapped.toAlignedRect()) > 0, "element not drawn where expected: " + node->id());
        ++nodes;
    }
    require(nodes > 0, "the example has no elements");
    require(pages.pages[0].detail.contains(QStringLiteral("fit to page")), "fit page does not state its scale");

    // Text, including Greek letters and math symbols, stays text in the PDF.
    if (const auto text = pdfText(fitPdf); !text.isEmpty()) {
        for (const auto* expected : {"Double pendulum", "Page 1 of 1", "S7 Studio test", "δ = θ1 − θ2"})
            require(text.contains(QString::fromUtf8(expected)), std::string("PDF text lacks: ") + expected);
    } else {
        std::cout << "pdftotext not found: PDF text not inspected\n";
    }

    // Subsystems: one page per nested level, titled with its path.
    DiagramPrintOptions withSubsystems;
    withSubsystems.includeSubsystems = true;
    const auto all = job.build(kLetterPortrait, withSubsystems, footer);
    require(static_cast<int>(all.pages.size()) == 1 + job.subsystemCount(), "subsystem pages missing");
    for (std::size_t index = 1; index < all.pages.size(); ++index) {
        require(all.pages[index].title.contains(QStringLiteral("▸")), "subsystem page lacks its path");
        const auto page = printing::renderPage(all, kLetterPortrait, static_cast<int>(index));
        const auto content = printing::contentArea(kLetterPortrait, all.pages[index]).translated(kLetterPortrait.paintRectPixels(printing::kPrintDpi).topLeft());
        require(inkIn(page, content.toAlignedRect()) > 50, "subsystem page " + std::to_string(index) + " is blank");
    }
    const auto lettersPdf = out.filePath(QStringLiteral("subsystems-letter.pdf"));
    require(printing::savePdf([&](const QPageLayout& used) { return job.build(used, withSubsystems, footer); }, kLetterPortrait, lettersPdf).isEmpty(),
            "subsystem PDF failed");
    info = inspectPdf(lettersPdf);
    require(info.pages == 5 && close(info.mediaBox.width(), 612, 1) && close(info.mediaBox.height(), 792, 1),
            "Letter portrait PDF has the wrong page count or size");

    // The printer path (print dialog and preview) uses the printer's page
    // layout and a 96 dpi coordinate system on a high-resolution printer.
    {
        QPrinter printer(QPrinter::HighResolution);
        printer.setOutputFormat(QPrinter::PdfFormat);
        const auto printed = out.filePath(QStringLiteral("printer-letter.pdf"));
        printer.setOutputFileName(printed);
        printer.setPageLayout(kLetterPortrait);
        require(printing::print([&](const QPageLayout& used) { return job.build(used, withSubsystems, footer); }, printer).isEmpty(),
                "printing to a PDF printer failed");
        info = inspectPdf(printed);
        require(info.pages == 5 && close(info.mediaBox.width(), 612, 1) && close(info.mediaBox.height(), 792, 1),
                "the printer did not use its Letter portrait layout");
    }

    // Print preview paints through the preview's own printer.
    {
        QPrinter printer(QPrinter::HighResolution);
        printer.setPageLayout(kLetterPortrait);
        QPrintPreviewWidget preview(&printer);
        QString error = QStringLiteral("preview did not paint");
        QObject::connect(&preview, &QPrintPreviewWidget::paintRequested, [&](QPrinter* target) {
            error = printing::print([&](const QPageLayout& used) { return job.build(used, withSubsystems, footer); }, *target);
        });
        preview.updatePreview();
        require(error.isEmpty(), "print preview failed: " + error.toStdString());
        require(preview.pageCount() == 5, "print preview shows " + std::to_string(preview.pageCount()) + " pages, expected 5");
    }

    // Tiling at 200 % on A4 portrait covers the region with overlapping tiles.
    const QPageLayout a4(QPageSize(QPageSize::A4), QPageLayout::Portrait, QMarginsF(10, 10, 10, 10), QPageLayout::Millimeter);
    DiagramPrintOptions tiled;
    tiled.tile = true;
    tiled.tileScale = 2.0;
    const auto tiles = job.build(a4, tiled, footer);
    const auto tileArea = printing::contentArea(a4, tiles.pages[0]);
    const QSizeF tileSize = tileArea.size() / 2.0;
    const double overlap = 10 * printing::kPrintDpi / 25.4 / 2.0;
    const int columns = static_cast<int>(std::ceil((region.width() - overlap) / (tileSize.width() - overlap)));
    const int rows = static_cast<int>(std::ceil((region.height() - overlap) / (tileSize.height() - overlap)));
    require(static_cast<int>(tiles.pages.size()) == rows * columns && rows * columns > 4,
            "tile count " + std::to_string(tiles.pages.size()) + " differs from " + std::to_string(rows * columns));
    require(tiles.pages.back().detail.contains(QStringLiteral("tile %1 of %1").arg(rows * columns)) &&
            tiles.pages.back().detail.contains(QStringLiteral("200 %")), "tile page does not state its position and scale");

    // A selection prints larger than the whole diagram.
    auto node = live.elementNodes().front();
    node->setSelected(true);
    DiagramPrintJob selected(live, document.pageLayout, QStringLiteral("Double pendulum"), {}, {});
    require(selected.hasSelection() && selected.selectionBounds().contains(node->sceneBoundingRect()), "selection bounds wrong");
    DiagramPrintOptions selection;
    selection.area = DiagramPrintOptions::Area::Selection;
    const auto selectedPages = selected.build(sheet, selection, footer);
    require(selectedPages.pages.size() == 1 && selectedPages.pages[0].detail.startsWith(QStringLiteral("Selection")), "selection page missing");
    node->setSelected(false);

    // The job is a snapshot: moving an element afterwards changes nothing printed.
    const auto snapshot = printing::renderPage(pages, sheet, 0);
    node->setPos(node->pos() + QPointF(400, 400));
    require(printing::renderPage(pages, sheet, 0) == snapshot, "a later edit changed the printed snapshot");
}

void checkFailures(const QDir& out)
{
    printing::PrintDocument empty;
    const auto layout = printing::sheetLayout(297, 210, true);
    require(!printing::savePdf([&](const QPageLayout&) { return empty; }, layout, out.filePath(QStringLiteral("empty.pdf"))).isEmpty(),
            "an empty document reported success");
    require(!QFileInfo::exists(out.filePath(QStringLiteral("empty.pdf"))), "an empty document left a file");

    printing::PrintDocument one;
    one.pages.push_back({QStringLiteral("Title"), {}, {}, {}, [](QPainter&, const QRectF&) {}});
    const auto missing = out.filePath(QStringLiteral("no/such/dir/out.pdf"));
    require(!printing::savePdf([&](const QPageLayout&) { return one; }, layout, missing).isEmpty() && !QFileInfo::exists(missing),
            "an unwritable PDF path reported success");

    // A device that is not at 96 dpi is refused, not drawn out of proportion.
    QBuffer buffer;
    buffer.open(QIODevice::WriteOnly);
    QPdfWriter writer(&buffer);
    writer.setResolution(300);
    require(!printing::render(one, writer).isEmpty(), "a 300 dpi device was accepted");
}

} // namespace

int main(int argc, char** argv)
try {
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);
    s7::register_builtin_elements(s7::ElementRegistry::instance());
    QTemporaryDir temporary;
    require(temporary.isValid(), "no temporary directory");
    const QDir out(qEnvironmentVariable("S7_PRINT_ARTIFACT_DIR", temporary.path()));
    checkDiagram(QStringLiteral(S7_PRINT_TEST_EXAMPLES), out);
    checkFailures(out);
    std::cout << "diagram printing checks passed\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
}
