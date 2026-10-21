#pragma once

#include "viewers/observation_viewer.hpp"
#include "observation/step_aligner.hpp"
#include "viewers/trajectory_geometry.hpp"

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

class PhaseTrajectory3DViewWindow;

// What Trajectory3D.qml binds to: the geometries, axis labels and the camera
// preset. The trajectory and its frame are C++-owned Qt Quick 3D geometries.
class Trajectory3DBridge : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QObject* trajectory READ trajectory CONSTANT)
    Q_PROPERTY(QObject* frame READ frame CONSTANT)
    Q_PROPERTY(QVariantList labels READ labels NOTIFY labelsChanged)
    Q_PROPERTY(QVector3D head READ head NOTIFY headChanged)
    Q_PROPERTY(bool hasHead READ hasHead NOTIFY headChanged)
    Q_PROPERTY(QVector3D viewRotation READ viewRotation NOTIFY viewRotationChanged)

public:
    explicit Trajectory3DBridge(QObject* parent);

    [[nodiscard]] auto trajectory() -> QObject* { return &trajectory_; }
    [[nodiscard]] auto frame() -> QObject* { return &frame_; }
    [[nodiscard]] auto trajectoryGeometry() -> viewers::TrajectoryGeometry& { return trajectory_; }
    [[nodiscard]] auto labels() const -> QVariantList { return labels_; }
    [[nodiscard]] auto head() const -> QVector3D { return head_; }
    [[nodiscard]] auto hasHead() const -> bool { return hasHead_; }
    [[nodiscard]] auto viewRotation() const -> QVector3D { return viewRotation_; }

    void setLabels(QVariantList labels);
    void setHead(bool hasHead, QVector3D head);
    // Camera orbit as Euler angles (degrees); see viewPreset in the window.
    void setViewRotation(QVector3D rotation);

signals:
    void labelsChanged();
    void headChanged();
    void viewRotationChanged();

private:
    viewers::TrajectoryGeometry trajectory_;
    viewers::TrajectoryFrameGeometry frame_;
    QVariantList labels_;
    QVector3D head_;
    bool hasHead_ = false;
    QVector3D viewRotation_;
};

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
    void showEvent(QShowEvent* event) override;

private slots:
    void clearBuffers();
    void exportCsv();
    void refreshPlots();

private:
    void addCoordinate(std::int64_t step, double value, std::size_t axis);
    void updateStatus();
    void updateLabels();

    QString xProbeName_;
    QString yProbeName_;
    QString zProbeName_;
    observation::StepAligner<3> aligner_;
    QList<viewers::TrajectoryPoint> points_;
    viewers::TrajectoryBounds bounds_;
    std::uint64_t droppedSamples_ = 0;
    bool dirty_ = false;

    Trajectory3DBridge* bridge_ = nullptr;
    QQuickWidget* quick_ = nullptr;
    QCheckBox* autoScaleCheck_ = nullptr;
    QPushButton* clearButton_ = nullptr;
    QPushButton* exportButton_ = nullptr;
    QLabel* statusLabel_ = nullptr;
    QTimer* repaintTimer_ = nullptr;
};

} // namespace s7studio::ui
