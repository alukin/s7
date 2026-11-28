// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-S7-plugin-permission
#include "printing/print_actions.hpp"

#include <QFileDialog>
#include <QLocale>
#include <QPageSetupDialog>
#include <QPrintDialog>
#include <QPrintPreviewDialog>
#include <QPrinter>
#include <QPrinterInfo>

#include <algorithm>

namespace s7studio::printing {
namespace {

const QMarginsF kMarginsMm(10, 10, 10, 10);

void prepare(QPrinter& printer, const QPageLayout& layout)
{
    printer.setPageLayout(layout);
    printer.setResolution(kPrintDpi);
}

} // namespace

PrintResult exportPdf(QWidget* parent, const DocumentBuilder& build, const QPageLayout& layout, const QString& suggestedPath)
{
    auto path = QFileDialog::getSaveFileName(parent, QStringLiteral("Export as PDF"), suggestedPath, QStringLiteral("PDF files (*.pdf)"));
    if (path.isEmpty()) return {};
    if (!path.endsWith(QStringLiteral(".pdf"), Qt::CaseInsensitive)) path += QStringLiteral(".pdf");
    if (const auto error = savePdf(build, layout, path); !error.isEmpty())
        return {PrintResult::Outcome::Failed, QStringLiteral("Could not write %1: %2").arg(path, error)};
    return {PrintResult::Outcome::Done, path};
}

PrintResult printWithDialog(QWidget* parent, const DocumentBuilder& build, const QPageLayout& layout)
{
    QPrinter printer(QPrinter::HighResolution);
    prepare(printer, layout);
    QPrintDialog dialog(&printer, parent);
    dialog.setOption(QAbstractPrintDialog::PrintSelection, false);
    dialog.setOption(QAbstractPrintDialog::PrintPageRange, false);
    if (dialog.exec() != QDialog::Accepted) return {};
    if (const auto error = print(build, printer); !error.isEmpty()) return {PrintResult::Outcome::Failed, error};
    return {PrintResult::Outcome::Done, printer.outputFileName().isEmpty() ? printer.printerName() : printer.outputFileName()};
}

PrintResult preview(QWidget* parent, const DocumentBuilder& build, const QPageLayout& layout)
{
    QPrinter printer(QPrinter::HighResolution);
    prepare(printer, layout);
    QPrintPreviewDialog dialog(&printer, parent);
    dialog.setObjectName(QStringLiteral("print.preview"));
    QString error;
    QObject::connect(&dialog, &QPrintPreviewDialog::paintRequested, &dialog, [&](QPrinter* target) {
        error = print(build, *target);
    });
    const bool printed = dialog.exec() == QDialog::Accepted;
    if (!error.isEmpty()) return {PrintResult::Outcome::Failed, error};
    if (!printed) return {};
    return {PrintResult::Outcome::Done, printer.outputFileName().isEmpty() ? printer.printerName() : printer.outputFileName()};
}

std::optional<QPageLayout> pageSetup(QWidget* parent, const QPageLayout& layout)
{
    QPrinter printer(QPrinter::HighResolution);
    printer.setPageLayout(layout);
    QPageSetupDialog dialog(&printer, parent);
    if (dialog.exec() != QDialog::Accepted) return std::nullopt;
    return printer.pageLayout();
}

QString describe(const QPageLayout& layout)
{
    const auto orientation = layout.orientation() == QPageLayout::Landscape ? QStringLiteral("landscape") : QStringLiteral("portrait");
    if (layout.pageSize().id() != QPageSize::Custom) return QStringLiteral("%1 %2").arg(layout.pageSize().name(), orientation);
    const auto size = layout.fullRect(QPageLayout::Millimeter).size();  // as oriented on paper
    return QStringLiteral("%1 \u00D7 %2 mm %3").arg(QString::number(size.width(), 'f', 0), QString::number(size.height(), 'f', 0), orientation);
}

QPageLayout defaultLayout()
{
    const auto printer = QPrinterInfo::defaultPrinter();
    auto size = printer.isNull() ? QPageSize() : printer.defaultPageSize();
    if (!size.isValid()) {
        const auto country = QLocale::system().territory();
        const bool letter = country == QLocale::UnitedStates || country == QLocale::Canada || country == QLocale::Mexico;
        size = QPageSize(letter ? QPageSize::Letter : QPageSize::A4);
    }
    return QPageLayout(size, QPageLayout::Portrait, kMarginsMm, QPageLayout::Millimeter);
}

QPageLayout sheetLayout(double widthMm, double heightMm, bool landscape)
{
    const QSizeF portrait(std::min(widthMm, heightMm), std::max(widthMm, heightMm));
    const QPageSize size(portrait, QPageSize::Millimeter, QString(), QPageSize::FuzzyMatch);
    return QPageLayout(size, landscape ? QPageLayout::Landscape : QPageLayout::Portrait, kMarginsMm, QPageLayout::Millimeter);
}

} // namespace s7studio::printing
