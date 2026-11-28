// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-S7-plugin-permission
#pragma once

#include "printing/print_actions.hpp"

#include <QDialog>

class QLabel;
class QPrinter;
class QPrintPreviewWidget;
class QPushButton;
class QVBoxLayout;

namespace s7studio::printing {

// Every print starts here: the pages as they will print, with Print…,
// Save as PDF…, Page Setup… and Close. Closing prints and writes nothing.
// PdfOnly mode (a PDF wastes no paper) shows only the options, the page
// count, Page Setup… and Save as PDF….
// Callers may add an options panel; call refresh() when an option changes.
// Object names: print.preview (this dialog), print.previewPages,
// print.print, print.pdf, print.pageSetup, print.summary, print.error.
class PreviewDialog : public QDialog {
    Q_OBJECT
public:
    enum class Mode { Preview, PdfOnly };
    PreviewDialog(DocumentBuilder build, QPageLayout layout, QString suggestedPdf, Mode mode = Mode::Preview, QWidget* parent = nullptr);
    ~PreviewDialog() override;

    // Adds a widget to the options panel on the left.
    void addOption(QWidget* widget);
    // Re-renders the preview, e.g. after an option changed.
    void refresh();
    void setBuilder(DocumentBuilder build);
    // The page layout in use (Page Setup may change it).
    [[nodiscard]] QPageLayout pageLayout() const;
    void setPageLayout(const QPageLayout& layout);
    [[nodiscard]] int pageCount() const;
    // The finished action (Done) or the last failure; Cancelled when closed.
    [[nodiscard]] const PrintResult& result() const { return result_; }
    // Saves the PDF without a file dialog (used by tests and scripts).
    PrintResult savePdfTo(const QString& path);

signals:
    void pageLayoutChanged(const QPageLayout& layout);

private:
    void finish(const PrintResult& result);
    void updateSummary();

    DocumentBuilder build_;
    QString suggestedPdf_;
    Mode mode_;
    PrintResult result_;
    std::unique_ptr<QPrinter> printer_;
    QPrintPreviewWidget* preview_ = nullptr;
    QVBoxLayout* options_ = nullptr;
    QLabel* summary_ = nullptr;
    QLabel* error_ = nullptr;
    QString renderError_;
};

} // namespace s7studio::printing
