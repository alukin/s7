// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-S7-plugin-permission

#pragma once

#include "viewers/observation_viewer.hpp"
#include "observation/step_aligner.hpp"
#include "viewers/trajectory_plot.hpp"

#include <QList>
#include <QObject>
#include <QVariantList>
#include <QVector3D>
#include <QWidget>

#include <cstdint>

class QCheckBox;
class QLabel;
class QPushButton;
class QQuickWidget;
class QTimer;

namespace s7studio::ui {
class Throttle;

class PhaseTrajectory3DViewWindow;

// A 3D phase trajectory: three probes (X, Y, Z) of the same runtime step form
// one point, and consecutive points are joined into a continuous line drawn
// with Qt Quick 3D. Drag to rotate, wheel to zoom; the preset buttons show
// the orthogonal XY, XZ and YZ projections.
class PhaseTrajectory3DViewWindow : public QWidget, public viewers::ObservationViewer
{
    Q_OBJECT

public:
    PhaseTrajectory3DViewWindow(QString xProbeName,
                                QString yProbeName,
                                QString zProbeName,
                                QWidget* parent = nullptr,
                                Qt::WindowFlags flags = Qt::WindowFlags());
    ~PhaseTrajectory3DViewWindow() override;

    // Registry entry: "PhaseTrajectory3D", inputs x, y and z.
    static auto viewerType() -> viewers::ViewerType;

    [[nodiscard]] auto xProbeName() const -> QString { return xProbeName_; }
    [[nodiscard]] auto yProbeName() const -> QString { return yProbeName_; }
    [[nodiscard]] auto zProbeName() const -> QString { return zProbeName_; }

    void appendXSample(std::int64_t step, double value);
    void appendYSample(std::int64_t step, double value);
    void appendZSample(std::int64_t step, double value);
    void clearSamples() override;
    void setDroppedSampleCount(std::uint64_t dropped) override;

    // ObservationViewer
    [[nodiscard]] auto widget() -> QWidget* override { return this; }
protected:
    void appendObservedSample(const viewers::ObservedSample& sample) override;
public:

    // Every recorded (x, y, z) triple in step order; export writes these.
    [[nodiscard]] auto points() const -> const QList<viewers::TrajectoryPoint>& { return points_; }
    // The data box the drawn trajectory is scaled into.
    [[nodiscard]] auto bounds() const -> const viewers::TrajectoryBounds& { return bounds_; }
    [[nodiscard]] auto bridge() -> Trajectory3DBridge* { return bridge_; }

    enum class ViewPreset { Perspective, XY, XZ, YZ };
    void setViewPreset(ViewPreset preset);

    static constexpr int kMaxPoints = 2'000'000;
    // At most this many vertices are uploaded; see TrajectoryGeometry.
    static constexpr int kMaxDrawnPoints = 500'000;

protected:

private slots:
    void clearBuffers();
    void exportCsv();
    void refreshPlots();

private:
    void addCoordinate(std::int64_t step, double value, std::size_t axis);
    void updateStatus();
    void updateLabels();

    QString xUnit_, yUnit_, zUnit_;
    QString xProbeName_;
    QString yProbeName_;
    QString zProbeName_;
    observation::StepAligner<3> aligner_;
    QList<viewers::TrajectoryPoint> points_;
    viewers::TrajectoryBounds bounds_;
    std::uint64_t droppedSamples_ = 0;
    bool dirty_ = false;

    Trajectory3DBridge* bridge_ = nullptr;
    TrajectoryPlotView* presentation_ = nullptr;
    QCheckBox* autoScaleCheck_ = nullptr;
    QPushButton* clearButton_ = nullptr;
    QPushButton* exportButton_ = nullptr;
    QLabel* statusLabel_ = nullptr;
    Throttle* statusRefresh_ = nullptr;  // data-driven status updates at 10 Hz
    QTimer* repaintTimer_ = nullptr;
};

} // namespace s7studio::ui
