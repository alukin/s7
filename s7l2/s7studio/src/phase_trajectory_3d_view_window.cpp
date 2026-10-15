#include "phase_trajectory_3d_view_window.hpp"

#include "qcp.h"

#include <QCheckBox>
#include <QColor>
#include <QFile>
#include <QFileDialog>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPen>
#include <QPushButton>
#include <QStringList>
#include <QTextStream>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <utility>

namespace s7studio::ui {

namespace {

auto configureProjection(QCustomPlot* plot, const QString& xLabel, const QString& yLabel) -> QCPCurve*
{
    plot->setInteractions(QCP::iRangeDrag | QCP::iRangeZoom);
    plot->xAxis->setLabel(xLabel);
    plot->yAxis->setLabel(yLabel);
    auto* curve = new QCPCurve(plot->xAxis, plot->yAxis);
    curve->setLineStyle(QCPCurve::lsLine);
    curve->setPen(QPen(QColor("#2ca02c"), 1.4));
    return curve;
}

} // namespace

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
    setWindowFlag(Qt::Window, true);
    setWindowFlag(Qt::WindowMinimizeButtonHint, true);
    setWindowFlag(Qt::WindowMaximizeButtonHint, true);
    setWindowFlag(Qt::WindowCloseButtonHint, true);
    setAttribute(Qt::WA_QuitOnClose, false);
    setMinimumSize(900, 650);
    setWindowTitle(QStringLiteral("3D Phase Trajectory - %1, %2, %3")
                       .arg(xProbeName_, yProbeName_, zProbeName_));
    resize(1100, 760);

    auto* root = new QVBoxLayout(this);
    auto* controls = new QHBoxLayout();
    autoScaleCheck_ = new QCheckBox(QStringLiteral("Auto Scale"), this);
    autoScaleCheck_->setChecked(true);
    controls->addWidget(autoScaleCheck_);
    clearButton_ = new QPushButton(QStringLiteral("Clear"), this);
    exportButton_ = new QPushButton(QStringLiteral("Export CSV"), this);
    controls->addWidget(clearButton_);
    controls->addWidget(exportButton_);
    controls->addStretch(1);
    statusLabel_ = new QLabel(this);
    controls->addWidget(statusLabel_);
    root->addLayout(controls);

    auto* plots = new QGridLayout();
    xyPlot_ = new QCustomPlot(this);
    xzPlot_ = new QCustomPlot(this);
    yzPlot_ = new QCustomPlot(this);
    xyCurve_ = configureProjection(xyPlot_, xProbeName_, yProbeName_);
    xzCurve_ = configureProjection(xzPlot_, xProbeName_, zProbeName_);
    yzCurve_ = configureProjection(yzPlot_, yProbeName_, zProbeName_);
    plots->addWidget(xyPlot_, 0, 0);
    plots->addWidget(xzPlot_, 0, 1);
    plots->addWidget(yzPlot_, 1, 0, 1, 2);
    root->addLayout(plots, 1);

    repaintTimer_ = new QTimer(this);
    repaintTimer_->setInterval(33);
    connect(repaintTimer_, &QTimer::timeout, this, &PhaseTrajectory3DViewWindow::refreshPlots);
    repaintTimer_->start();
    connect(clearButton_, &QPushButton::clicked, this, &PhaseTrajectory3DViewWindow::clearBuffers);
    connect(exportButton_, &QPushButton::clicked, this, &PhaseTrajectory3DViewWindow::exportCsv);
}

void PhaseTrajectory3DViewWindow::appendXSample(const long step, const double value)
{
    addCoordinate(step, value, 0);
}

void PhaseTrajectory3DViewWindow::appendYSample(const long step, const double value)
{
    addCoordinate(step, value, 1);
}

void PhaseTrajectory3DViewWindow::appendZSample(const long step, const double value)
{
    addCoordinate(step, value, 2);
}

void PhaseTrajectory3DViewWindow::addCoordinate(const long step, const double value, const int axis)
{
    auto& pending = pending_[step];
    if (axis == 0) {
        pending.point.x = value;
        pending.hasX = true;
    } else if (axis == 1) {
        pending.point.y = value;
        pending.hasY = true;
    } else {
        pending.point.z = value;
        pending.hasZ = true;
    }
    if (pending.hasX && pending.hasY && pending.hasZ) {
        if (points_.size() >= kMaxPoints) {
            points_.pop_front();
        }
        points_.push_back(pending.point);
        pending_.erase(step);
        dirty_ = true;
    }
    while (pending_.size() > 8) {
        pending_.erase(pending_.begin());
        ++incompletePointsDropped_;
    }
}

void PhaseTrajectory3DViewWindow::clearSamples()
{
    clearBuffers();
}

void PhaseTrajectory3DViewWindow::setDroppedSampleCount(const std::uint64_t dropped)
{
    droppedSamples_ = dropped;
    updateStatus();
}

void PhaseTrajectory3DViewWindow::updateStatus()
{
    QStringList parts;
    if (points_.size() >= kMaxPoints) {
        parts << QStringLiteral("trajectory trimmed to %1 points").arg(kMaxPoints);
    }
    if (const auto totalDropped = droppedSamples_ + incompletePointsDropped_; totalDropped > 0) {
        parts << QStringLiteral("display dropped %1 samples").arg(totalDropped);
    }
    statusLabel_->setText(parts.join(QStringLiteral(" | ")));
}

void PhaseTrajectory3DViewWindow::clearBuffers()
{
    points_.clear();
    pending_.clear();
    droppedSamples_ = 0;
    incompletePointsDropped_ = 0;
    updateStatus();
    for (auto* plot : {xyPlot_, xzPlot_, yzPlot_}) {
        auto* curve = plot == xyPlot_ ? xyCurve_ : (plot == xzPlot_ ? xzCurve_ : yzCurve_);
        curve->data()->clear();
        plot->replot(QCustomPlot::rpQueuedReplot);
    }
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
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::critical(this, QStringLiteral("Export failed"), file.errorString());
        return;
    }
    QTextStream out(&file);
    out << xProbeName_ << ',' << yProbeName_ << ',' << zProbeName_ << '\n';
    for (const auto& point : points_) {
        out << point.x << ',' << point.y << ',' << point.z << '\n';
    }
}

void PhaseTrajectory3DViewWindow::refreshPlots()
{
    if (!dirty_) {
        return;
    }
    dirty_ = false;
    QVector<double> x;
    QVector<double> y;
    QVector<double> z;
    x.reserve(points_.size());
    y.reserve(points_.size());
    z.reserve(points_.size());
    for (const auto& point : points_) {
        x.push_back(point.x);
        y.push_back(point.y);
        z.push_back(point.z);
    }
    xyCurve_->setData(x, y);
    xzCurve_->setData(x, z);
    yzCurve_->setData(y, z);
    if (autoScaleCheck_->isChecked()) {
        xyPlot_->rescaleAxes(true);
        xzPlot_->rescaleAxes(true);
        yzPlot_->rescaleAxes(true);
    }
    xyPlot_->replot(QCustomPlot::rpQueuedReplot);
    xzPlot_->replot(QCustomPlot::rpQueuedReplot);
    yzPlot_->replot(QCustomPlot::rpQueuedReplot);
    updateStatus();
}

} // namespace s7studio::ui
