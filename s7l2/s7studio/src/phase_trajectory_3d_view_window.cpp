// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-S7-plugin-permission

#include "phase_trajectory_3d_view_window.hpp"
#include "instrument_controls.hpp"

#include "viewers/quick_support.hpp"

#include <QCheckBox>
#include <QSaveFile>
#include <QFile>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QQuickWidget>
#include <QStringList>
#include <QTextStream>
#include <QTimer>
#include <QVBoxLayout>
#include <QVariantMap>

#include <algorithm>
#include <cmath>
#include <utility>

namespace s7studio::ui {

auto PhaseTrajectory3DViewWindow::viewerType() -> viewers::ViewerType
{
    return {QStringLiteral("PhaseTrajectory3D"), {}, QStringLiteral("3→plot"), {},
            {QStringLiteral("x"), QStringLiteral("y"), QStringLiteral("z")},
            [](const viewers::ViewerInstanceSpec& spec) -> viewers::ObservationViewer* {
                const auto& probes = spec.probeNames;
                if (probes.size() != 3 || probes[0].isEmpty() || probes[1].isEmpty() || probes[2].isEmpty()
                    || probes[0] == probes[1] || probes[0] == probes[2] || probes[1] == probes[2]) return nullptr;
                return new PhaseTrajectory3DViewWindow(probes[0], probes[1], probes[2], nullptr,
                    Qt::Window | Qt::WindowTitleHint | Qt::WindowSystemMenuHint |
                        Qt::WindowMinMaxButtonsHint | Qt::WindowCloseButtonHint);
            }};
}

PhaseTrajectory3DViewWindow::PhaseTrajectory3DViewWindow(QString xProbeName,
                                                         QString yProbeName,
                                                         QString zProbeName,
                                                         QWidget* parent,
                                                         const Qt::WindowFlags flags)
    : QWidget(parent, flags)
    , xProbeName_(std::move(xProbeName))
    , yProbeName_(std::move(yProbeName))
    , zProbeName_(std::move(zProbeName))
{
    statusRefresh_ = new Throttle([this] { updateStatus(); }, this);
    setWindowFlag(Qt::Window, true);
    setWindowFlag(Qt::WindowMinimizeButtonHint, true);
    setWindowFlag(Qt::WindowMaximizeButtonHint, true);
    setWindowFlag(Qt::WindowCloseButtonHint, true);
    setAttribute(Qt::WA_QuitOnClose, false);
    setMinimumSize(640, 560);
    setWindowTitle(QStringLiteral("3D Phase Trajectory - %1, %2, %3")
                       .arg(xProbeName_, yProbeName_, zProbeName_));
    resize(900, 760);

    presentation_ = new TrajectoryPlotView(this);
    bridge_ = presentation_->bridge();

    auto* root = new QVBoxLayout(this);
    auto* controls = new QHBoxLayout();
    autoScaleCheck_ = new QCheckBox(QStringLiteral("Auto Scale"), this);
    autoScaleCheck_->setChecked(true);
    controls->addWidget(autoScaleCheck_);
    clearButton_ = new QPushButton(QStringLiteral("Clear display"), this);
    clearButton_->setToolTip(QStringLiteral("Clear this presentation. Recorded source data remains available for export and reopening."));
    exportButton_ = new QPushButton(QStringLiteral("Export CSV"), this);
    controls->addWidget(clearButton_);
    controls->addWidget(exportButton_);
    controls->addSpacing(12);
    const std::pair<const char*, ViewPreset> presets[]{{"3D", ViewPreset::Perspective}, {"XY", ViewPreset::XY},
                                                       {"XZ", ViewPreset::XZ}, {"YZ", ViewPreset::YZ}};
    for (const auto& [text, preset] : presets) {
        auto* button = new QPushButton(QString::fromLatin1(text), this);
        button->setToolTip(preset == ViewPreset::Perspective
            ? QStringLiteral("Reset to the default 3D view")
            : QStringLiteral("View the %1 projection").arg(QString::fromLatin1(text)));
        connect(button, &QPushButton::clicked, this, [this, preset = preset] { setViewPreset(preset); });
        controls->addWidget(button);
    }
    controls->addStretch(1);
    statusLabel_ = new QLabel(this);
    statusLabel_->setObjectName(QStringLiteral("viewer.status"));
    statusLabel_->setTextFormat(Qt::PlainText);
    statusLabel_->setWordWrap(true);

    root->addLayout(controls);
    root->addWidget(statusLabel_);
    root->addWidget(presentation_, 1);

    setViewPreset(ViewPreset::Perspective);
    updateLabels();

    repaintTimer_ = new QTimer(this);
    repaintTimer_->setInterval(33);
    connect(repaintTimer_, &QTimer::timeout, this, &PhaseTrajectory3DViewWindow::refreshPlots);
    repaintTimer_->start();
    connect(clearButton_, &QPushButton::clicked, this, &PhaseTrajectory3DViewWindow::clearBuffers);
    connect(exportButton_, &QPushButton::clicked, this, &PhaseTrajectory3DViewWindow::exportCsv);
    connect(autoScaleCheck_, &QCheckBox::toggled, this, [this] { dirty_ = true; });
}

PhaseTrajectory3DViewWindow::~PhaseTrajectory3DViewWindow() = default;

void PhaseTrajectory3DViewWindow::appendObservedSample(const viewers::ObservedSample& sample)
{
    if (sample.role == "x") xUnit_ = sample.unit;
    if (sample.role == "y") yUnit_ = sample.unit;
    if (sample.role == "z") zUnit_ = sample.unit;
    if (sample.role == QStringLiteral("x")) appendXSample(sample.step, sample.value);
    if (sample.role == QStringLiteral("y")) appendYSample(sample.step, sample.value);
    if (sample.role == QStringLiteral("z")) appendZSample(sample.step, sample.value);
}

void PhaseTrajectory3DViewWindow::appendXSample(const std::int64_t step, const double value)
{
    addCoordinate(step, value, 0);
}

void PhaseTrajectory3DViewWindow::appendYSample(const std::int64_t step, const double value)
{
    addCoordinate(step, value, 1);
}

void PhaseTrajectory3DViewWindow::appendZSample(const std::int64_t step, const double value)
{
    addCoordinate(step, value, 2);
}

void PhaseTrajectory3DViewWindow::addCoordinate(const std::int64_t step, const double value, const std::size_t axis)
{
    if (const auto point = aligner_.add(step, axis, value)) {
        if (points_.size() >= kMaxPoints) {
            points_.pop_front();
        }
        points_.push_back(*point);
        dirty_ = true;
    }
}

void PhaseTrajectory3DViewWindow::clearSamples()
{
    clearBuffers();
}

void PhaseTrajectory3DViewWindow::setDroppedSampleCount(const std::uint64_t dropped)
{
    // Called on every data refresh: loss shows at once, other status at 10 Hz.
    if (dropped != droppedSamples_) { droppedSamples_ = dropped; updateStatus(); }
    else statusRefresh_->request();
}

void PhaseTrajectory3DViewWindow::updateStatus()
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
    QStringList missingUnits;
    if (xUnit_.isEmpty()) missingUnits << QStringLiteral("X");
    if (yUnit_.isEmpty()) missingUnits << QStringLiteral("Y");
    if (zUnit_.isEmpty()) missingUnits << QStringLiteral("Z");
    if (!missingUnits.isEmpty()) parts << QStringLiteral("Units unspecified: %1").arg(missingUnits.join(QStringLiteral(", ")));
    statusLabel_->setText(parts.join(QStringLiteral(" | ")));
}

void PhaseTrajectory3DViewWindow::updateLabels()
{
    presentation_->setAxisLabels(bounds_, {xProbeName_ + (xUnit_.isEmpty() ? "" : " (" + xUnit_ + ")"),
        yProbeName_ + (yUnit_.isEmpty() ? "" : " (" + yUnit_ + ")"), zProbeName_ + (zUnit_.isEmpty() ? "" : " (" + zUnit_ + ")")});
}

void PhaseTrajectory3DViewWindow::setViewPreset(const ViewPreset preset)
{
    // Euler angles of the camera orbit. Scene Y is data Z (up) and scene -Z
    // is data Y, so the front view is XZ.
    switch (preset) {
    case ViewPreset::Perspective: bridge_->setViewRotation({-25.0f, 35.0f, 0.0f}); break;
    case ViewPreset::XY: bridge_->setViewRotation({-90.0f, 0.0f, 0.0f}); break;
    case ViewPreset::XZ: bridge_->setViewRotation({0.0f, 0.0f, 0.0f}); break;
    case ViewPreset::YZ: bridge_->setViewRotation({0.0f, 90.0f, 0.0f}); break;
    }
}

void PhaseTrajectory3DViewWindow::clearBuffers()
{
    xUnit_.clear(); yUnit_.clear(); zUnit_.clear();
    discardPresentationHistory();
    points_.clear();
    aligner_.clear();
    droppedSamples_ = 0;
    updateStatus();
    bridge_->trajectoryGeometry().setTrajectory({}, bounds_, kMaxDrawnPoints);
    bridge_->setHead(false, {});
    dirty_ = true;
}

void PhaseTrajectory3DViewWindow::exportCsv()
{
    const QString filePath = QFileDialog::getSaveFileName(
        this, QStringLiteral("Export 3D phase trajectory"), QStringLiteral("phase_trajectory_3d.csv"),
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
        if (!source()->writeAlignedCsv(file, selectedChannels({QStringLiteral("x"), QStringLiteral("y"), QStringLiteral("z")}), &error)) {
            QMessageBox::critical(this, QStringLiteral("Export failed"), error); return;
        }
    } else {
    QTextStream out(&file);
    out.setRealNumberPrecision(17);
    out << xProbeName_ << ',' << yProbeName_ << ',' << zProbeName_ << '\n';
    for (const auto& point : std::as_const(points_)) {
        out << point[0] << ',' << point[1] << ',' << point[2] << '\n';
    }    out.flush();
    if (out.status() != QTextStream::Ok) {
        QMessageBox::critical(this, QStringLiteral("Export failed"), QStringLiteral("CSV write failed.")); return;
    }
    }
    if (!file.commit()) QMessageBox::critical(this, QStringLiteral("Export failed"), file.errorString());

}

void PhaseTrajectory3DViewWindow::refreshPlots()
{
    if (!dirty_ || !isVisible()) {
        return;
    }
    dirty_ = false;
    if (autoScaleCheck_->isChecked() && !points_.isEmpty()) {
        bounds_ = viewers::TrajectoryBounds::of(points_);
        updateLabels();
    }
    bridge_->trajectoryGeometry().setTrajectory(points_, bounds_, kMaxDrawnPoints);
    if (points_.isEmpty()) bridge_->setHead(false, {});
    else bridge_->setHead(true, viewers::toScene(points_.back(), bounds_));
    updateStatus();
}

} // namespace s7studio::ui
