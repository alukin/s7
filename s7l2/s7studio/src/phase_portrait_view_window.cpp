#include "phase_portrait_view_window.hpp"

#include "qcp.h"

#include <QCheckBox>
#include <QColor>
#include <QFile>
#include <QFileDialog>
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
const QColor kTrajectoryColor("#2ca02c");
} // namespace

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

    clearButton_ = new QPushButton(QStringLiteral("Clear"), this);
    exportButton_ = new QPushButton(QStringLiteral("Export CSV"), this);
    controls->addWidget(clearButton_);
    controls->addWidget(exportButton_);
    controls->addStretch(1);
    statusLabel_ = new QLabel(this);
    controls->addWidget(statusLabel_);

    rootLayout->addLayout(controls);

    plot_ = new QCustomPlot(this);
    rootLayout->addWidget(plot_, 1);

    configurePlot();

    repaintTimer_ = new QTimer(this);
    repaintTimer_->setInterval(33);
    connect(repaintTimer_, &QTimer::timeout, this, &PhasePortraitViewWindow::refreshPlot);
    repaintTimer_->start();

    connect(clearButton_, &QPushButton::clicked, this, &PhasePortraitViewWindow::clearBuffers);
    connect(exportButton_, &QPushButton::clicked, this, &PhasePortraitViewWindow::exportRecordedCsv);
}

void PhasePortraitViewWindow::configurePlot()
{
    plot_->setInteractions(QCP::iRangeDrag | QCP::iRangeZoom);
    plot_->xAxis->setLabel(xProbeName_);
    plot_->yAxis->setLabel(yProbeName_);

    curve_ = new QCPCurve(plot_->xAxis, plot_->yAxis);
    curve_->setLineStyle(QCPCurve::lsLine);
    curve_->setScatterStyle(QCPScatterStyle(QCPScatterStyle::ssNone));
    QPen pen(kTrajectoryColor);
    pen.setWidthF(1.4);
    curve_->setPen(pen);
}

void PhasePortraitViewWindow::appendXSample(const long step, const double value)
{
    if (pendingYStep_ && *pendingYStep_ == step) {
        emitPoint(value, pendingYValue_);
        pendingYStep_.reset();
        pendingXStep_.reset();
    } else {
        pendingXStep_ = step;
        pendingXValue_ = value;
    }
}

void PhasePortraitViewWindow::appendYSample(const long step, const double value)
{
    if (pendingXStep_ && *pendingXStep_ == step) {
        emitPoint(pendingXValue_, value);
        pendingXStep_.reset();
        pendingYStep_.reset();
    } else {
        pendingYStep_ = step;
        pendingYValue_ = value;
    }
}

void PhasePortraitViewWindow::emitPoint(const double x, const double y)
{
    if (xValues_.size() >= kMaxPoints) {
        xValues_.pop_front();
        yValues_.pop_front();
    }
    xValues_.push_back(x);
    yValues_.push_back(y);
    dirty_ = true;
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
    if (xValues_.size() >= kMaxPoints) {
        parts << QStringLiteral("trajectory trimmed to %1 points").arg(kMaxPoints);
    }
    if (droppedSamples_ > 0) {
        parts << QStringLiteral("display dropped %1 samples").arg(droppedSamples_);
    }
    statusLabel_->setText(parts.join(QStringLiteral(" | ")));
}

void PhasePortraitViewWindow::clearBuffers()
{
    xValues_.clear();
    yValues_.clear();
    pendingXStep_.reset();
    pendingYStep_.reset();
    droppedSamples_ = 0;
    updateStatus();

    curve_->data()->clear();

    dirty_ = true;
    plot_->replot(QCustomPlot::rpQueuedReplot);
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

    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::critical(this, QStringLiteral("Export failed"), file.errorString());
        return;
    }

    QTextStream out(&file);
    out << xProbeName_ << ',' << yProbeName_ << '\n';
    const int count = std::min(xValues_.size(), yValues_.size());
    for (int i = 0; i < count; ++i) {
        out << xValues_[i] << ',' << yValues_[i] << '\n';
    }
}

void PhasePortraitViewWindow::refreshPlot()
{
    // Hidden diagram observers keep collecting pairs without rebuilding a
    // potentially large curve until the viewer is opened.
    if (!dirty_ || !isVisible()) {
        return;
    }

    dirty_ = false;
    curve_->setData(xValues_, yValues_);

    if (autoScaleCheck_->isChecked()) {
        plot_->rescaleAxes(true);
    }

    plot_->replot(QCustomPlot::rpQueuedReplot);
}

} // namespace s7studio::ui
