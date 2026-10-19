#include "phase_trajectory_3d_view_window.hpp"

#include "viewers/quick_support.hpp"

#include <QCheckBox>
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

namespace {

constexpr float kExtent = viewers::kTrajectoryHalfExtent;

auto label(const QVector3D& position, const QString& text, const bool title) -> QVariant
{
    return QVariantMap{{QStringLiteral("position"), position},
                       {QStringLiteral("text"), text},
                       {QStringLiteral("title"), title}};
}

// A tick value with about three significant digits of the axis span, so
// midpoints read "0.00" rather than "-0.0007885".
auto number(const double value, const double span) -> QString
{
    if (!(span > 0.0) || !std::isfinite(span)) return QString::number(value, 'g', 4);
    const int decimals = std::clamp(2 - static_cast<int>(std::floor(std::log10(span))), 0, 12);
    if (decimals == 0 && std::abs(value) >= 1e6) return QString::number(value, 'g', 4);
    auto text = QString::number(value, 'f', decimals);
    if (text.startsWith(QLatin1Char('-')) && text.toDouble() == 0.0) text.remove(0, 1);
    return text;
}

} // namespace

Trajectory3DBridge::Trajectory3DBridge(QObject* parent)
    : QObject(parent)
{
}

void Trajectory3DBridge::setLabels(QVariantList labels)
{
    labels_ = std::move(labels);
    emit labelsChanged();
}

void Trajectory3DBridge::setHead(const bool hasHead, const QVector3D head)
{
    if (hasHead_ == hasHead && head_ == head) return;
    hasHead_ = hasHead;
    head_ = head;
    emit headChanged();
}

void Trajectory3DBridge::setViewRotation(const QVector3D rotation)
{
    viewRotation_ = rotation;
    emit viewRotationChanged();
}

auto PhaseTrajectory3DViewWindow::viewerType() -> viewers::ViewerType
{
    return {QStringLiteral("PhaseTrajectory3D"), QStringLiteral("3D Phase Trajectory"), QStringLiteral("3→plot"),
            QStringLiteral("Plots three observed signals as a continuous trajectory in 3D."),
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
    setWindowFlag(Qt::Window, true);
    setWindowFlag(Qt::WindowMinimizeButtonHint, true);
    setWindowFlag(Qt::WindowMaximizeButtonHint, true);
    setWindowFlag(Qt::WindowCloseButtonHint, true);
    setAttribute(Qt::WA_QuitOnClose, false);
    setMinimumSize(640, 560);
    setWindowTitle(QStringLiteral("3D Phase Trajectory - %1, %2, %3")
                       .arg(xProbeName_, yProbeName_, zProbeName_));
    resize(900, 760);

    bridge_ = new Trajectory3DBridge(this);

    auto* root = new QVBoxLayout(this);
    auto* controls = new QHBoxLayout();
    autoScaleCheck_ = new QCheckBox(QStringLiteral("Auto Scale"), this);
    autoScaleCheck_->setChecked(true);
    controls->addWidget(autoScaleCheck_);
    clearButton_ = new QPushButton(QStringLiteral("Clear"), this);
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
    controls->addWidget(statusLabel_);
    root->addLayout(controls);
    // The Qt Quick 3D scene is created on first show (showEvent).

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

PhaseTrajectory3DViewWindow::~PhaseTrajectory3DViewWindow()
{
    // The scene refers to the bridge's geometries; it goes first.
    viewers::unloadQuickScene(quick_);
    quick_ = nullptr;
}

void PhaseTrajectory3DViewWindow::showEvent(QShowEvent* event)
{
    if (!quick_) {
        quick_ = viewers::createQuickWidget();
        quick_->setToolTip(QStringLiteral("Drag to rotate, scroll to zoom."));
        static_cast<QVBoxLayout*>(layout())->addWidget(quick_, 1);
        viewers::loadQuickScene(*quick_, QStringLiteral("Trajectory3D.qml"),
                                {{QStringLiteral("trajectoryView"), QVariant::fromValue<QObject*>(bridge_)}});
    }
    QWidget::showEvent(event);
}

auto PhaseTrajectory3DViewWindow::subscribes(const QString& probeName) const -> bool
{
    return probeName == xProbeName_ || probeName == yProbeName_ || probeName == zProbeName_;
}

void PhaseTrajectory3DViewWindow::appendObservedSample(const viewers::ObservedSample& sample)
{
    if (sample.probeName == xProbeName_) appendXSample(sample.step, sample.value);
    if (sample.probeName == yProbeName_) appendYSample(sample.step, sample.value);
    if (sample.probeName == zProbeName_) appendZSample(sample.step, sample.value);
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

void PhaseTrajectory3DViewWindow::addCoordinate(const long step, const double value, const std::size_t axis)
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
    droppedSamples_ = dropped;
    updateStatus();
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
    statusLabel_->setText(parts.join(QStringLiteral(" | ")));
}

void PhaseTrajectory3DViewWindow::updateLabels()
{
    const float e = kExtent;
    const float offset = e * 0.12f;
    QVariantList labels;
    // Tick values along the three labelled edges (the same edges and
    // fractions TrajectoryFrameGeometry puts ticks on).
    for (const double fraction : viewers::kTrajectoryTickFractions) {
        const auto t = static_cast<float>(-e + 2.0 * fraction * e);
        const auto at = [&](std::size_t axis) {
            const double span = bounds_.max[axis] - bounds_.min[axis];
            return number(bounds_.min[axis] + fraction * span, span);
        };
        labels << label({t, -e - offset, e + offset}, at(0), false);
        labels << label({e + offset, -e - offset, -t}, at(1), false);
        labels << label({-e - offset, t, e + offset}, at(2), false);
    }
    labels << label({0.0f, -e - 2.6f * offset, e + 2.6f * offset}, QStringLiteral("X: ") + xProbeName_, true);
    labels << label({e + 2.6f * offset, -e - 2.6f * offset, 0.0f}, QStringLiteral("Y: ") + yProbeName_, true);
    labels << label({-e - 2.6f * offset, e + offset, e + offset}, QStringLiteral("Z: ") + zProbeName_, true);
    bridge_->setLabels(labels);
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
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::critical(this, QStringLiteral("Export failed"), file.errorString());
        return;
    }
    QTextStream out(&file);
    out << xProbeName_ << ',' << yProbeName_ << ',' << zProbeName_ << '\n';
    for (const auto& point : std::as_const(points_)) {
        out << point[0] << ',' << point[1] << ',' << point[2] << '\n';
    }
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
