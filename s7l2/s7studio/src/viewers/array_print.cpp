// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-S7-plugin-permission

#include "viewers/array_print.hpp"

#include <QPainter>
#include <QPdfWriter>
#include <QPrinter>
#include <QSaveFile>

#include <algorithm>

namespace s7studio::viewers {
namespace {

bool render(const ArrayPrintSnapshot& snapshot, QPagedPaintDevice& device)
{
    QPainter painter;
    if (!painter.begin(&device)) return false;
    painter.setRenderHint(QPainter::LosslessImageRendering);
    const auto page = painter.viewport();
    constexpr int width = 760;
    constexpr int height = 970;
    constexpr int margin = 24;
    const auto scale = std::min(double(page.width()) / width, double(page.height()) / height);
    painter.translate((page.width() - width * scale) / 2, (page.height() - height * scale) / 2);
    painter.scale(scale, scale);
    auto font = painter.font();
    const auto text = [&](int y, int pixelSize, const QString& value, int flags = Qt::AlignLeft | Qt::AlignTop) {
        font.setPixelSize(pixelSize);
        painter.setFont(font);
        painter.setPen(Qt::black);
        painter.drawText(QRect(margin, y, width - 2 * margin, pixelSize + 8), flags, value);
    };
    const auto pageHeader = [&](const QString& title, int pageNumber) {
        text(margin, 25, title);
        text(height - 35, 12, QString::number(pageNumber), Qt::AlignRight | Qt::AlignTop);
    };

    pageHeader(snapshot.title, 1);
    int y = 70;
    for (const auto& detail : snapshot.details) {
        text(y, 15, detail);
        y += 24;
    }
    snapshot.draw(painter, QRect(margin, y + 12, width - 2 * margin, height - y - 68));

    int pageNumber = 1;
    y = height;
    for (const auto& row : snapshot.rows) {
        if (y > height - 65) {
            if (!device.newPage()) return false;
            pageHeader(QStringLiteral("Waterfall row details"), ++pageNumber);
            y = 70;
        }
        text(y, 13, row);
        y += 20;
    }
    return painter.end();
}

} // namespace

QString saveArrayPdf(const ArrayPrintSnapshot& snapshot, const QString& path)
{
    QSaveFile output(path);
    if (!output.open(QIODevice::WriteOnly)) return output.errorString();
    {
        QPdfWriter writer(&output);
        writer.setTitle(snapshot.title);
        writer.setPageSize(QPageSize(QPageSize::A4));
        writer.setResolution(144);
        if (!render(snapshot, writer)) return QStringLiteral("Could not render the PDF.");
    }
    if (!output.commit()) return output.errorString();
    return {};
}

QString printArraySnapshot(const ArrayPrintSnapshot& snapshot, QPrinter& printer)
{
    printer.setDocName(snapshot.title);
    if (!render(snapshot, printer) || printer.printerState() == QPrinter::Error ||
        printer.printerState() == QPrinter::Aborted) {
        return QStringLiteral("The printer could not complete the job.");
    }
    return {};
}

} // namespace s7studio::viewers
