// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-S7-plugin-permission
#include "diagram_print_dialog.hpp"

#include <QButtonGroup>
#include <QCheckBox>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QRadioButton>
#include <QSpinBox>
#include <QVBoxLayout>

namespace s7studio {

DiagramPrintDialog::DiagramPrintDialog(std::shared_ptr<const diagram::DiagramPrintJob> job, QPageLayout sheet,
                                       std::optional<QPageLayout> paper, QString footer, QString suggestedPdf, Mode mode, QWidget* parent)
    : printing::PreviewDialog({}, paper.value_or(sheet), std::move(suggestedPdf), mode, parent), job_(std::move(job)), sheet_(std::move(sheet)),
      paper_(std::move(paper)), footer_(std::move(footer))
{
    setWindowTitle(mode == Mode::PdfOnly ? QStringLiteral("Export Diagram as PDF") : QStringLiteral("Print Diagram"));
    const auto group = [this](const QString& title) {
        auto* box = new QGroupBox(title, this);
        auto* column = new QVBoxLayout(box);
        addOption(box);
        return column;
    };

    auto* area = group(QStringLiteral("Print"));
    wholeDiagram_ = new QRadioButton(QStringLiteral("Whole diagram"), this);
    wholeDiagram_->setObjectName(QStringLiteral("print.area.diagram"));
    selection_ = new QRadioButton(QStringLiteral("Selection"), this);
    selection_->setObjectName(QStringLiteral("print.area.selection"));
    selection_->setEnabled(job_->hasSelection());
    if (!job_->hasSelection()) selection_->setToolTip(QStringLiteral("Select elements in the diagram first."));
    auto* areaGroup = new QButtonGroup(this);
    areaGroup->addButton(wholeDiagram_);
    areaGroup->addButton(selection_);
    wholeDiagram_->setChecked(true);
    area->addWidget(wholeDiagram_);
    area->addWidget(selection_);
    subsystems_ = new QCheckBox(QStringLiteral("Include subsystems (%1)").arg(job_->subsystemCount()), this);
    subsystems_->setObjectName(QStringLiteral("print.subsystems"));
    subsystems_->setEnabled(job_->subsystemCount() > 0);
    subsystems_->setToolTip(job_->subsystemError().isEmpty()
        ? QStringLiteral("Append every nested subsystem level, each on its own page(s).")
        : QStringLiteral("Subsystems cannot be printed: %1").arg(job_->subsystemError()));
    area->addWidget(subsystems_);

    auto* scaling = group(QStringLiteral("Scaling"));
    fit_ = new QRadioButton(QStringLiteral("Fit to one page"), this);
    fit_->setObjectName(QStringLiteral("print.fit"));
    tile_ = new QRadioButton(QStringLiteral("Tile pages at"), this);
    tile_->setObjectName(QStringLiteral("print.tile"));
    tileScale_ = new QSpinBox(this);
    tileScale_->setObjectName(QStringLiteral("print.tileScale"));
    tileScale_->setRange(10, 1000);
    tileScale_->setSingleStep(10);
    tileScale_->setValue(100);
    tileScale_->setSuffix(QStringLiteral(" %"));
    tileScale_->setToolTip(QStringLiteral("100 % prints the diagram at its sheet size; neighbouring tiles overlap by 10 mm."));
    auto* scaleGroup = new QButtonGroup(this);
    scaleGroup->addButton(fit_);
    scaleGroup->addButton(tile_);
    fit_->setChecked(true);
    scaling->addWidget(fit_);
    auto* tileRow = new QHBoxLayout();
    tileRow->addWidget(tile_);
    tileRow->addWidget(tileScale_);
    scaling->addLayout(tileRow);

    auto* paperBox = group(QStringLiteral("Paper"));
    paperLabel_ = new QLabel(this);
    paperLabel_->setObjectName(QStringLiteral("print.paper"));
    paperLabel_->setWordWrap(true);
    useSheet_ = new QPushButton(QStringLiteral("Use Diagram Sheet"), this);
    useSheet_->setObjectName(QStringLiteral("print.useSheet"));
    paperBox->addWidget(paperLabel_);
    paperBox->addWidget(useSheet_);

    connect(this, &PreviewDialog::pageLayoutChanged, this, [this](const QPageLayout& layout) {
        if (!settingSheet_) paper_ = layout;
        optionsChanged();
    });
    connect(useSheet_, &QPushButton::clicked, this, [this] {
        paper_.reset();
        settingSheet_ = true;
        setPageLayout(sheet_);
        settingSheet_ = false;
    });
    for (auto* button : {wholeDiagram_, selection_, fit_, tile_}) connect(button, &QRadioButton::toggled, this, &DiagramPrintDialog::optionsChanged);
    connect(tileScale_, qOverload<int>(&QSpinBox::valueChanged), this, &DiagramPrintDialog::optionsChanged);
    connect(subsystems_, &QCheckBox::toggled, this, &DiagramPrintDialog::optionsChanged);
    optionsChanged();
}

diagram::DiagramPrintOptions DiagramPrintDialog::options() const
{
    diagram::DiagramPrintOptions options;
    options.area = selection_->isChecked() ? diagram::DiagramPrintOptions::Area::Selection : diagram::DiagramPrintOptions::Area::Diagram;
    options.tile = tile_->isChecked();
    options.tileScale = tileScale_->value() / 100.0;
    options.includeSubsystems = subsystems_->isChecked();
    return options;
}

printing::DocumentBuilder DiagramPrintDialog::builder() const
{
    return [job = job_, options = options(), footer = footer_](const QPageLayout& layout) { return job->build(layout, options, footer); };
}

void DiagramPrintDialog::optionsChanged()
{
    if (!tileScale_ || !paperLabel_) return;  // still constructing
    tileScale_->setEnabled(tile_->isChecked());
    paperLabel_->setText(paper_ ? QStringLiteral("%1 (Page Setup)").arg(printing::describe(*paper_))
                                : QStringLiteral("%1 (diagram sheet)").arg(printing::describe(sheet_)));
    useSheet_->setEnabled(paper_.has_value());
    setBuilder(builder());
}

} // namespace s7studio
