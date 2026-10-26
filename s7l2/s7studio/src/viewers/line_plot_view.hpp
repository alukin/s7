// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-S7-plugin-permission

#pragma once

#include <QColor>
#include <QList>
#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVariantMap>
#include <QWidget>
#include <QImage>
#include <QRectF>
#include <QSizeF>
#include <QSet>
#include <cstdint>

class QLineSeries;
class QQuickWidget;
class QXYSeries;
class QScatterSeries;

namespace s7studio::viewers {

class LinePlotView;
class DensityProvider;

// What LinePlot.qml binds to: axis ranges and titles, the legend, and the
// series to show. Wheel and drag gestures come back as zoom()/pan(), so the
// view state lives in C++ and works (and is testable) with or without QML.
class LinePlotBridge : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool inspectionEnabled MEMBER inspectionEnabled NOTIFY inspectionChanged)
    Q_PROPERTY(bool triggerVisible MEMBER triggerVisible NOTIFY inspectionChanged)
    Q_PROPERTY(bool cursorVisible MEMBER cursorVisible NOTIFY inspectionChanged)
    Q_PROPERTY(double cursorX MEMBER cursorX NOTIFY inspectionChanged)
    Q_PROPERTY(bool densityVisible MEMBER densityVisible NOTIFY densityChanged)
    Q_PROPERTY(QString densitySource MEMBER densitySource NOTIFY densityChanged)
    Q_PROPERTY(QRectF densityBounds MEMBER densityBounds NOTIFY densityChanged)
    Q_PROPERTY(QVariantList selectionMarkers MEMBER selectionMarkers NOTIFY selectionChanged)
    // Pixel size of the plot area, written by QML, so owners can measure
    // pointer distances in pixels rather than axis units.
    Q_PROPERTY(QSizeF plotAreaSize MEMBER plotAreaSize NOTIFY plotAreaChanged)
    // Pointer tooltip rows ({text, color?}) and on-curve markers ({x, y, color})
    // in data coordinates. Empty rows hide the tooltip.
    Q_PROPERTY(QVariantList hoverTip MEMBER hoverTip NOTIFY hoverTipChanged)
    Q_PROPERTY(QVariantList hoverMarkers MEMBER hoverMarkers NOTIFY hoverTipChanged)
    Q_PROPERTY(double xMin READ xMin NOTIFY xRangeChanged)
    Q_PROPERTY(double xMax READ xMax NOTIFY xRangeChanged)
    Q_PROPERTY(double yMin READ yMin NOTIFY yRangeChanged)
    Q_PROPERTY(double yMax READ yMax NOTIFY yRangeChanged)
    Q_PROPERTY(QString xTitle READ xTitle NOTIFY titlesChanged)
    Q_PROPERTY(QString yTitle READ yTitle NOTIFY titlesChanged)
    Q_PROPERTY(bool legendVisible READ legendVisible NOTIFY legendChanged)
    Q_PROPERTY(QVariantList legend READ legend NOTIFY legendChanged)
    Q_PROPERTY(bool timeDivisions MEMBER timeDivisions NOTIFY legendChanged)
    Q_PROPERTY(int verticalDivisions MEMBER verticalDivisions NOTIFY legendChanged)
    Q_PROPERTY(int selectedLegend MEMBER selectedLegend NOTIFY legendChanged)
    Q_PROPERTY(bool channelAxis MEMBER channelAxis NOTIFY channelAxisChanged)
    Q_PROPERTY(double channelGain MEMBER channelGain NOTIFY channelAxisChanged)
    Q_PROPERTY(double channelOffset MEMBER channelOffset NOTIFY channelAxisChanged)
    Q_PROPERTY(QColor channelAxisColor MEMBER channelAxisColor NOTIFY channelAxisChanged)
    Q_PROPERTY(bool compactAxes MEMBER compactAxes NOTIFY legendChanged)
    Q_PROPERTY(bool instrumentTheme MEMBER instrumentTheme NOTIFY legendChanged)
    Q_PROPERTY(bool checkableLegend MEMBER checkableLegend NOTIFY legendChanged)
    // Empty on ordinary plots. Live Time owns these presentation/edit handles.
    Q_PROPERTY(QVariantList channelZeroMarkers MEMBER channelZeroMarkers NOTIFY instrumentMarkersChanged)
    Q_PROPERTY(QVariantMap instrumentTrigger MEMBER instrumentTrigger NOTIFY instrumentMarkersChanged)
    Q_PROPERTY(bool markerFocus MEMBER markerFocus NOTIFY markerFocusChanged)

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
    bool checkableLegend = false;
    bool instrumentTheme = false;
    bool compactAxes = false, timeDivisions = false;
    int verticalDivisions = 0, selectedLegend = -1;
    bool channelAxis = false;
    double channelGain = 1, channelOffset = 0;
    QColor channelAxisColor;
    // Time instruments navigate horizontally; XY/other plots retain both axes.
    bool horizontalNavigation = false;
    bool triggerVisible = false;
    bool cursorVisible = false;
    double cursorX = 0;
    bool densityVisible = false;
    QString densitySource;
    QRectF densityBounds;
    QVariantList selectionMarkers;
    QSizeF plotAreaSize;
    QVariantList hoverTip, hoverMarkers;
    QVariantList channelZeroMarkers;
    QVariantMap instrumentTrigger;
    bool markerFocus = false;
    Q_INVOKABLE void moveChannelZero(int channel, double displayValue) { emit channelZeroMoved(channel, displayValue); }
    Q_INVOKABLE void moveTriggerLevel(double displayValue) { emit triggerLevelMoved(displayValue); }
    Q_INVOKABLE void moveTriggerPosition(double percent) { emit triggerPositionMoved(percent); }
    Q_INVOKABLE void pick(double xFraction, double yFraction);
    Q_INVOKABLE void hover(double fraction, bool inside);
    Q_INVOKABLE void hoverPoint(double xFraction, double yFraction, bool inside);
    Q_INVOKABLE QList<QObject*> seriesObjects() const;
    // fx, fy: the anchor as a fraction of the plot area from its left/bottom
    // edge; factor < 1 zooms in. horizontalNavigation preserves the Y range.
    Q_INVOKABLE void zoom(double fx, double fy, double factor);
    // Moves the view by a fraction of the visible span (positive: right/up).
    Q_INVOKABLE void pan(double dxFraction, double dyFraction);
    Q_INVOKABLE void toggleLegend(int index, bool checked) { emit legendToggled(index, checked); }
    Q_INVOKABLE void selectLegend(int index) { emit legendSelected(index); }

signals:
    void channelZeroMoved(int channel, double displayValue);
    void triggerLevelMoved(double displayValue);
    void triggerPositionMoved(double percent);
    void instrumentMarkersChanged();
    void markerFocusChanged();
    void legendToggled(int index, bool checked);
    void legendSelected(int index);
    void channelAxisChanged();
    void inspectionChanged();
    void densityChanged();
    void selectionChanged();
    void plotAreaChanged();
    void hoverTipChanged();
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
    auto addScatterSeries(const QString& name, QColor color) -> QScatterSeries*;
    static void setScatterPoints(QScatterSeries* series, const QList<QPointF>& points);
    void setLegendEntryVisible(QXYSeries* series, bool visible);
    void setDensityImage(QImage image, QRectF bounds = {});
    void removeSeries(QLineSeries* series);
    void clearSeries();
    [[nodiscard]] auto series() const -> const QList<QLineSeries*>& { return series_; }
    [[nodiscard]] auto isOverlay(const QLineSeries* series) const -> bool;
    void setSeriesName(QLineSeries* series, const QString& name);
    void setSeriesVisible(QLineSeries* series, bool visible);
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
    void hoveredPoint(double x, double y, bool active);
    void pointPicked(double x, double y);
    void navigated();
    void xRangeChanged(double lower, double upper);
    void yRangeChanged(double lower, double upper);

protected:
    void showEvent(QShowEvent* event) override;

private:
    friend class LinePlotBridge;
    void ensureQuickScene();
    void emitLegendChanged();
    void retire(QXYSeries* series);
    QList<QXYSeries*> allSeries() const;

    LinePlotBridge* bridge_ = nullptr;
    QQuickWidget* quick_ = nullptr;
    QList<QLineSeries*> series_;
    QList<QScatterSeries*> scatterSeries_;
    QSet<QXYSeries*> hiddenLegend_;
    QImage densityImage_;
    DensityProvider* densityProvider_ = nullptr; // Owned by the QML engine.
    std::uint64_t densityRevision_ = 0;
    QList<QLineSeries*> overlays_;
    // Removed series, emptied: GraphsView cleans removed series up on its
    // next polish, so they must outlive the scene (deleted after it).
    QList<QXYSeries*> retired_;
    QString xTitle_;
    QString yTitle_;
    bool legendVisible_ = false;
    double xMin_ = 0.0;
    double xMax_ = 1.0;
    double yMin_ = 0.0;
    double yMax_ = 1.0;
};

} // namespace s7studio::viewers
