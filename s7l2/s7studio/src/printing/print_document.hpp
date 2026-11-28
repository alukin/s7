// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-S7-plugin-permission
#pragma once

#include <QPageLayout>
#include <QRectF>
#include <QString>
#include <QStringList>

#include <functional>
#include <vector>

class QImage;
class QPagedPaintDevice;
class QPainter;
class QPrinter;

namespace s7studio::printing {

// Every print job paints in 1/96 inch units, the Studio scene unit, on a
// device set to 96 dpi. Diagram items draw text with point-size fonts, which
// a painter resolves against the device resolution: on a 600 dpi device the
// text would grow relative to the drawing. A 96 dpi coordinate system keeps
// PDF and printer output vector and in proportion; it does not lower the
// printed quality.
inline constexpr int kPrintDpi = 96;

// One printed page: a provenance header, the content and a footer.
struct PrintPage {
    QString title;      // header, first line (bold)
    QString subtitle;   // header, second line
    QString detail;     // header, right-aligned on the first line (area, scale, tile)
    QStringList notes;  // further header lines (ranges, warnings)
    // Paints the content into `area` (1/96 inch, relative to the printable
    // area's top-left corner). The painter is clipped to `area`.
    std::function<void(QPainter& painter, const QRectF& area)> draw;
};

struct PrintDocument {
    QString name;         // PDF title and printer job name
    QString footer;       // left footer; "Page n of m" is added on the right
    std::vector<PrintPage> pages;
};

// Builds a document for the page geometry actually used (a PDF writer, the
// printer the user chose or a print preview).
using DocumentBuilder = std::function<PrintDocument(const QPageLayout& layout)>;

// The content area of `page` on `layout`: the printable area less the header
// and footer, in 1/96 inch relative to the printable area's top-left corner.
[[nodiscard]] QRectF contentArea(const QPageLayout& layout, const PrintPage& page);

// Paints every page onto a device set to kPrintDpi. Returns an error message,
// empty on success. An empty document is an error, never a blank success.
[[nodiscard]] QString render(const PrintDocument& document, QPagedPaintDevice& device);
// Writes a PDF atomically (nothing is left at `path` on failure).
[[nodiscard]] QString savePdf(const DocumentBuilder& build, const QPageLayout& layout, const QString& path);
// Prints on a configured printer, using its page layout.
[[nodiscard]] QString print(const DocumentBuilder& build, QPrinter& printer);
// One page as an image of the whole sheet (white paper), `scale` times 96 dpi.
[[nodiscard]] QImage renderPage(const PrintDocument& document, const QPageLayout& layout, int index, double scale = 1.0);

} // namespace s7studio::printing
