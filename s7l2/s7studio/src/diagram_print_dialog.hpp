// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-S7-plugin-permission
#pragma once

#include "diagram_print.hpp"
#include "printing/preview_dialog.hpp"

#include <memory>
#include <optional>

class QCheckBox;
class QLabel;
class QPushButton;
class QRadioButton;
class QSpinBox;

namespace s7studio {

// File > Print and Export as PDF for the diagram tab: the print preview with
// what to print (whole diagram or selection, nested subsystems), fit or
// tiling, and the paper (the diagram's sheet unless Page Setup chose another).
class DiagramPrintDialog final : public printing::PreviewDialog {
    Q_OBJECT
public:
    DiagramPrintDialog(std::shared_ptr<const diagram::DiagramPrintJob> job, QPageLayout sheet, std::optional<QPageLayout> paper,
                       QString footer, QString suggestedPdf, Mode mode, QWidget* parent = nullptr);

    [[nodiscard]] diagram::DiagramPrintOptions options() const;
    // The Page Setup choice, empty while the diagram's sheet is used.
    [[nodiscard]] const std::optional<QPageLayout>& paper() const { return paper_; }

private:
    void optionsChanged();
    [[nodiscard]] printing::DocumentBuilder builder() const;

    std::shared_ptr<const diagram::DiagramPrintJob> job_;
    QPageLayout sheet_;
    std::optional<QPageLayout> paper_;
    QString footer_;
    bool settingSheet_ = false;
    QRadioButton* wholeDiagram_ = nullptr;
    QRadioButton* selection_ = nullptr;
    QRadioButton* fit_ = nullptr;
    QRadioButton* tile_ = nullptr;
    QSpinBox* tileScale_ = nullptr;
    QCheckBox* subsystems_ = nullptr;
    QLabel* paperLabel_ = nullptr;
    QPushButton* useSheet_ = nullptr;
};

} // namespace s7studio
