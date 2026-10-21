#include "viewers/line_plot_view.hpp"

#include "viewers/quick_support.hpp"

#include <QQuickWidget>
#include <QShortcut>
#include <QVBoxLayout>
#include <QVariantMap>
#include <QtGraphs/QLineSeries>

#include <algorithm>
#include <cmath>
#include <limits>

namespace s7studio::viewers {

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
    for (const auto* series : view_.series_) {
        entries.push_back(QVariantMap{{QStringLiteral("name"), series->name()},
                                      {QStringLiteral("color"), series->color()},
                                      {QStringLiteral("overlay"), view_.isOverlay(series)}});
    }
    return entries;
}

QList<QObject*> LinePlotBridge::seriesObjects() const
{
    return {view_.series_.cbegin(), view_.series_.cend()};
}

void LinePlotBridge::zoom(const double fx, const double fy, const double factor)
{
    if (!(factor > 0.0) || !std::isfinite(factor)) return;
    const double anchorX = view_.xMin_ + std::clamp(fx, 0.0, 1.0) * (view_.xMax_ - view_.xMin_);
    const double anchorY = view_.yMin_ + std::clamp(fy, 0.0, 1.0) * (view_.yMax_ - view_.yMin_);
    view_.setXRange(anchorX - (anchorX - view_.xMin_) * factor, anchorX + (view_.xMax_ - anchorX) * factor);
    view_.setYRange(anchorY - (anchorY - view_.yMin_) * factor, anchorY + (view_.yMax_ - anchorY) * factor);
}

void LinePlotBridge::pan(const double dxFraction, const double dyFraction)
{
    const double dx = dxFraction * (view_.xMax_ - view_.xMin_);
    const double dy = dyFraction * (view_.yMax_ - view_.yMin_);
    if (dx != 0.0) view_.setXRange(view_.xMin_ + dx, view_.xMax_ + dx);
    if (dy != 0.0) view_.setYRange(view_.yMin_ + dy, view_.yMax_ + dy);
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
    ensureQuickScene();
    QWidget::showEvent(event);
}

void LinePlotView::ensureQuickScene()
{
    if (quick_) return;
    quick_ = createQuickWidget();
    layout()->addWidget(quick_);
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

void LinePlotView::removeSeries(QLineSeries* series)
{
    if (!series_.removeOne(series)) return;
    overlays_.removeOne(series);
    retire(series);
    emitLegendChanged();
}

void LinePlotView::retire(QLineSeries* series)
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
    overlays_.clear();
    for (auto* series : all) retire(series);
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

void LinePlotView::setPoints(QLineSeries* series, const QList<QPointF>& points)
{
    series->replace(points);
}

void LinePlotView::setAxisTitles(const QString& xTitle, const QString& yTitle)
{
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
    for (const auto* series : series_) {
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
    for (const auto* series : series_) {
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
