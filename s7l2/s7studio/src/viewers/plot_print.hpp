// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-S7-plugin-permission
#pragma once

#include <QColor>
#include <QImage>
#include <QList>
#include <QPointF>
#include <QRectF>
#include <QString>

#include <vector>

class QPainter;

namespace s7studio::viewers {

class LinePlotView;

// An immutable copy of what a LinePlotView shows, for printing (P02, P03,
// P05, P06). The on-screen plot draws with Qt Graphs on the GPU; printing
// redraws the same data, ranges and axis rules as vector graphics on white
// paper. Transient interaction (hover tips, drag handles) is not copied.
struct PlotSnapshot {
    struct Series {
        enum class Kind { Line, Markers, Dots };
        QString name;
        QColor color;
        double width = 1.6;
        bool overlay = false;  // comparison overlay: translucent, marked in the legend
        Kind kind = Kind::Line;
        QList<QPointF> points;
    };
    struct LegendEntry {
        QString name;
        QColor color;
        bool overlay = false;
        bool marker = false;  // a point series: a dot, not a line, in the legend
    };
    QString heading;  // optional caption above the plot
    QString xTitle, yTitle;
    double xMin = 0, xMax = 1, yMin = 0, yMax = 1;
    bool compactAxes = false;
    bool timeDivisions = false;  // oscilloscope graticule: 10 horizontal divisions
    int verticalDivisions = 0;
    bool channelAxis = false;    // Y labels in the selected channel's units
    double channelGain = 1, channelOffset = 0;
    QColor channelAxisColor;
    bool cursorVisible = false;
    double cursorX = 0;
    QImage density;
    QRectF densityBounds;
    std::vector<Series> series;  // visible series, in drawing order
    bool legendVisible = false;
    std::vector<LegendEntry> legend;

    [[nodiscard]] std::size_t pointCount() const;
};

[[nodiscard]] PlotSnapshot snapshotPlot(const LinePlotView& view);

// Paints `plot` into `area` (axes, titles, grid, data, legend).
void paintPlot(QPainter& painter, const QRectF& area, const PlotSnapshot& plot);

// Trace colours on white paper: pale colours (yellow, light green, the
// instrument theme's light blues) are darkened to stay legible.
[[nodiscard]] QColor printColor(const QColor& color);

// Tick positions for an axis, following LinePlot.qml (1-2-2.5-5 steps, fixed
// divisions for the oscilloscope graticule).
[[nodiscard]] std::vector<double> axisTicks(double low, double high, double step, double anchor);
[[nodiscard]] double niceTickStep(double span, int divisions);

} // namespace s7studio::viewers
