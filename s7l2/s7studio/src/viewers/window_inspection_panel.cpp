// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-S7-plugin-permission
#include "viewers/window_inspection_panel.hpp"
#include "viewers/line_plot_view.hpp"
#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QQuickItem>
#include <QQuickWidget>
#include <QShortcut>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTimer>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>

namespace s7studio::viewers {
using namespace analysis;
WindowInspectionPanel::WindowInspectionPanel(Capture capture, QWidget* parent) : QWidget(parent), capture_(std::move(capture))
{
    setObjectName("live.inspection");
    auto* layout = new QVBoxLayout(this); layout->setContentsMargins(0, 0, 0, 0);
    auto* cursors = new QHBoxLayout;
    auto* pinButton = new QPushButton("Pin window / A–B", this); pinButton->setObjectName("inspection.pin"); cursors->addWidget(pinButton);
    active_ = new QComboBox(this); active_->setObjectName("inspection.active"); active_->addItems({"Click sets A", "Click sets B"}); cursors->addWidget(active_);
    auto index = [&](const char* name, const QString& label) {
        cursors->addWidget(new QLabel(label, this)); auto* spin = new QSpinBox(this); spin->setObjectName(name);
        spin->setRange(0, 0); spin->setMaximumWidth(110); spin->setEnabled(false); cursors->addWidget(spin); return spin;
    };
    a_ = index("inspection.a", "A index"); b_ = index("inspection.b", "B index");
    auto* measure = new QPushButton("Measure A–B", this); measure->setObjectName("inspection.measure"); cursors->addWidget(measure);
    auto* copy = new QPushButton("Copy readout", this); copy->setObjectName("inspection.copy"); cursors->addWidget(copy); cursors->addStretch();
    layout->addLayout(cursors);
    readout_ = new QPlainTextEdit(this); readout_->setObjectName("inspection.readout"); readout_->setReadOnly(true);
    readout_->setMaximumHeight(125); readout_->hide(); layout->addWidget(readout_);
    auto* exports = new QHBoxLayout;
    format_ = new QComboBox(this); format_->setObjectName("inspection.format"); format_->addItems({"Window JSON (.s7window)", "Raw acquisition CSV", "Displayed data CSV"}); exports->addWidget(format_);
    export_ = new QPushButton("Export frozen window…", this); export_->setObjectName("inspection.export"); exports->addWidget(export_);
    cancel_ = new QPushButton("Cancel export", this); cancel_->setObjectName("inspection.cancel"); cancel_->setEnabled(false); exports->addWidget(cancel_);
    status_ = new QLabel("Click a plot or pin a window for exact A/B cursors. Alt+Left/Right moves the selected cursor one position.", this);
    status_->setObjectName("inspection.status"); status_->setWordWrap(true); status_->setTextFormat(Qt::PlainText); exports->addWidget(status_, 1); layout->addLayout(exports);
    connect(pinButton, &QPushButton::clicked, this, [this] { pin(); });
    connect(a_, &QSpinBox::valueChanged, this, [this] { updateReadout(); });
    connect(b_, &QSpinBox::valueChanged, this, [this] { updateReadout(); });
    connect(measure, &QPushButton::clicked, this, [this] { if (pin()) { measured_ = true; updateReadout(); } });
    connect(copy, &QPushButton::clicked, this, [this] { if (pin()) QApplication::clipboard()->setText(readout()); });
    for (const auto& [key, delta] : {std::pair{Qt::Key_Left, -1}, std::pair{Qt::Key_Right, 1}}) {
        auto* shortcut = new QShortcut(QKeySequence(Qt::ALT | key), parent);
        shortcut->setContext(Qt::WidgetWithChildrenShortcut);
        connect(shortcut, &QShortcut::activated, this, [this, delta] { if (pin()) { auto* spin = active_->currentIndex() == 0 ? a_ : b_; spin->setValue(spin->value()+delta); } });
    }
    connect(export_, &QPushButton::clicked, this, [this] {
        if (!pin()) return;
        const auto format = static_cast<WindowExportFormat>(format_->currentIndex());
        const bool native = format == WindowExportFormat::WindowJson;
        const auto path = QFileDialog::getSaveFileName(this, "Export bounded frozen window", native ? "window.s7window" : "window.csv",
            native ? "S7 window JSON (*.s7window);;All files (*)" : "CSV (*.csv);;All files (*)");
        if (!path.isEmpty()) exportTo(path, format);
    });
    connect(cancel_, &QPushButton::clicked, this, &WindowInspectionPanel::cancelExport);
    timer_ = new QTimer(this); timer_->setInterval(50);
    connect(timer_, &QTimer::timeout, this, [this] {
        if (auto result = exporter_.takeResult()) {
            timer_->stop(); export_->setEnabled(true); cancel_->setEnabled(false);
            status_->setText(result->success ? "Export complete: bounded frozen window saved." : result->error);
            emit exportFinished(result->success, result->cancelled, result->error);
        } else status_->setText(QStringLiteral("Exporting pinned window… %1%").arg(exporter_.progress()));
    });
}
void WindowInspectionPanel::setPlot(LinePlotView* plot)
{
    clear(); plot_ = plot;
}
void WindowInspectionPanel::clear(bool cancelExport)
{
    if (cancelExport) exporter_.cancel();
    snapshot_.reset(); measured_ = false; a_->setEnabled(false); b_->setEnabled(false); readout_->clear(); readout_->hide();
    if (plot_) { plot_->bridge()->selectionMarkers.clear(); emit plot_->bridge()->selectionChanged(); }
    if (!exporter_.busy()) status_->setText("Click a plot or pin a window for exact A/B cursors. Alt+Left/Right moves the selected cursor one position.");
}
bool WindowInspectionPanel::pin()
{
    if (snapshot_) return true;
    try {
        auto value = capture_();
        if (!value || value->axis.empty()) { status_->setText("No completed displayed data to pin. Wait for acquisition/analysis or reduce N."); return false; }
        snapshot_ = std::make_shared<const WindowSnapshot>(std::move(*value));
        const QSignalBlocker blockA(a_), blockB(b_);
        const auto maximum = static_cast<int>(snapshot_->axis.size()-1);
        a_->setRange(0, maximum); b_->setRange(0, maximum); a_->setValue(0); b_->setValue(maximum);
        a_->setEnabled(true); b_->setEnabled(true); emit pinned(); updateReadout();
        status_->setText("Pinned displayed input. Click to place A/B; edit indices or use Alt+Left/Right. Resume live releases selection.");
        return true;
    } catch (const std::exception& ex) { status_->setText(QString::fromUtf8(ex.what())); return false; }
}
void WindowInspectionPanel::pick(double x, double y)
{
    if (!pin() || !plot_) return;
    const auto& d = *snapshot_; std::optional<std::size_t> chosen;
    if (d.kind == WindowSnapshot::Kind::XY) {
        QRectF area(0, 0, std::max(1, plot_->width()), std::max(1, plot_->height()));
        if (plot_->quickWidget() && plot_->quickWidget()->rootObject())
            if (auto* graph = plot_->quickWidget()->rootObject()->findChild<QObject*>("viewer.graphs")) area = graph->property("plotArea").toRectF();
        const double sx = area.width()/(plot_->xMax()-plot_->xMin()), sy = area.height()/(plot_->yMax()-plot_->yMin());
        double best = 144;
        for (std::size_t i = 0; i < d.displayX.size(); ++i) {
            if (d.displayX[i] < plot_->xMin() || d.displayX[i] > plot_->xMax() || d.displayY[i] < plot_->yMin() || d.displayY[i] > plot_->yMax()) continue;
            const double dx = (d.displayX[i]-x)*sx, dy = (d.displayY[i]-y)*sy, distance = dx*dx+dy*dy;
            if (distance < best || (distance == best && chosen && d.steps[i] > d.steps[*chosen])) { chosen = i; best = distance; }
        }
    } else if (x >= d.displayX.front() && x <= d.displayX.back()) {
        auto i = static_cast<std::size_t>(std::lower_bound(d.displayX.begin(), d.displayX.end(), x)-d.displayX.begin());
        if (i && x-d.displayX[i-1] <= d.displayX[i]-x) --i;
        chosen = i;
    }
    if (!chosen) { status_->setText("No underlying position here; XY requires a raw pair within 12 pixels (including in density mode)."); return; }
    (active_->currentIndex() == 0 ? a_ : b_)->setValue(static_cast<int>(*chosen)); updateReadout();
}
void WindowInspectionPanel::updateReadout()
{
    if (!snapshot_) return;
    try {
        const auto a = static_cast<std::size_t>(a_->value()), b = static_cast<std::size_t>(b_->value());
        QString text = cursorReadout(*snapshot_, a, b);
        if (measured_) text = measurementReadout(*snapshot_, a, b)+"\n\n"+text;
        readout_->setPlainText(text); readout_->show();
        if (plot_) {
            QVariantList markers;
            for (const auto& [label, i] : {std::pair{QStringLiteral("A"), a}, std::pair{QStringLiteral("B"), b}})
                markers.push_back(QVariantMap{{"label", label}, {"x", snapshot_->displayX[i]},
                    {"y", snapshot_->kind == WindowSnapshot::Kind::XY ? snapshot_->displayY[i] : 0.}, {"vertical", snapshot_->kind != WindowSnapshot::Kind::XY}});
            plot_->bridge()->selectionMarkers = markers; emit plot_->bridge()->selectionChanged();
        }
    } catch (const std::exception& ex) { status_->setText(QString::fromUtf8(ex.what())); }
}
QString WindowInspectionPanel::readout() const { return readout_->toPlainText(); }
bool WindowInspectionPanel::exportTo(const QString& path, WindowExportFormat format)
{
    if (!pin() || !exporter_.start(path, snapshot_, static_cast<std::size_t>(a_->value()), static_cast<std::size_t>(b_->value()), format)) return false;
    export_->setEnabled(false); cancel_->setEnabled(true); timer_->start(); status_->setText("Exporting immutable bounded window…"); return true;
}
void WindowInspectionPanel::cancelExport() { exporter_.cancel(); }
}
