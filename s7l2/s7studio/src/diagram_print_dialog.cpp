// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-S7-plugin-permission
#include "diagram_print_dialog.hpp"

#include <QButtonGroup>
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QRadioButton>
#include <QSpinBox>
#include <QVBoxLayout>

namespace s7studio {

DiagramPrintDialog::DiagramPrintDialog(std::shared_ptr<const diagram::DiagramPrintJob> job, QPageLayout sheet,
                                       std::optional<QPageLayout> paper, QString footer, QString suggestedPdf, Action preferred,
                                       QWidget* parent)
    : QDialog(parent), job_(std::move(job)), sheet_(std::move(sheet)), paper_(std::move(paper)), footer_(std::move(footer)),
      suggestedPdf_(std::move(suggestedPdf))
{
    setObjectName(QStringLiteral("print.diagramDialog"));
    setWindowTitle(QStringLiteral("Print Diagram"));
    auto* layout = new QVBoxLayout(this);
    auto* form = new QFormLayout();
    layout->addLayout(form);

    auto* area = new QHBoxLayout();
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
    area->addStretch();
    form->addRow(QStringLiteral("Print"), area);

    auto* scaling = new QHBoxLayout();
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
    scaling->addWidget(tile_);
    scaling->addWidget(tileScale_);
    scaling->addStretch();
    form->addRow(QStringLiteral("Scaling"), scaling);

    subsystems_ = new QCheckBox(QStringLiteral("Include subsystems (%1)").arg(job_->subsystemCount()), this);
    subsystems_->setObjectName(QStringLiteral("print.subsystems"));
    subsystems_->setEnabled(job_->subsystemCount() > 0);
    subsystems_->setToolTip(job_->subsystemError().isEmpty()
        ? QStringLiteral("Append every nested subsystem level, each on its own page(s).")
        : QStringLiteral("Subsystems cannot be printed: %1").arg(job_->subsystemError()));
    form->addRow(QString(), subsystems_);

    auto* paperRow = new QHBoxLayout();
    paperLabel_ = new QLabel(this);
    paperLabel_->setObjectName(QStringLiteral("print.paper"));
    auto* pageSetup = new QPushButton(QStringLiteral("Page Setup…"), this);
    pageSetup->setObjectName(QStringLiteral("print.pageSetup"));
    useSheet_ = new QPushButton(QStringLiteral("Use Diagram Sheet"), this);
    useSheet_->setObjectName(QStringLiteral("print.useSheet"));
    paperRow->addWidget(paperLabel_, 1);
    paperRow->addWidget(pageSetup);
    paperRow->addWidget(useSheet_);
    form->addRow(QStringLiteral("Paper"), paperRow);

    summary_ = new QLabel(this);
    summary_->setObjectName(QStringLiteral("print.summary"));
    layout->addWidget(summary_);
    error_ = new QLabel(this);
    error_->setObjectName(QStringLiteral("print.error"));
    error_->setWordWrap(true);
    error_->setStyleSheet(QStringLiteral("color: #b00020;"));
    error_->hide();
    layout->addWidget(error_);

    auto* buttons = new QDialogButtonBox(this);
    auto* previewButton = buttons->addButton(QStringLiteral("Preview…"), QDialogButtonBox::ActionRole);
    previewButton->setObjectName(QStringLiteral("print.preview"));
    auto* pdfButton = buttons->addButton(QStringLiteral("Save as PDF…"), QDialogButtonBox::ActionRole);
    pdfButton->setObjectName(QStringLiteral("print.pdf"));
    auto* printButton = buttons->addButton(QStringLiteral("Print…"), QDialogButtonBox::ActionRole);
    printButton->setObjectName(QStringLiteral("print.print"));
    buttons->addButton(QDialogButtonBox::Close);
    layout->addWidget(buttons);
    (preferred == Action::Pdf ? pdfButton : preferred == Action::Preview ? previewButton : printButton)->setDefault(true);

    connect(previewButton, &QPushButton::clicked, this, [this] { run(Action::Preview); });
    connect(pdfButton, &QPushButton::clicked, this, [this] { run(Action::Pdf); });
    connect(printButton, &QPushButton::clicked, this, [this] { run(Action::Print); });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(pageSetup, &QPushButton::clicked, this, [this] {
        if (const auto chosen = printing::pageSetup(this, pageLayout())) {
            paper_ = *chosen;
            refresh();
        }
    });
    connect(useSheet_, &QPushButton::clicked, this, [this] {
        paper_.reset();
        refresh();
    });
    for (auto* button : {wholeDiagram_, selection_, fit_, tile_}) connect(button, &QRadioButton::toggled, this, &DiagramPrintDialog::refresh);
    connect(tileScale_, qOverload<int>(&QSpinBox::valueChanged), this, &DiagramPrintDialog::refresh);
    connect(subsystems_, &QCheckBox::toggled, this, &DiagramPrintDialog::refresh);
    refresh();
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

int DiagramPrintDialog::pageCount() const
{
    return static_cast<int>(job_->build(pageLayout(), options(), footer_).pages.size());
}

void DiagramPrintDialog::refresh()
{
    tileScale_->setEnabled(tile_->isChecked());
    paperLabel_->setText(paper_ ? QStringLiteral("%1 (Page Setup)").arg(printing::describe(*paper_))
                                : QStringLiteral("%1 (diagram sheet)").arg(printing::describe(sheet_)));
    useSheet_->setEnabled(paper_.has_value());
    const int pages = pageCount();
    summary_->setText(pages == 1 ? QStringLiteral("1 page") : QStringLiteral("%1 pages").arg(pages));
}

void DiagramPrintDialog::run(Action action)
{
    error_->hide();
    printing::PrintResult result;
    switch (action) {
    case Action::Preview: result = printing::preview(this, builder(), pageLayout()); break;
    case Action::Pdf: result = printing::exportPdf(this, builder(), pageLayout(), suggestedPdf_); break;
    case Action::Print: result = printing::printWithDialog(this, builder(), pageLayout()); break;
    }
    if (result.outcome == printing::PrintResult::Outcome::Cancelled) return;
    result_ = result;
    if (result.outcome == printing::PrintResult::Outcome::Failed) {
        error_->setText(result.message);
        error_->show();
        return;
    }
    accept();
}

} // namespace s7studio
