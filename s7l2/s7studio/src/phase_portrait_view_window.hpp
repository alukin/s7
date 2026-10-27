// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-S7-plugin-permission

#pragma once

#include "viewers/observation_viewer.hpp"
#include "observation/step_aligner.hpp"

#include <QList>
#include <QPointF>

#include <cstdint>
#include <Qt>
#include <QWidget>

class QCheckBox;
class QLabel;
class QLineSeries;
class QPushButton;
class QTimer;

namespace s7studio::viewers {
class LinePlotView;
}

namespace s7studio::ui {

// Two probes plotted against each other (VISUAL_NUMERICAL_TESTS_PLAN.md
// section 3.2: position vs. velocity, an I/Q constellation, ...), the X-Y
// scope mode of an analog computer (section 1.0). Samples for the two probes
// arrive independently through appendXSample()/appendYSample(); a point is
// plotted only once both probes have a sample for the same step (they are
// always sampled together, once per runtime step, so this always catches up).
class PhasePortraitViewWindow : public QWidget, public viewers::ObservationViewer
{
    Q_OBJECT

public:
    PhasePortraitViewWindow(QString xProbeName,
                            QString yProbeName,
                            QWidget* parent = nullptr,
                            Qt::WindowFlags flags = Qt::WindowFlags());

    // Registry entry: "PhasePortrait", inputs x and y.
    static auto viewerType() -> viewers::ViewerType;

    [[nodiscard]] auto xProbeName() const -> QString { return xProbeName_; }
    [[nodiscard]] auto yProbeName() const -> QString { return yProbeName_; }

    void appendXSample(std::int64_t step, double value);
    void appendYSample(std::int64_t step, double value);
    void clearSamples() override;
    void setDroppedSampleCount(std::uint64_t dropped) override;

    // ObservationViewer
    [[nodiscard]] auto widget() -> QWidget* override { return this; }
protected:
    void appendObservedSample(const viewers::ObservedSample& sample) override;
public:

    // Every recorded (x, y) pair in step order; export writes these.
    [[nodiscard]] auto points() const -> const QList<QPointF>& { return points_; }
    [[nodiscard]] auto plotView() const -> viewers::LinePlotView* { return plot_; }
    [[nodiscard]] auto trajectorySeries() const -> QLineSeries* { return curve_; }

    // The trajectory keeps at most this many points, then drops the oldest.
    static constexpr int kMaxPoints = 2'000'000;
    // Upper bound for resolution-dependent drawing. Export keeps every point.
    static constexpr int kMaxDrawnPoints = 16'384;

private slots:
    void clearBuffers();
    void exportRecordedCsv();
    void refreshPlot();

private:
    void updateStatus();
    void addCoordinate(std::int64_t step, std::size_t axis, double value);

    QString xUnit_, yUnit_, zUnit_;
    QString xProbeName_;
    QString yProbeName_;
    std::uint64_t droppedSamples_ = 0;
    bool dirty_ = false;
    qsizetype lastDrawBudget_ = 0;

    observation::StepAligner<2> aligner_;
    QList<QPointF> points_;

    viewers::LinePlotView* plot_ = nullptr;
    QLineSeries* curve_ = nullptr;
    QCheckBox* autoScaleCheck_ = nullptr;
    QPushButton* clearButton_ = nullptr;
    QPushButton* exportButton_ = nullptr;
    QLabel* statusLabel_ = nullptr;
    QTimer* repaintTimer_ = nullptr;
};

} // namespace s7studio::ui
