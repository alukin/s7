// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-S7-plugin-permission

#include "phase_portrait_view_window.hpp"

#include "viewers/line_plot_view.hpp"

#include <QCheckBox>
#include <QColor>
#include <QSaveFile>
#include <QFile>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QStringList>
#include <QTextStream>
#include <QTimer>
#include <QVBoxLayout>
#include <QtGraphs/QLineSeries>

#include <utility>

namespace s7studio::ui {

namespace {
const QColor kTrajectoryColor("#2ca02c");
} // namespace

auto PhasePortraitViewWindow::viewerType() -> viewers::ViewerType
{
    return {QStringLiteral("PhasePortrait"), {}, QStringLiteral("2→plot"), {}, {QStringLiteral("x"), QStringLiteral("y")},
            [](const viewers::ViewerInstanceSpec& spec) -> viewers::ObservationViewer* {
                if (spec.probeNames.size() != 2 || spec.probeNames[0].isEmpty() || spec.probeNames[1].isEmpty()
                    || spec.probeNames[0] == spec.probeNames[1]) return nullptr;
                return new PhasePortraitViewWindow(spec.probeNames[0], spec.probeNames[1], nullptr,
                    Qt::Window | Qt::WindowTitleHint | Qt::WindowSystemMenuHint |
                        Qt::WindowMinMaxButtonsHint | Qt::WindowCloseButtonHint);
            }};
}

PhasePortraitViewWindow::PhasePortraitViewWindow(QString xProbeName,
                                                 QString yProbeName,
                                                 QWidget* parent,
                                                 const Qt::WindowFlags flags)
    : QWidget(parent, flags)
    , xProbeName_(std::move(xProbeName))
    , yProbeName_(std::move(yProbeName))
{
    setWindowFlag(Qt::Window, true);
    setWindowFlag(Qt::WindowMinimizeButtonHint, true);
    setWindowFlag(Qt::WindowMaximizeButtonHint, true);
    setWindowFlag(Qt::WindowCloseButtonHint, true);
    setAttribute(Qt::WA_QuitOnClose, false);
    setMinimumSize(480, 480);
    setWindowTitle(QStringLiteral("Phase Portrait - %1 vs %2").arg(yProbeName_, xProbeName_));
    resize(640, 640);

    auto* rootLayout = new QVBoxLayout(this);
    auto* controls = new QHBoxLayout();

    autoScaleCheck_ = new QCheckBox(QStringLiteral("Auto Scale"), this);
    autoScaleCheck_->setChecked(true);
    controls->addWidget(autoScaleCheck_);

    clearButton_ = new QPushButton(QStringLiteral("Clear display"), this);
    clearButton_->setToolTip(QStringLiteral("Clear this presentation. Recorded source data remains available for export and reopening."));
    exportButton_ = new QPushButton(QStringLiteral("Export CSV"), this);
    controls->addWidget(clearButton_);
    controls->addWidget(exportButton_);
    controls->addStretch(1);
    statusLabel_ = new QLabel(this);
    statusLabel_->setObjectName(QStringLiteral("viewer.status"));
    statusLabel_->setTextFormat(Qt::PlainText);
    statusLabel_->setWordWrap(true);


    rootLayout->addLayout(controls);
    rootLayout->addWidget(statusLabel_);

    plot_ = new viewers::LinePlotView(this);
    plot_->setAxisTitles(xProbeName_ + " (unit unspecified)", yProbeName_ + " (unit unspecified)");
    plot_->setToolTip(QStringLiteral("Zoom with the mouse wheel and pan by dragging. Keyboard: +/− zoom, arrows pan, Home fits all data."));
    curve_ = plot_->addSeries(QStringLiteral("%1 vs %2").arg(yProbeName_, xProbeName_), {kTrajectoryColor, 1.4, false});
    rootLayout->addWidget(plot_, 1);

    repaintTimer_ = new QTimer(this);
    repaintTimer_->setInterval(33);
    connect(repaintTimer_, &QTimer::timeout, this, &PhasePortraitViewWindow::refreshPlot);
    repaintTimer_->start();

    connect(clearButton_, &QPushButton::clicked, this, &PhasePortraitViewWindow::clearBuffers);
    connect(exportButton_, &QPushButton::clicked, this, &PhasePortraitViewWindow::exportRecordedCsv);
}

void PhasePortraitViewWindow::appendObservedSample(const viewers::ObservedSample& sample)
{
    if (sample.role == "x") xUnit_ = sample.unit;
    if (sample.role == "y") yUnit_ = sample.unit;
    plot_->setAxisTitles(xProbeName_ + (xUnit_.isEmpty() ? " (unit unspecified)" : " (" + xUnit_ + ")"),
                         yProbeName_ + (yUnit_.isEmpty() ? " (unit unspecified)" : " (" + yUnit_ + ")"));
    if (sample.role == QStringLiteral("x")) appendXSample(sample.step, sample.value);
    if (sample.role == QStringLiteral("y")) appendYSample(sample.step, sample.value);
}

void PhasePortraitViewWindow::appendXSample(const std::int64_t step, const double value)
{
    addCoordinate(step, 0, value);
}

void PhasePortraitViewWindow::appendYSample(const std::int64_t step, const double value)
{
    addCoordinate(step, 1, value);
}

void PhasePortraitViewWindow::addCoordinate(const std::int64_t step, const std::size_t axis, const double value)
{
    const auto discarded = aligner_.discardedSteps();
    if (const auto point = aligner_.add(step, axis, value)) {
        if (points_.size() >= kMaxPoints) {
            points_.pop_front();
        }
        points_.push_back({(*point)[0], (*point)[1]});
        dirty_ = true;
    }
    if (aligner_.discardedSteps() != discarded) updateStatus();
}

void PhasePortraitViewWindow::clearSamples()
{
    clearBuffers();
}

void PhasePortraitViewWindow::setDroppedSampleCount(const std::uint64_t dropped)
{
    droppedSamples_ = dropped;
    updateStatus();
}

void PhasePortraitViewWindow::updateStatus()
{
    QStringList parts;
    if (points_.size() >= kMaxPoints) {
        parts << QStringLiteral("trajectory trimmed to %1 points").arg(kMaxPoints);
    }
    if (points_.size() > kMaxDrawnPoints) {
        parts << QStringLiteral("drawing every %1th point").arg((points_.size() + kMaxDrawnPoints - 1) / kMaxDrawnPoints);
    }
    if (const auto totalDropped = droppedSamples_ + aligner_.discardedSteps(); totalDropped > 0) {
        parts << QStringLiteral("display dropped %1 samples").arg(totalDropped);
    }
    if (!sourceStatusText().isEmpty()) parts << sourceStatusText();
    statusLabel_->setText(parts.join(QStringLiteral(" | ")));
}

void PhasePortraitViewWindow::clearBuffers()
{
    xUnit_.clear(); yUnit_.clear();
    plot_->setAxisTitles(xProbeName_ + " (unit unspecified)", yProbeName_ + " (unit unspecified)");
    discardPresentationHistory();
    points_.clear();
    aligner_.clear();
    droppedSamples_ = 0;
    updateStatus();
    viewers::LinePlotView::setPoints(curve_, {});
    dirty_ = true;
}

void PhasePortraitViewWindow::exportRecordedCsv()
{
    const QString filePath = QFileDialog::getSaveFileName(
        this,
        QStringLiteral("Export phase portrait data"),
        QStringLiteral("%1_vs_%2.csv").arg(yProbeName_, xProbeName_),
        QStringLiteral("CSV files (*.csv);;All files (*)"));
    if (filePath.isEmpty()) {
        return;
    }

    QSaveFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::critical(this, QStringLiteral("Export failed"), file.errorString());
        return;
    }

    if (source()) {
        QString error;
        if (!source()->writeAlignedCsv(file, selectedChannels({QStringLiteral("x"), QStringLiteral("y")}), &error)) {
            QMessageBox::critical(this, QStringLiteral("Export failed"), error); return;
        }
    } else {
    QTextStream out(&file);
    out.setRealNumberPrecision(17);
    out << xProbeName_ << ',' << yProbeName_ << '\n';
    for (const auto& point : std::as_const(points_)) {
        out << point.x() << ',' << point.y() << '\n';
    }    out.flush();
    if (out.status() != QTextStream::Ok) {
        QMessageBox::critical(this, QStringLiteral("Export failed"), QStringLiteral("CSV write failed.")); return;
    }
    }
    if (!file.commit()) QMessageBox::critical(this, QStringLiteral("Export failed"), file.errorString());

}

void PhasePortraitViewWindow::refreshPlot()
{
    // Hidden diagram observers keep collecting pairs without rebuilding a
    // potentially large curve until the viewer is opened.
    if (!dirty_ || !isVisible()) {
        return;
    }

    dirty_ = false;
    if (points_.size() <= kMaxDrawnPoints) {
        viewers::LinePlotView::setPoints(curve_, points_);
    } else {
        const qsizetype stride = (points_.size() + kMaxDrawnPoints - 1) / kMaxDrawnPoints;
        QList<QPointF> drawn;
        drawn.reserve(points_.size() / stride + 2);
        for (qsizetype index = 0; index < points_.size(); index += stride) drawn.push_back(points_[index]);
        if ((points_.size() - 1) % stride != 0) drawn.push_back(points_.back());
        viewers::LinePlotView::setPoints(curve_, drawn);
    }

    if (autoScaleCheck_->isChecked()) {
        static_cast<void>(plot_->fitAll());
    }
    updateStatus();
}

} // namespace s7studio::ui
