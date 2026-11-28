// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-S7-plugin-permission
#include "viewers/plot_print.hpp"

#include "viewers/line_plot_view.hpp"

#include <QFontMetricsF>
#include <QLineSeries>
#include <QPainter>
#include <QPainterPath>
#include <QScatterSeries>

#include <algorithm>
#include <cmath>
#include <limits>

namespace s7studio::viewers {
namespace {

// LinePlot.qml axisTickFormat: decimals implied by the step (one more for
// 2.5), scientific notation for very large or very small values.
QString ordinaryLabel(double value, double low, double high, double step)
{
    if (!(step > 0) || !std::isfinite(step) || !std::isfinite(low) || !std::isfinite(high)) return QString::number(value, 'g', 6);
    const int stepExponent = static_cast<int>(std::floor(std::log10(step) + 1e-9));
    const double mantissa = step / std::pow(10.0, stepExponent);
    const int extra = std::abs(mantissa - std::round(mantissa)) > 1e-6 ? 1 : 0;
    const double magnitude = std::max(std::abs(low), std::abs(high));
    if (std::abs(value) < step * 1e-9) value = 0;
    if (magnitude >= 1e6 || stepExponent <= -5)
        return QString::number(value, 'e', std::max(0, static_cast<int>(std::floor(std::log10(magnitude) + 1e-9)) - stepExponent + extra));
    return QString::number(value, 'f', std::max(0, extra - stepExponent));
}

// LinePlot.qml verticalTickFormat for divided (instrument) Y axes.
QString divisionLabel(double value, double span, int divisions)
{
    const double step = std::abs(span) / divisions;
    if (!(step > 0) || step < 1e-6 || step >= 1e6) return QString::number(value, 'e', 4);
    if (std::abs(value) < step * 1e-9) value = 0;
    return QString::number(value, 'f', std::max(0, 2 - static_cast<int>(std::floor(std::log10(step)))));
}

// LinePlot.qml channelTickText.
QString channelLabel(double value, double division)
{
    if (!std::isfinite(value)) return QStringLiteral("—");
    const double step = std::abs(division);
    if (std::abs(value) < step * 1e-8) return QStringLiteral("0");
    if (!(step >= 1e-6 && step < 1e6)) return QString::number(value, 'g', 5);
    const int exponent = static_cast<int>(std::floor(std::log10(step) + 1e-9));
    const double mantissa = step / std::pow(10.0, exponent);
    if (std::abs(mantissa * 2 - std::round(mantissa * 2)) > 1e-6) return QString::number(value, 'g', 5);
    return QString::number(value, 'f', std::min(10, std::max(0, -exponent + (std::abs(mantissa - std::round(mantissa)) > 1e-6 ? 1 : 0))));
}

struct Axis {
    double low = 0, high = 1, step = 0, anchor = 0;
    int minorPerMajor = 1;
    std::function<QString(double)> label;
};

} // namespace

std::size_t PlotSnapshot::pointCount() const
{
    std::size_t count = 0;
    for (const auto& line : series) count += static_cast<std::size_t>(line.points.size());
    return count;
}

double niceTickStep(double span, int divisions)
{
    const double raw = span / std::max(2, divisions);
    if (!(raw > 0) || !std::isfinite(raw)) return 0;
    const double decade = std::pow(10.0, std::floor(std::log10(raw)));
    const double fraction = raw / decade;
    const double step = fraction <= 1 ? 1 : fraction <= 2 ? 2 : fraction <= 2.5 ? 2.5 : fraction <= 5 ? 5 : 10;
    return step * decade;
}

std::vector<double> axisTicks(double low, double high, double step, double anchor)
{
    std::vector<double> ticks;
    if (!(step > 0) || !std::isfinite(low) || !std::isfinite(high) || high <= low || (high - low) / step > 200) return ticks;
    const double first = anchor + std::ceil((low - anchor) / step - 1e-9) * step;
    for (double value = first; value <= high + step * 1e-9; value += step) ticks.push_back(value);
    return ticks;
}

QColor printColor(const QColor& color)
{
    if (!color.isValid()) return Qt::black;
    float hue = 0, saturation = 0, lightness = 0, alpha = 1;
    color.toHsl().getHslF(&hue, &saturation, &lightness, &alpha);
    if (lightness <= 0.5f) return color;
    return QColor::fromHslF(hue, saturation, std::max(0.3f, lightness - (lightness - 0.5f) * 1.6f - 0.08f), alpha);
}

PlotSnapshot snapshotPlot(const LinePlotView& view)
{
    PlotSnapshot plot;
    const auto* bridge = view.bridge_;
    plot.xTitle = view.xTitle();
    plot.yTitle = view.yTitle();
    plot.xMin = view.xMin();
    plot.xMax = view.xMax();
    plot.yMin = view.yMin();
    plot.yMax = view.yMax();
    plot.compactAxes = bridge->compactAxes;
    plot.timeDivisions = bridge->timeDivisions;
    plot.verticalDivisions = bridge->verticalDivisions;
    plot.channelAxis = bridge->channelAxis;
    plot.channelGain = bridge->channelGain;
    plot.channelOffset = bridge->channelOffset;
    plot.channelAxisColor = bridge->channelAxisColor;
    plot.cursorVisible = bridge->cursorVisible;
    plot.cursorX = bridge->cursorX;
    plot.density = view.densityImage_;
    plot.densityBounds = bridge->densityBounds;
    plot.legendVisible = view.legendVisible();
    for (auto* series : view.allSeries()) {
        const auto* line = qobject_cast<const QLineSeries*>(series);
        const bool overlay = line && view.isOverlay(line);
        if (series->isVisible()) {
            PlotSnapshot::Series copy;
            copy.name = series->name();
            copy.color = series->color();
            copy.overlay = overlay;
            if (line) copy.width = line->width();
            copy.kind = line ? PlotSnapshot::Series::Kind::Line
                             : view.denseScatter_.contains(series) ? PlotSnapshot::Series::Kind::Dots : PlotSnapshot::Series::Kind::Markers;
            copy.points = series->points();
            plot.series.push_back(std::move(copy));
        }
        if (series->isVisible() && !view.hiddenLegend_.contains(series)) plot.legend.push_back({series->name(), series->color(), overlay, !line});
    }
    return plot;
}

void paintPlot(QPainter& painter, const QRectF& area, const PlotSnapshot& plot)
{
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing);
    painter.fillRect(area, Qt::white);
    QFont font = painter.font();
    font.setPointSizeF(7.5);
    painter.setFont(font);
    const QFontMetricsF metrics(font, painter.device());
    const double lineHeight = metrics.height();

    double top = area.top();
    if (!plot.heading.isEmpty()) {
        QFont bold = font;
        bold.setBold(true);
        bold.setPointSizeF(8.5);
        painter.setFont(bold);
        painter.setPen(Qt::black);
        painter.drawText(QRectF(area.left(), top, area.width(), lineHeight * 1.4), Qt::AlignLeft | Qt::AlignVCenter, plot.heading);
        painter.setFont(font);
        top += lineHeight * 1.5;
    }

    const double xLow = plot.xMin, xHigh = plot.xMax > plot.xMin ? plot.xMax : plot.xMin + 1;
    const double yLow = plot.yMin, yHigh = plot.yMax > plot.yMin ? plot.yMax : plot.yMin + 1;
    const double roughWidth = area.width() - 80, roughHeight = area.bottom() - top - 50;

    Axis x{xLow, xHigh};
    if (plot.timeDivisions) {
        x.step = (xHigh - xLow) / 10;
        x.anchor = xLow;
        x.label = [](double value) { return QString::number(value, 'g', 6); };
    } else {
        x.step = plot.compactAxes ? niceTickStep(xHigh - xLow, std::min(6, static_cast<int>(roughWidth / 70)))
                                  : niceTickStep(xHigh - xLow, std::max(2, static_cast<int>(roughWidth / 80)));
        x.label = plot.compactAxes ? std::function<QString(double)>([](double value) { return QString::number(value, 'g', 5); })
                                   : [low = xLow, high = xHigh, step = x.step](double value) { return ordinaryLabel(value, low, high, step); };
    }
    Axis y{yLow, yHigh};
    if (plot.verticalDivisions > 0) {
        y.step = (yHigh - yLow) / plot.verticalDivisions;
        y.label = [span = yHigh - yLow, divisions = plot.verticalDivisions](double value) { return divisionLabel(value, span, divisions); };
    } else {
        y.step = plot.compactAxes ? niceTickStep(yHigh - yLow, std::min(6, static_cast<int>(roughHeight / 40)))
                                  : niceTickStep(yHigh - yLow, std::max(2, static_cast<int>(roughHeight / 50)));
        y.label = plot.compactAxes ? std::function<QString(double)>([](double value) { return QString::number(value, 'g', 5); })
                                   : [low = yLow, high = yHigh, step = y.step](double value) { return ordinaryLabel(value, low, high, step); };
    }
    const auto xTicks = axisTicks(xLow, xHigh, x.step, x.anchor);
    // Channel axes label the selected channel's own units at each division.
    std::vector<std::pair<double, QString>> yLabels;
    if (plot.channelAxis && plot.verticalDivisions > 0 && plot.channelGain != 0) {
        const double division = (yHigh - yLow) / plot.verticalDivisions;
        const double first = std::ceil(yLow / division - 1e-9);
        const int count = std::min(10, std::max(0, static_cast<int>(std::floor(yHigh / division + 1e-9) - first + 1)));
        for (int i = 0; i < count; ++i) {
            const double display = (first + i) * division;
            yLabels.emplace_back(display, channelLabel((display - plot.channelOffset) / plot.channelGain, division / plot.channelGain));
        }
    } else {
        for (const double value : axisTicks(yLow, yHigh, y.step, y.anchor)) yLabels.emplace_back(value, y.label(value));
    }

    double labelWidth = 0;
    for (const auto& [value, text] : yLabels) labelWidth = std::max(labelWidth, metrics.horizontalAdvance(text));
    const double left = area.left() + (plot.yTitle.isEmpty() ? 0 : lineHeight + 4) + labelWidth + 6;
    const double bottom = area.bottom() - lineHeight - 4 - (plot.xTitle.isEmpty() ? 0 : lineHeight + 2);
    const double right = area.right() - 6 - metrics.horizontalAdvance(x.label(xHigh)) / 2;
    const QRectF frame(QPointF(left, top + lineHeight / 2), QPointF(right, bottom));
    if (frame.width() < 20 || frame.height() < 20) {
        painter.restore();
        return;
    }
    const auto mapX = [&](double value) { return frame.left() + (value - xLow) / (xHigh - xLow) * frame.width(); };
    const auto mapY = [&](double value) { return frame.bottom() - (value - yLow) / (yHigh - yLow) * frame.height(); };

    // Grid: major lines at ticks, one minor line between them.
    const QPen major(QColor(0xc8, 0xc8, 0xc8), 0.75), minor(QColor(0xe6, 0xe6, 0xe6), 0.5);
    const auto grid = [&](const std::vector<double>& ticks, double step, bool vertical, double low, double high) {
        for (const double value : ticks) {
            for (const double line : {value, value - step / 2}) {
                if (line < low - 1e-12 || line > high + 1e-12) continue;
                painter.setPen(line == value ? major : minor);
                if (vertical) painter.drawLine(QPointF(mapX(line), frame.top()), QPointF(mapX(line), frame.bottom()));
                else painter.drawLine(QPointF(frame.left(), mapY(line)), QPointF(frame.right(), mapY(line)));
            }
        }
    };
    grid(xTicks, x.step, true, xLow, xHigh);
    std::vector<double> yTickValues;
    for (const auto& entry : yLabels) yTickValues.push_back(entry.first);
    grid(yTickValues, plot.channelAxis ? (yHigh - yLow) / std::max(1, plot.verticalDivisions) : y.step, false, yLow, yHigh);

    // Data, clipped to the plot frame.
    painter.save();
    painter.setClipRect(frame);
    if (!plot.density.isNull() && plot.densityBounds.isValid()) {
        const QRectF target(QPointF(mapX(plot.densityBounds.left()), mapY(plot.densityBounds.bottom())),
                            QPointF(mapX(plot.densityBounds.right()), mapY(plot.densityBounds.top())));
        painter.drawImage(target.normalized(), plot.density);
    }
    for (const auto& series : plot.series) {
        auto color = printColor(series.color);
        if (series.kind == PlotSnapshot::Series::Kind::Line) {
            if (series.overlay) color.setAlphaF(0.8f);
            QPen pen(color, std::max(1.0, series.width * 0.8));
            pen.setJoinStyle(Qt::RoundJoin);
            pen.setCapStyle(Qt::RoundCap);
            if (series.overlay) pen.setStyle(Qt::DashLine);
            painter.setPen(pen);
            painter.setBrush(Qt::NoBrush);
            QPainterPath path;
            bool open = false;
            for (const auto& point : series.points) {
                if (!std::isfinite(point.x()) || !std::isfinite(point.y())) {
                    open = false;  // a gap in the recording
                    continue;
                }
                const QPointF mapped(mapX(point.x()), mapY(std::clamp(point.y(), yLow - (yHigh - yLow) * 4, yHigh + (yHigh - yLow) * 4)));
                if (open) path.lineTo(mapped);
                else path.moveTo(mapped);
                open = true;
            }
            painter.drawPath(path);
        } else {
            const bool dots = series.kind == PlotSnapshot::Series::Kind::Dots;
            const double radius = dots ? 1.0 : 3.2;
            painter.setPen(dots ? QPen(Qt::NoPen) : QPen(Qt::white, 0.8));
            painter.setBrush(color);
            for (const auto& point : series.points) {
                if (!std::isfinite(point.x()) || !std::isfinite(point.y())) continue;
                painter.drawEllipse(QPointF(mapX(point.x()), mapY(point.y())), radius, radius);
            }
        }
    }
    if (plot.cursorVisible && plot.cursorX >= xLow && plot.cursorX <= xHigh) {
        painter.setPen(QPen(QColor(0x50, 0x50, 0x50), 0.75, Qt::DashLine));
        painter.drawLine(QPointF(mapX(plot.cursorX), frame.top()), QPointF(mapX(plot.cursorX), frame.bottom()));
    }
    painter.restore();

    // Frame, tick labels and titles.
    painter.setPen(QPen(QColor(0x60, 0x60, 0x60), 0.75));
    painter.setBrush(Qt::NoBrush);
    painter.drawRect(frame);
    painter.setPen(QColor(0x30, 0x30, 0x30));
    for (const double value : xTicks) {
        const auto text = x.label(value);
        const double width = metrics.horizontalAdvance(text);
        painter.drawText(QRectF(mapX(value) - width / 2 - 2, frame.bottom() + 2, width + 4, lineHeight), Qt::AlignCenter, text);
    }
    if (plot.channelAxis) painter.setPen(printColor(plot.channelAxisColor.isValid() ? plot.channelAxisColor : QColor(Qt::black)));
    for (const auto& [value, text] : yLabels)
        painter.drawText(QRectF(frame.left() - labelWidth - 6, mapY(value) - lineHeight / 2, labelWidth + 2, lineHeight),
                         Qt::AlignRight | Qt::AlignVCenter, text);
    painter.setPen(QColor(0x30, 0x30, 0x30));
    if (!plot.xTitle.isEmpty())
        painter.drawText(QRectF(frame.left(), area.bottom() - lineHeight - 2, frame.width(), lineHeight), Qt::AlignCenter, plot.xTitle);
    if (!plot.yTitle.isEmpty()) {
        painter.save();
        painter.translate(area.left() + lineHeight / 2 + 1, frame.center().y());
        painter.rotate(-90);
        if (plot.channelAxis) painter.setPen(printColor(plot.channelAxisColor.isValid() ? plot.channelAxisColor : QColor(Qt::black)));
        painter.drawText(QRectF(-frame.height() / 2, -lineHeight / 2, frame.height(), lineHeight), Qt::AlignCenter, plot.yTitle);
        painter.restore();
    }

    // Legend, top right inside the frame.
    if (plot.legendVisible && !plot.legend.empty()) {
        double width = 0;
        for (const auto& entry : plot.legend)
            width = std::max(width, metrics.horizontalAdvance(entry.overlay ? entry.name + QStringLiteral(" (overlay)") : entry.name));
        const double swatch = 18;
        const QRectF box(frame.right() - width - swatch - 18, frame.top() + 6, width + swatch + 12,
                         lineHeight * static_cast<double>(plot.legend.size()) + 6);
        painter.setPen(QPen(QColor(0xa0, 0xa0, 0xa0), 0.5));
        painter.setBrush(QColor(255, 255, 255, 230));
        painter.drawRect(box);
        double rowTop = box.top() + 3;
        for (const auto& entry : plot.legend) {
            const double middle = rowTop + lineHeight / 2;
            if (entry.marker) {
                painter.setPen(Qt::NoPen);
                painter.setBrush(printColor(entry.color));
                painter.drawEllipse(QPointF(box.left() + 4 + (swatch - 4) / 2, middle), 3.0, 3.0);
                painter.setBrush(Qt::NoBrush);
            } else {
                QPen pen(printColor(entry.color), 1.6);
                if (entry.overlay) pen.setStyle(Qt::DashLine);
                painter.setPen(pen);
                painter.drawLine(QPointF(box.left() + 4, middle), QPointF(box.left() + 4 + swatch - 4, middle));
            }
            painter.setPen(QColor(0x20, 0x20, 0x20));
            painter.drawText(QRectF(box.left() + swatch + 4, rowTop, width + 4, lineHeight), Qt::AlignLeft | Qt::AlignVCenter,
                             entry.overlay ? entry.name + QStringLiteral(" (overlay)") : entry.name);
            rowTop += lineHeight;
        }
    }
    painter.restore();
}

} // namespace s7studio::viewers
