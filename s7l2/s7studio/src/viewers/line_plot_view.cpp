// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-S7-plugin-permission

#include "viewers/line_plot_view.hpp"
#include <QPointer>

#include "viewers/quick_support.hpp"

#include <QQuickWidget>
#include <QShortcut>
#include <QVBoxLayout>
#include <QVariantMap>
#include <QtGraphs/QLineSeries>
#include <QtGraphs/QScatterSeries>
#include <QQuickImageProvider>
#include <QQmlEngine>
#include <mutex>

#include <algorithm>
#include <cmath>
#include <limits>

namespace s7studio::viewers {
class DensityProvider : public QQuickImageProvider {
public:
    DensityProvider() : QQuickImageProvider(QQuickImageProvider::Image) {}
    void setImage(QImage image) { std::lock_guard lock(mutex_); image_ = std::move(image); }
    QImage requestImage(const QString&, QSize* size, const QSize&) override {
        std::lock_guard lock(mutex_); if (size) *size = image_.size(); return image_;
    }
private:
    std::mutex mutex_;
    QImage image_;
};

namespace {

// Keeps an axis usable for a degenerate or non-finite request.
auto sanitizedRange(double lower, double upper) -> std::pair<double, double>
{
    if (!std::isfinite(lower) || !std::isfinite(upper)) return {0.0, 1.0};
    if (upper < lower) std::swap(lower, upper);
    if (upper - lower <= std::abs(lower) * 1e-12 || upper == lower) {
        const double half = lower == 0.0 ? 0.5 : std::abs(lower) * 0.05;
        return {lower - half, upper + half};
    }
    return {lower, upper};
}

// Y padding so a trace at its extremes is not drawn on the frame.
auto paddedRange(const double lower, const double upper) -> std::pair<double, double>
{
    const double pad = (upper - lower) * 0.04;
    return sanitizedRange(lower - pad, upper + pad);
}

} // namespace

LinePlotBridge::LinePlotBridge(LinePlotView& view)
    : QObject(&view)
    , view_(view)
{
}

auto LinePlotBridge::xMin() const -> double { return view_.xMin_; }
auto LinePlotBridge::xMax() const -> double { return view_.xMax_; }
auto LinePlotBridge::yMin() const -> double { return view_.yMin_; }
auto LinePlotBridge::yMax() const -> double { return view_.yMax_; }
auto LinePlotBridge::xTitle() const -> QString { return view_.xTitle_; }
auto LinePlotBridge::yTitle() const -> QString { return view_.yTitle_; }
auto LinePlotBridge::legendVisible() const -> bool { return view_.legendVisible_; }

auto LinePlotBridge::legend() const -> QVariantList
{
    QVariantList entries;
    for (auto* series : view_.allSeries()) {
        if ((!series->isVisible() && !checkableLegend) || view_.hiddenLegend_.contains(series)) continue;
        const auto* line = qobject_cast<const QLineSeries*>(series);
        entries.push_back(QVariantMap{{QStringLiteral("name"), series->name()},
                                      {QStringLiteral("color"), series->color()},
                                      {QStringLiteral("overlay"), line && view_.isOverlay(line)},
                                      {QStringLiteral("index"), view_.series_.indexOf(line)},
                                      {QStringLiteral("checked"), series->isVisible()}});
    }
    return entries;
}

QList<QObject*> LinePlotBridge::seriesObjects() const
{
    QList<QObject*> result; for (auto* series : view_.allSeries()) result.push_back(series); return result;
}

void LinePlotBridge::pick(double xFraction, double yFraction)
{
    if (inspectionEnabled && xFraction >= 0 && xFraction <= 1 && yFraction >= 0 && yFraction <= 1)
        emit view_.pointPicked(view_.xMin_+xFraction*(view_.xMax_-view_.xMin_), view_.yMin_+yFraction*(view_.yMax_-view_.yMin_));
}

void LinePlotBridge::hoverPoint(double xFraction, double yFraction, bool inside)
{
    if (inspectionEnabled) emit view_.hoveredPoint(view_.xMin_+xFraction*(view_.xMax_-view_.xMin_),
        view_.yMin_+yFraction*(view_.yMax_-view_.yMin_), inside);
}

void LinePlotBridge::hover(double fraction, bool inside)
{
    if (inspectionEnabled) emit view_.hovered(view_.xMin_ + fraction * (view_.xMax_ - view_.xMin_), inside);
}

void LinePlotBridge::zoom(const double fx, const double fy, const double factor)
{
    if (!(factor > 0.0) || !std::isfinite(factor)) return;
    emit view_.navigated();
    const double anchorX = view_.xMin_ + std::clamp(fx, 0.0, 1.0) * (view_.xMax_ - view_.xMin_);
    const double anchorY = view_.yMin_ + std::clamp(fy, 0.0, 1.0) * (view_.yMax_ - view_.yMin_);
    view_.setXRange(anchorX - (anchorX - view_.xMin_) * factor, anchorX + (view_.xMax_ - anchorX) * factor);
    if (!horizontalNavigation) view_.setYRange(anchorY - (anchorY - view_.yMin_) * factor, anchorY + (view_.yMax_ - anchorY) * factor);
}

void LinePlotBridge::pan(const double dxFraction, const double dyFraction)
{
    emit view_.navigated();
    const double dx = dxFraction * (view_.xMax_ - view_.xMin_);
    const double dy = dyFraction * (view_.yMax_ - view_.yMin_);
    if (dx != 0.0) view_.setXRange(view_.xMin_ + dx, view_.xMax_ + dx);
    if (!horizontalNavigation && dy != 0.0) view_.setYRange(view_.yMin_ + dy, view_.yMax_ + dy);
}

LinePlotView::LinePlotView(QWidget* parent)
    : QWidget(parent)
{
    bridge_ = new LinePlotBridge(*this);
    setFocusPolicy(Qt::StrongFocus);
    setAccessibleName(QStringLiteral("Observation plot"));
    setAccessibleDescription(QStringLiteral("Plus and minus zoom, arrows pan, Home fits all data. Axes and units are shown on the plot."));
    auto shortcut = [this](int key, auto operation) {
        auto* action = new QShortcut(QKeySequence(key), this);
        action->setContext(Qt::WidgetWithChildrenShortcut);
        connect(action, &QShortcut::activated, this, operation);
        connect(bridge_, &LinePlotBridge::markerFocusChanged, action, [this, action] { action->setEnabled(!bridge_->markerFocus); });
    };
    shortcut(Qt::Key_Plus, [this] { bridge_->zoom(.5, .5, .85); });
    shortcut(Qt::Key_Equal, [this] { bridge_->zoom(.5, .5, .85); });
    shortcut(Qt::Key_Minus, [this] { bridge_->zoom(.5, .5, 1 / .85); });
    shortcut(Qt::Key_Left, [this] { bridge_->pan(-.1, 0); });
    shortcut(Qt::Key_Right, [this] { bridge_->pan(.1, 0); });
    shortcut(Qt::Key_Up, [this] { bridge_->pan(0, .1); });
    shortcut(Qt::Key_Down, [this] { bridge_->pan(0, -.1); });
    shortcut(Qt::Key_Home, [this] { fitAll(); });
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    setMinimumSize(200, 150);
}

LinePlotView::~LinePlotView()
{
    // The scene refers to the bridge and the series, including retired ones;
    // it goes first.
    unloadQuickScene(quick_);
    quick_ = nullptr;
    qDeleteAll(retired_);
}

void LinePlotView::showEvent(QShowEvent* event)
{
    const QPointer<LinePlotView> alive(this);
    ensureQuickScene();
    if (alive) QWidget::showEvent(event);
}

void LinePlotView::ensureQuickScene()
{
    if (quick_) return;
    const QPointer<LinePlotView> alive(this);
    auto* created = createQuickWidget();
    if (!alive) { unloadQuickScene(created); return; }
    quick_ = created;
    densityProvider_ = new DensityProvider;
    densityProvider_->setImage(densityImage_);
    quick_->engine()->addImageProvider("s7density", densityProvider_);
    layout()->addWidget(quick_);
    if (!alive) return;
    loadQuickScene(*quick_, QStringLiteral("LinePlot.qml"), {{QStringLiteral("plot"), QVariant::fromValue<QObject*>(bridge_)}});
}

auto LinePlotView::addSeries(const QString& name, const SeriesStyle& style) -> QLineSeries*
{
    auto* series = new QLineSeries(this);
    series->setName(name);
    series->setColor(style.color);
    series->setWidth(style.width);
    if (style.overlay) {
        series->setOpacity(0.55);
        overlays_.push_back(series);
    }
    series_.push_back(series);
    emit bridge_->seriesAdded(series);
    emitLegendChanged();
    return series;
}

auto LinePlotView::addScatterSeries(const QString& name, QColor color) -> QScatterSeries*
{
    auto* series = new QScatterSeries(this); series->setName(name); series->setColor(color);
    scatterSeries_.push_back(series); emit bridge_->seriesAdded(series); emitLegendChanged(); return series;
}
void LinePlotView::setScatterPoints(QScatterSeries* series, const QList<QPointF>& points) { series->replace(points); }
void LinePlotView::setLegendEntryVisible(QXYSeries* series, bool visible)
{
    if (visible) hiddenLegend_.remove(series); else hiddenLegend_.insert(series); emitLegendChanged();
}
QList<QXYSeries*> LinePlotView::allSeries() const
{
    QList<QXYSeries*> all;
    for (auto* series : series_) all.push_back(series);
    for (auto* series : scatterSeries_) all.push_back(series);
    return all;
}
void LinePlotView::setDensityImage(QImage image, QRectF bounds)
{
    densityImage_ = std::move(image); if (densityProvider_) densityProvider_->setImage(densityImage_);
    bridge_->densityVisible = !densityImage_.isNull(); bridge_->densityBounds = bounds;
    bridge_->densitySource = bridge_->densityVisible ? QStringLiteral("image://s7density/%1").arg(++densityRevision_) : QString();
    emit bridge_->densityChanged();
}

void LinePlotView::removeSeries(QLineSeries* series)
{
    if (!series_.removeOne(series)) return;
    overlays_.removeOne(series);
    retire(series);
    emitLegendChanged();
}

void LinePlotView::retire(QXYSeries* series)
{
    emit bridge_->seriesRemoved(series);
    if (!quick_) {
        delete series;
        return;
    }
    series->clear();
    retired_.push_back(series);
}

void LinePlotView::clearSeries()
{
    const auto all = std::exchange(series_, {});
    const auto scatter = std::exchange(scatterSeries_, {});
    overlays_.clear();
    hiddenLegend_.clear();
    for (auto* series : all) retire(series);
    for (auto* series : scatter) retire(series);
    emitLegendChanged();
}

auto LinePlotView::isOverlay(const QLineSeries* series) const -> bool
{
    return std::find(overlays_.cbegin(), overlays_.cend(), series) != overlays_.cend();
}

void LinePlotView::setSeriesName(QLineSeries* series, const QString& name)
{
    if (series->name() == name) return;
    series->setName(name);
    emitLegendChanged();
}

void LinePlotView::setSeriesVisible(QLineSeries* series, bool visible)
{
    if (series->isVisible() == visible) return;
    series->setVisible(visible); emitLegendChanged();
}

void LinePlotView::setPoints(QLineSeries* series, const QList<QPointF>& points)
{
    series->replace(points);
}

void LinePlotView::setAxisTitles(const QString& xTitle, const QString& yTitle)
{
    if (xTitle_ == xTitle && yTitle_ == yTitle) return;
    xTitle_ = xTitle;
    yTitle_ = yTitle;
    emit bridge_->titlesChanged();
}

void LinePlotView::setLegendVisible(const bool visible)
{
    legendVisible_ = visible;
    emitLegendChanged();
}

void LinePlotView::emitLegendChanged()
{
    emit bridge_->legendChanged();
}

void LinePlotView::setXRange(const double lower, const double upper)
{
    const auto [low, high] = sanitizedRange(lower, upper);
    if (low == xMin_ && high == xMax_) return;
    xMin_ = low;
    xMax_ = high;
    emit bridge_->xRangeChanged();
    emit xRangeChanged(xMin_, xMax_);
}

void LinePlotView::setYRange(const double lower, const double upper)
{
    const auto [low, high] = sanitizedRange(lower, upper);
    if (low == yMin_ && high == yMax_) return;
    yMin_ = low;
    yMax_ = high;
    emit bridge_->yRangeChanged();
    emit yRangeChanged(yMin_, yMax_);
}

auto LinePlotView::fitYToVisibleData() -> bool
{
    double low = std::numeric_limits<double>::infinity();
    double high = -std::numeric_limits<double>::infinity();
    for (const auto* series : allSeries()) {
        if (!series->isVisible()) continue;
        for (const auto& point : series->points()) {
            if (point.x() < xMin_ || point.x() > xMax_) continue;
            low = std::min(low, point.y());
            high = std::max(high, point.y());
        }
    }
    if (!(low <= high)) return false;
    const auto [paddedLow, paddedHigh] = paddedRange(low, high);
    setYRange(paddedLow, paddedHigh);
    return true;
}

auto LinePlotView::fitAll() -> bool
{
    double xLow = std::numeric_limits<double>::infinity(), xHigh = -xLow;
    double yLow = xLow, yHigh = -xLow;
    for (const auto* series : allSeries()) {
        if (!series->isVisible()) continue;
        for (const auto& point : series->points()) {
            xLow = std::min(xLow, point.x());
            xHigh = std::max(xHigh, point.x());
            yLow = std::min(yLow, point.y());
            yHigh = std::max(yHigh, point.y());
        }
    }
    if (!(xLow <= xHigh)) return false;
    const auto [paddedXLow, paddedXHigh] = paddedRange(xLow, xHigh);
    const auto [paddedYLow, paddedYHigh] = paddedRange(yLow, yHigh);
    setXRange(paddedXLow, paddedXHigh);
    setYRange(paddedYLow, paddedYHigh);
    return true;
}

} // namespace s7studio::viewers
