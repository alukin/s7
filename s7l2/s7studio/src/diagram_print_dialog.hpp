// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-S7-plugin-permission
#pragma once

#include "diagram_print.hpp"
#include "printing/print_actions.hpp"

#include <QDialog>

#include <memory>

class QCheckBox;
class QLabel;
class QPushButton;
class QRadioButton;
class QSpinBox;

namespace s7studio {

// File > Print / Print Preview / Export as PDF for the diagram tab: what to
// print (whole diagram or selection, nested subsystems), fit or tiling, and
// the paper (the diagram's sheet unless Page Setup chose another).
class DiagramPrintDialog final : public QDialog {
    Q_OBJECT
public:
    enum class Action { Print, Preview, Pdf };

    DiagramPrintDialog(std::shared_ptr<const diagram::DiagramPrintJob> job, QPageLayout sheet, std::optional<QPageLayout> paper,
                       QString footer, QString suggestedPdf, Action preferred, QWidget* parent = nullptr);

    [[nodiscard]] diagram::DiagramPrintOptions options() const;
    // The paper used: the Page Setup choice, else the diagram's sheet.
    [[nodiscard]] QPageLayout pageLayout() const { return paper_.value_or(sheet_); }
    // The Page Setup choice, empty while the diagram's sheet is used.
    [[nodiscard]] const std::optional<QPageLayout>& paper() const { return paper_; }
    [[nodiscard]] printing::DocumentBuilder builder() const;
    [[nodiscard]] int pageCount() const;
    // The last finished action (Done or Failed); Cancelled when none finished.
    [[nodiscard]] const printing::PrintResult& result() const { return result_; }

private:
    void refresh();
    void run(Action action);

    std::shared_ptr<const diagram::DiagramPrintJob> job_;
    QPageLayout sheet_;
    std::optional<QPageLayout> paper_;
    QString footer_;
    QString suggestedPdf_;
    printing::PrintResult result_;
    QRadioButton* wholeDiagram_ = nullptr;
    QRadioButton* selection_ = nullptr;
    QRadioButton* fit_ = nullptr;
    QRadioButton* tile_ = nullptr;
    QSpinBox* tileScale_ = nullptr;
    QCheckBox* subsystems_ = nullptr;
    QLabel* paperLabel_ = nullptr;
    QPushButton* useSheet_ = nullptr;
    QLabel* summary_ = nullptr;
    QLabel* error_ = nullptr;
};

} // namespace s7studio
