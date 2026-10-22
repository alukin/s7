// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-S7-plugin-permission

#pragma once

#include <QColor>
#include <QList>
#include <QObject>
#include <QString>
#include <QVariantList>
#include <QWidget>

class QLineSeries;
class QQuickWidget;

namespace s7studio::viewers {

class LinePlotView;

// What LinePlot.qml binds to: axis ranges and titles, the legend, and the
// series to show. Wheel and drag gestures come back as zoom()/pan(), so the
// view state lives in C++ and works (and is testable) with or without QML.
class LinePlotBridge : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool inspectionEnabled MEMBER inspectionEnabled NOTIFY inspectionChanged)
    Q_PROPERTY(bool cursorVisible MEMBER cursorVisible NOTIFY inspectionChanged)
    Q_PROPERTY(double cursorX MEMBER cursorX NOTIFY inspectionChanged)
    Q_PROPERTY(double xMin READ xMin NOTIFY xRangeChanged)
    Q_PROPERTY(double xMax READ xMax NOTIFY xRangeChanged)
    Q_PROPERTY(double yMin READ yMin NOTIFY yRangeChanged)
    Q_PROPERTY(double yMax READ yMax NOTIFY yRangeChanged)
    Q_PROPERTY(QString xTitle READ xTitle NOTIFY titlesChanged)
    Q_PROPERTY(QString yTitle READ yTitle NOTIFY titlesChanged)
    Q_PROPERTY(bool legendVisible READ legendVisible NOTIFY legendChanged)
    Q_PROPERTY(QVariantList legend READ legend NOTIFY legendChanged)

public:
    explicit LinePlotBridge(LinePlotView& view);

    [[nodiscard]] auto xMin() const -> double;
    [[nodiscard]] auto xMax() const -> double;
    [[nodiscard]] auto yMin() const -> double;
    [[nodiscard]] auto yMax() const -> double;
    [[nodiscard]] auto xTitle() const -> QString;
    [[nodiscard]] auto yTitle() const -> QString;
    [[nodiscard]] auto legendVisible() const -> bool;
    [[nodiscard]] auto legend() const -> QVariantList;

    bool inspectionEnabled = false;
    bool cursorVisible = false;
    double cursorX = 0;
    Q_INVOKABLE void hover(double fraction, bool inside);
    Q_INVOKABLE QList<QObject*> seriesObjects() const;
    // fx, fy: the anchor as a fraction of the plot area from its left/bottom
    // edge; factor < 1 zooms in. Both axes, like the wheel in QCustomPlot.
    Q_INVOKABLE void zoom(double fx, double fy, double factor);
    // Moves the view by a fraction of the visible span (positive: right/up).
    Q_INVOKABLE void pan(double dxFraction, double dyFraction);

signals:
    void inspectionChanged();
    void xRangeChanged();
    void yRangeChanged();
    void titlesChanged();
    void legendChanged();
    void seriesAdded(QObject* series);
    void seriesRemoved(QObject* series);

private:
    LinePlotView& view_;
};

// A 2D line plot drawn with Qt Graphs (GraphsView in a QQuickWidget). Series
// connect their points in insertion order, so a series may be a time trace or
// a parametric trajectory. The QML scene is created when the view is first
// shown; series data and axis ranges are plain C++ state before that.
class LinePlotView : public QWidget
{
    Q_OBJECT

public:
    struct SeriesStyle {
        QColor color;
        double width = 1.6;
        // Comparison overlays: Qt Graphs line series have no dash patterns,
        // so an overlay is drawn translucent and marked in the legend.
        bool overlay = false;
    };

    explicit LinePlotView(QWidget* parent = nullptr);
    ~LinePlotView() override;

    auto addSeries(const QString& name, const SeriesStyle& style) -> QLineSeries*;
    void removeSeries(QLineSeries* series);
    void clearSeries();
    [[nodiscard]] auto series() const -> const QList<QLineSeries*>& { return series_; }
    [[nodiscard]] auto isOverlay(const QLineSeries* series) const -> bool;
    void setSeriesName(QLineSeries* series, const QString& name);
    // Replaces the series' points and refreshes the legend if needed.
    static void setPoints(QLineSeries* series, const QList<QPointF>& points);

    void setAxisTitles(const QString& xTitle, const QString& yTitle);
    [[nodiscard]] auto xTitle() const -> QString { return xTitle_; }
    [[nodiscard]] auto yTitle() const -> QString { return yTitle_; }
    void setLegendVisible(bool visible);
    [[nodiscard]] auto legendVisible() const -> bool { return legendVisible_; }

    // An empty or inverted range is widened so the axis stays usable.
    void setXRange(double lower, double upper);
    void setYRange(double lower, double upper);
    [[nodiscard]] auto xMin() const -> double { return xMin_; }
    [[nodiscard]] auto xMax() const -> double { return xMax_; }
    [[nodiscard]] auto yMin() const -> double { return yMin_; }
    [[nodiscard]] auto yMax() const -> double { return yMax_; }
    // Fits the Y axis to the points of every series with x in [xMin, xMax].
    // Returns false (and keeps the range) when no point is in that window.
    auto fitYToVisibleData() -> bool;
    // Fits both axes to every point of every series.
    auto fitAll() -> bool;

    [[nodiscard]] auto bridge() -> LinePlotBridge* { return bridge_; }
    [[nodiscard]] auto quickWidget() const -> QQuickWidget* { return quick_; }

signals:
    // Emitted for every change of the X range, by the program or the user.
    void hovered(double time, bool active);
    void navigated();
    void xRangeChanged(double lower, double upper);
    void yRangeChanged(double lower, double upper);

protected:
    void showEvent(QShowEvent* event) override;

private:
    friend class LinePlotBridge;
    void ensureQuickScene();
    void emitLegendChanged();
    void retire(QLineSeries* series);

    LinePlotBridge* bridge_ = nullptr;
    QQuickWidget* quick_ = nullptr;
    QList<QLineSeries*> series_;
    QList<QLineSeries*> overlays_;
    // Removed series, emptied: GraphsView cleans removed series up on its
    // next polish, so they must outlive the scene (deleted after it).
    QList<QLineSeries*> retired_;
    QString xTitle_;
    QString yTitle_;
    bool legendVisible_ = false;
    double xMin_ = 0.0;
    double xMax_ = 1.0;
    double yMin_ = 0.0;
    double yMax_ = 1.0;
};

} // namespace s7studio::viewers
