// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-S7-plugin-permission
#include "printing/preview_dialog.hpp"

#include <QFileDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QPrintDialog>
#include <QPrintPreviewWidget>
#include <QPrinter>
#include <QPushButton>
#include <QToolButton>
#include <QVBoxLayout>

namespace s7studio::printing {

PreviewDialog::PreviewDialog(DocumentBuilder build, QPageLayout layout, QString suggestedPdf, Mode mode, QWidget* parent)
    : QDialog(parent), build_(std::move(build)), suggestedPdf_(std::move(suggestedPdf)), mode_(mode),
      printer_(std::make_unique<QPrinter>(QPrinter::HighResolution))
{
    setObjectName(QStringLiteral("print.preview"));
    setWindowTitle(QStringLiteral("Print Preview"));
    resize(1100, 760);
    printer_->setPageLayout(layout);
    printer_->setResolution(kPrintDpi);

    auto* root = new QVBoxLayout(this);
    auto* body = new QHBoxLayout();
    root->addLayout(body, 1);
    auto* side = new QWidget(this);
    side->setObjectName(QStringLiteral("print.options"));
    options_ = new QVBoxLayout(side);
    options_->setContentsMargins(0, 0, 0, 0);
    body->addWidget(side);
    preview_ = new QPrintPreviewWidget(printer_.get(), this);
    preview_->setObjectName(QStringLiteral("print.previewPages"));
    body->addWidget(preview_, 1);
    side->hide();
    connect(preview_, &QPrintPreviewWidget::paintRequested, this, [this](QPrinter* target) {
        if (build_) renderError_ = print(build_, *target);
    });

    auto* bar = new QHBoxLayout();
    root->addLayout(bar);
    const auto tool = [this, bar](const QString& text, const QString& tip, auto slot) {
        auto* button = new QToolButton(this);
        button->setText(text);
        button->setToolTip(tip);
        button->setAutoRaise(true);
        connect(button, &QToolButton::clicked, this, slot);
        bar->addWidget(button);
        return button;
    };
    tool(QStringLiteral("◀"), QStringLiteral("Previous page"), [this] { preview_->setCurrentPage(preview_->currentPage() - 1); updateSummary(); });
    summary_ = new QLabel(this);
    summary_->setObjectName(QStringLiteral("print.summary"));
    bar->addWidget(summary_);
    tool(QStringLiteral("▶"), QStringLiteral("Next page"), [this] { preview_->setCurrentPage(preview_->currentPage() + 1); updateSummary(); });
    tool(QStringLiteral("Fit"), QStringLiteral("Fit the page in the window"), [this] { preview_->fitInView(); });
    tool(QStringLiteral("−"), QStringLiteral("Zoom out"), [this] { preview_->zoomOut(); });
    tool(QStringLiteral("+"), QStringLiteral("Zoom in"), [this] { preview_->zoomIn(); });
    connect(preview_, &QPrintPreviewWidget::previewChanged, this, &PreviewDialog::updateSummary);
    bar->addStretch(1);
    error_ = new QLabel(this);
    error_->setObjectName(QStringLiteral("print.error"));
    error_->setStyleSheet(QStringLiteral("color: #b00020;"));
    error_->setWordWrap(true);
    error_->hide();
    bar->addWidget(error_, 2);

    auto* pageSetup = new QPushButton(QStringLiteral("Page Setup…"), this);
    pageSetup->setObjectName(QStringLiteral("print.pageSetup"));
    auto* pdf = new QPushButton(QStringLiteral("Save as PDF…"), this);
    pdf->setObjectName(QStringLiteral("print.pdf"));
    auto* printButton = new QPushButton(QStringLiteral("Print…"), this);
    printButton->setObjectName(QStringLiteral("print.print"));
    printButton->setDefault(true);
    auto* close = new QPushButton(QStringLiteral("Close"), this);
    close->setObjectName(QStringLiteral("print.close"));
    for (auto* button : {pageSetup, pdf, printButton, close}) bar->addWidget(button);
    if (mode_ == Mode::PdfOnly) {
        setWindowTitle(QStringLiteral("Export as PDF"));
        preview_->hide();
        printButton->hide();
        pdf->setDefault(true);
        for (auto* button : bar->parentWidget()->findChildren<QToolButton*>()) button->hide();
        resize(520, 300);
    }

    connect(close, &QPushButton::clicked, this, &QDialog::reject);
    connect(pageSetup, &QPushButton::clicked, this, [this] {
        if (const auto chosen = printing::pageSetup(this, pageLayout())) setPageLayout(*chosen);
    });
    connect(pdf, &QPushButton::clicked, this, [this] {
        auto path = QFileDialog::getSaveFileName(this, QStringLiteral("Save as PDF"), suggestedPdf_, QStringLiteral("PDF files (*.pdf)"));
        if (path.isEmpty()) return;
        if (!path.endsWith(QStringLiteral(".pdf"), Qt::CaseInsensitive)) path += QStringLiteral(".pdf");
        finish(savePdfTo(path));
    });
    connect(printButton, &QPushButton::clicked, this, [this] {
        QPrinter printer(QPrinter::HighResolution);
        printer.setPageLayout(pageLayout());
        printer.setResolution(kPrintDpi);
        QPrintDialog dialog(&printer, this);
        dialog.setOption(QAbstractPrintDialog::PrintSelection, false);
        dialog.setOption(QAbstractPrintDialog::PrintPageRange, false);
        if (dialog.exec() != QDialog::Accepted) return;
        if (const auto error = print(build_, printer); !error.isEmpty()) {
            finish({PrintResult::Outcome::Failed, error});
            return;
        }
        finish({PrintResult::Outcome::Done, printer.outputFileName().isEmpty() ? printer.printerName() : printer.outputFileName()});
    });
    refresh();
}

PreviewDialog::~PreviewDialog() = default;

void PreviewDialog::addOption(QWidget* widget)
{
    options_->addWidget(widget);
    options_->parentWidget()->show();
}

void PreviewDialog::refresh()
{
    renderError_.clear();
    if (mode_ == Mode::Preview) preview_->updatePreview();
    updateSummary();
}

void PreviewDialog::setBuilder(DocumentBuilder build)
{
    build_ = std::move(build);
    refresh();
}

QPageLayout PreviewDialog::pageLayout() const
{
    return printer_->pageLayout();
}

void PreviewDialog::setPageLayout(const QPageLayout& layout)
{
    printer_->setPageLayout(layout);
    emit pageLayoutChanged(layout);
    refresh();
}

int PreviewDialog::pageCount() const
{
    if (mode_ == Mode::PdfOnly) return build_ ? static_cast<int>(build_(pageLayout()).pages.size()) : 0;
    return preview_->pageCount();
}

PrintResult PreviewDialog::savePdfTo(const QString& path)
{
    if (const auto error = savePdf(build_, pageLayout(), path); !error.isEmpty())
        return {PrintResult::Outcome::Failed, QStringLiteral("Could not write %1: %2").arg(path, error)};
    return {PrintResult::Outcome::Done, path};
}

void PreviewDialog::finish(const PrintResult& result)
{
    result_ = result;
    if (result.outcome == PrintResult::Outcome::Failed) {
        error_->setText(result.message);
        error_->show();
        return;
    }
    accept();
}

void PreviewDialog::updateSummary()
{
    if (!renderError_.isEmpty()) {
        error_->setText(renderError_);
        error_->show();
    }
    const int pages = pageCount();
    if (mode_ == Mode::PdfOnly) summary_->setText(pages == 1 ? QStringLiteral("1 page") : QStringLiteral("%1 pages").arg(pages));
    else summary_->setText(pages <= 1 ? QStringLiteral("%1 page").arg(pages)
                                      : QStringLiteral("Page %1 of %2").arg(preview_->currentPage()).arg(pages));
}

} // namespace s7studio::printing
