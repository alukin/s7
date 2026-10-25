// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-S7-plugin-permission
#pragma once
#include "viewers/trajectory_geometry.hpp"
#include <QWidget>
#include <QVariantList>
#include <QStringList>
class QQuickWidget;
namespace s7studio::ui {
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

// Reusable plot surface; source, capture, toolbars and window state belong to its host.
class TrajectoryPlotView : public QWidget {
public:
    explicit TrajectoryPlotView(QWidget* parent = nullptr);
    ~TrajectoryPlotView() override;
    Trajectory3DBridge* bridge() const { return bridge_; }
    void setAxisLabels(const viewers::TrajectoryBounds& bounds, const QStringList& labels, bool compact = false);
protected:
    void showEvent(QShowEvent*) override;
private:
    Trajectory3DBridge* bridge_;
    QQuickWidget* quick_ = nullptr;
};
}
