// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-S7-plugin-permission
#include "viewers/trajectory_plot.hpp"
#include "viewers/quick_support.hpp"
#include <QQuickWidget>
#include <QPointer>
#include <QVBoxLayout>
#include <QVariantMap>
#include <algorithm>
#include <cmath>
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

void Trajectory3DBridge::setAntialiased(const bool antialiased)
{
    if (antialiased_ == antialiased) return;
    antialiased_ = antialiased;
    emit antialiasedChanged();
}

void TrajectoryPlotView::setMinimumFrameInterval(const int milliseconds)
{
    frameIntervalMs_ = milliseconds;
    if (quick_) viewers::setMinimumFrameInterval(*quick_, milliseconds);
}

void TrajectoryPlotView::setAxisLabels(const viewers::TrajectoryBounds& bounds, const QStringList& axes, bool compact)
{
    const float e = kExtent;
    const float offset = e * 0.12f;
    QVariantList labels;
    // Tick values along the three labelled edges (the same edges and
    // fractions TrajectoryFrameGeometry puts ticks on).
    for (const double fraction : viewers::kTrajectoryTickFractions) {
        if (compact && fraction != .5) continue;
        const auto t = static_cast<float>(-e + 2.0 * fraction * e);
        const auto at = [&](std::size_t axis) {
            const double span = bounds.max[axis] - bounds.min[axis];
            return number(bounds.min[axis] + fraction * span, span);
        };
        labels << label({t, -e - offset, e + offset}, at(0), false);
        labels << label({e + offset, -e - offset, -t}, at(1), false);
        labels << label({-e - offset, t, e + offset}, at(2), false);
    }
    labels << label({0.0f, -e - 2.6f * offset, e + 2.6f * offset}, compact ? QString("X") : QStringLiteral("X: ") + axes.value(0), true);
    labels << label({e + 2.6f * offset, -e - 2.6f * offset, 0.0f}, compact ? QString("Y") : QStringLiteral("Y: ") + axes.value(1), true);
    labels << label({-e - 2.6f * offset, e + offset, e + offset}, compact ? QString("Z") : QStringLiteral("Z: ") + axes.value(2), true);
    bridge_->setLabels(labels);
}


TrajectoryPlotView::TrajectoryPlotView(QWidget* parent) : QWidget(parent), bridge_(new Trajectory3DBridge(this)) {
    setObjectName("viewer.trajectoryPresentation"); setMinimumSize(180, 150);
    auto* root = new QVBoxLayout(this); root->setContentsMargins(0, 0, 0, 0);
    bridge_->setViewRotation({-25.f, 35.f, 0.f});
}
TrajectoryPlotView::~TrajectoryPlotView() { viewers::unloadQuickScene(quick_); }
void TrajectoryPlotView::showEvent(QShowEvent* event) {
    const QPointer<TrajectoryPlotView> alive(this);
    if (!quick_) {
        auto* created = viewers::createQuickWidget();
        if (!alive) { viewers::unloadQuickScene(created); return; }
        quick_ = created; viewers::setMinimumFrameInterval(*quick_, frameIntervalMs_); layout()->addWidget(quick_);
        if (!alive) return;
        viewers::loadQuickScene(*quick_, "Trajectory3D.qml", {{"trajectoryView", QVariant::fromValue<QObject*>(bridge_)}});
    }
    if (alive) QWidget::showEvent(event);
}
}
