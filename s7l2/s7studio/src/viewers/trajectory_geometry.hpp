// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-S7-plugin-permission

#pragma once

#include <QList>
#include <QQuick3DGeometry>
#include <QVector3D>

#include <array>

namespace s7studio::viewers {

using TrajectoryPoint = std::array<double, 3>;

// The data box a trajectory is drawn in. Each axis is scaled independently
// into the scene cube [-kHalfExtent, kHalfExtent]^3, so signals of very
// different magnitude stay readable (like independently scaled projections).
struct TrajectoryBounds {
    TrajectoryPoint min{-1.0, -1.0, -1.0};
    TrajectoryPoint max{1.0, 1.0, 1.0};

    // The bounds of points; a degenerate axis is widened around its value.
    static auto of(const QList<TrajectoryPoint>& points) -> TrajectoryBounds;
};

inline constexpr float kTrajectoryHalfExtent = 100.0f;
// Where along each axis the 3D viewer puts ticks and value labels, as a
// fraction of the data range: inside the edge, so labels of different axes
// do not collide at the cube corners.
inline constexpr std::array<double, 3> kTrajectoryTickFractions{0.1, 0.5, 0.9};

// Data axes to scene axes: data X right, data Y into the screen, data Z up
// (Qt Quick 3D's Y). A right-handed data frame viewed from the front.
[[nodiscard]] auto toScene(const TrajectoryPoint& point, const TrajectoryBounds& bounds) -> QVector3D;

// A continuous trajectory as one line strip: consecutive samples are joined
// in time order. At most maxVertices are uploaded; longer trajectories are
// thinned with a uniform stride that always keeps the newest point.
class TrajectoryGeometry : public QQuick3DGeometry
{
    Q_OBJECT

public:
    explicit TrajectoryGeometry(QQuick3DObject* parent = nullptr);

    void setTrajectory(const QList<TrajectoryPoint>& points, const TrajectoryBounds& bounds, qsizetype maxVertices);
    [[nodiscard]] auto vertexCount() const -> qsizetype { return vertices_.size(); }
    [[nodiscard]] auto vertexAt(qsizetype index) const -> QVector3D { return vertices_.at(index); }

private:
    QList<QVector3D> vertices_;
};

// The wireframe of the scene cube plus tick marks: the frame the axis labels
// of the 3D viewer refer to. Constant; it does not depend on data.
class TrajectoryFrameGeometry : public QQuick3DGeometry
{
    Q_OBJECT

public:
    explicit TrajectoryFrameGeometry(QQuick3DObject* parent = nullptr);
};

} // namespace s7studio::viewers
