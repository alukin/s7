// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-S7-plugin-permission
#include "printing/print_document.hpp"

#include <QImage>
#include <QPainter>
#include <QPdfWriter>
#include <QPrinter>
#include <QSaveFile>

#include <cmath>

namespace s7studio::printing {
namespace {

// Fixed line pitches keep pagination independent of font metrics.
constexpr double kTitleLine = 20;
constexpr double kTextLine = 15;
constexpr double kHeaderGap = 8;
constexpr double kFooterHeight = 20;

double headerHeight(const PrintPage& page)
{
    double height = kTitleLine;
    if (!page.subtitle.isEmpty()) height += kTextLine;
    height += kTextLine * static_cast<double>(page.notes.size());
    return height + kHeaderGap;
}

QRectF printableArea(const QPageLayout& layout)
{
    return QRectF(QPointF(0, 0), QSizeF(layout.paintRectPixels(kPrintDpi).size()));
}

void paintPage(QPainter& painter, const QRectF& printable, const PrintPage& page, const QString& footer, int number, int count)
{
    painter.save();
    QFont font = painter.font();
    font.setPointSizeF(10);
    font.setBold(true);
    painter.setFont(font);
    painter.setPen(Qt::black);
    const double width = printable.width();
    QRectF line(0, 0, width, kTitleLine);
    painter.drawText(line, Qt::AlignLeft | Qt::AlignVCenter, page.title);
    font.setBold(false);
    font.setPointSizeF(8);
    painter.setFont(font);
    painter.drawText(line, Qt::AlignRight | Qt::AlignVCenter, page.detail);
    double y = kTitleLine;
    QStringList lines;
    if (!page.subtitle.isEmpty()) lines << page.subtitle;
    lines << page.notes;
    for (const auto& text : lines) {
        painter.drawText(QRectF(0, y, width, kTextLine), Qt::AlignLeft | Qt::AlignVCenter, text);
        y += kTextLine;
    }
    painter.setPen(QPen(QColor(0x80, 0x80, 0x80), 0.75));
    painter.drawLine(QPointF(0, y + kHeaderGap / 2), QPointF(width, y + kHeaderGap / 2));
    const double footerTop = printable.height() - kFooterHeight;
    painter.drawLine(QPointF(0, footerTop + 2), QPointF(width, footerTop + 2));
    painter.setPen(Qt::black);
    const QRectF footerLine(0, footerTop + 4, width, kFooterHeight - 4);
    painter.drawText(footerLine, Qt::AlignLeft | Qt::AlignVCenter, footer);
    painter.drawText(footerLine, Qt::AlignRight | Qt::AlignVCenter, QStringLiteral("Page %1 of %2").arg(number).arg(count));
    painter.restore();

    const auto area = QRectF(0, headerHeight(page), width, footerTop - headerHeight(page));
    if (page.draw && area.isValid()) {
        painter.save();
        painter.setClipRect(area);
        page.draw(painter, area);
        painter.restore();
    }
}

} // namespace

QRectF contentArea(const QPageLayout& layout, const PrintPage& page)
{
    const auto printable = printableArea(layout);
    const double top = headerHeight(page);
    return {0, top, printable.width(), printable.height() - kFooterHeight - top};
}

QString render(const PrintDocument& document, QPagedPaintDevice& device)
{
    if (document.pages.empty()) return QStringLiteral("There is nothing to print.");
    if (device.logicalDpiX() != kPrintDpi || device.logicalDpiY() != kPrintDpi)
        return QStringLiteral("The output device did not accept a %1 dpi coordinate system.").arg(kPrintDpi);
    const auto printable = printableArea(device.pageLayout());
    if (contentArea(device.pageLayout(), document.pages.front()).height() < 50)
        return QStringLiteral("The page is too small for the header, footer and content.");
    QPainter painter;
    if (!painter.begin(&device)) return QStringLiteral("Could not start the print job.");
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::TextAntialiasing);
    painter.setRenderHint(QPainter::LosslessImageRendering);
    const int count = static_cast<int>(document.pages.size());
    for (int index = 0; index < count; ++index) {
        if (index > 0 && !device.newPage()) {
            painter.end();
            return QStringLiteral("Could not start page %1.").arg(index + 1);
        }
        paintPage(painter, printable, document.pages[static_cast<std::size_t>(index)], document.footer, index + 1, count);
    }
    if (!painter.end()) return QStringLiteral("Could not finish the print job.");
    return {};
}

QString savePdf(const DocumentBuilder& build, const QPageLayout& layout, const QString& path)
{
    const auto document = build(layout);
    if (document.pages.empty()) return QStringLiteral("There is nothing to print.");
    QSaveFile output(path);
    if (!output.open(QIODevice::WriteOnly)) return output.errorString();
    {
        QPdfWriter writer(&output);
        writer.setTitle(document.name);
        writer.setCreator(QStringLiteral("S7 Studio"));
        writer.setResolution(kPrintDpi);
        if (!writer.setPageLayout(layout)) return QStringLiteral("The PDF writer rejected the page layout.");
        if (const auto error = render(document, writer); !error.isEmpty()) return error;
    }
    if (!output.commit()) return output.errorString();
    return {};
}

QString print(const DocumentBuilder& build, QPrinter& printer)
{
    printer.setResolution(kPrintDpi);
    const auto document = build(printer.pageLayout());
    printer.setDocName(document.name);
    if (const auto error = render(document, printer); !error.isEmpty()) return error;
    if (printer.printerState() == QPrinter::Error) return QStringLiteral("The printer reported an error.");
    if (printer.printerState() == QPrinter::Aborted) return QStringLiteral("The print job was aborted.");
    return {};
}

QImage renderPage(const PrintDocument& document, const QPageLayout& layout, int index, double scale)
{
    const auto paper = layout.fullRectPixels(kPrintDpi);
    QImage image(QSize(static_cast<int>(std::ceil(paper.width() * scale)), static_cast<int>(std::ceil(paper.height() * scale))),
                 QImage::Format_ARGB32_Premultiplied);
    image.setDotsPerMeterX(static_cast<int>(std::lround(kPrintDpi / 0.0254)));
    image.setDotsPerMeterY(static_cast<int>(std::lround(kPrintDpi / 0.0254)));
    image.fill(Qt::white);
    if (index < 0 || index >= static_cast<int>(document.pages.size())) return image;
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::TextAntialiasing);
    painter.scale(scale, scale);
    const auto printable = layout.paintRectPixels(kPrintDpi);
    painter.translate(printable.topLeft());
    paintPage(painter, printableArea(layout), document.pages[static_cast<std::size_t>(index)], document.footer,
              index + 1, static_cast<int>(document.pages.size()));
    return image;
}

} // namespace s7studio::printing
