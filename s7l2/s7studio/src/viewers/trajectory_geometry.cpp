// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-S7-plugin-permission

#include "viewers/trajectory_geometry.hpp"

#include <QByteArray>

#include <algorithm>
#include <cmath>
#include <limits>

namespace s7studio::viewers {

namespace {

void upload(QQuick3DGeometry& geometry, const QList<QVector3D>& vertices,
            const QQuick3DGeometry::PrimitiveType primitive)
{
    QByteArray data(static_cast<qsizetype>(vertices.size() * 3 * sizeof(float)), Qt::Uninitialized);
    auto* out = reinterpret_cast<float*>(data.data());
    for (const auto& vertex : vertices) {
        *out++ = vertex.x();
        *out++ = vertex.y();
        *out++ = vertex.z();
    }
    geometry.clear();
    geometry.setStride(3 * sizeof(float));
    geometry.setPrimitiveType(primitive);
    geometry.addAttribute(QQuick3DGeometry::Attribute::PositionSemantic, 0, QQuick3DGeometry::Attribute::F32Type);
    geometry.setVertexData(data);
    const float extent = kTrajectoryHalfExtent;
    geometry.setBounds(QVector3D(-extent, -extent, -extent), QVector3D(extent, extent, extent));
    geometry.update();
}

auto scaled(const double value, const double low, const double high) -> float
{
    return static_cast<float>((2.0 * (value - low) / (high - low) - 1.0) * kTrajectoryHalfExtent);
}

} // namespace

auto TrajectoryBounds::of(const QList<TrajectoryPoint>& points) -> TrajectoryBounds
{
    TrajectoryBounds bounds;
    if (points.isEmpty()) return bounds;
    for (std::size_t axis = 0; axis < 3; ++axis) {
        double low = std::numeric_limits<double>::infinity();
        double high = -low;
        for (const auto& point : points) {
            if (!std::isfinite(point[axis])) continue;
            low = std::min(low, point[axis]);
            high = std::max(high, point[axis]);
        }
        if (!(low <= high)) {
            low = -1.0;
            high = 1.0;
        } else if (high - low <= std::abs(low) * 1e-12 || high == low) {
            const double half = low == 0.0 ? 0.5 : std::abs(low) * 0.05;
            low -= half;
            high += half;
        }
        bounds.min[axis] = low;
        bounds.max[axis] = high;
    }
    return bounds;
}

auto toScene(const TrajectoryPoint& point, const TrajectoryBounds& bounds) -> QVector3D
{
    return {scaled(point[0], bounds.min[0], bounds.max[0]),
            scaled(point[2], bounds.min[2], bounds.max[2]),
            -scaled(point[1], bounds.min[1], bounds.max[1])};
}

TrajectoryGeometry::TrajectoryGeometry(QQuick3DObject* parent)
    : QQuick3DGeometry(parent)
{
    upload(*this, vertices_, PrimitiveType::LineStrip);
}

void TrajectoryGeometry::setTrajectory(const QList<TrajectoryPoint>& points, const TrajectoryBounds& bounds,
                                       const qsizetype maxVertices)
{
    vertices_.clear();
    const qsizetype count = points.size();
    const qsizetype stride = maxVertices > 0 && count > maxVertices ? (count + maxVertices - 1) / maxVertices : 1;
    vertices_.reserve(count / stride + 2);
    for (qsizetype index = 0; index < count; index += stride) vertices_.push_back(toScene(points[index], bounds));
    if (count > 0 && (count - 1) % stride != 0) vertices_.push_back(toScene(points.back(), bounds));
    upload(*this, vertices_, PrimitiveType::LineStrip);
}

TrajectoryFrameGeometry::TrajectoryFrameGeometry(QQuick3DObject* parent)
    : QQuick3DGeometry(parent)
{
    const float e = kTrajectoryHalfExtent;
    QList<QVector3D> lines;
    // The 12 cube edges.
    for (const float a : {-e, e}) {
        for (const float b : {-e, e}) {
            lines << QVector3D(-e, a, b) << QVector3D(e, a, b);
            lines << QVector3D(a, -e, b) << QVector3D(a, e, b);
            lines << QVector3D(a, b, -e) << QVector3D(a, b, e);
        }
    }
    // Ticks where the axis labels are (kTrajectoryTickFractions), along the
    // three labelled edges.
    const float tick = e * 0.04f;
    for (const double fraction : kTrajectoryTickFractions) {
        const auto t = static_cast<float>(-e + 2.0 * fraction * e);
        lines << QVector3D(t, -e, e) << QVector3D(t, -e - tick, e + tick);        // data X, front bottom
        lines << QVector3D(e, -e, -t) << QVector3D(e + tick, -e - tick, -t);      // data Y, right bottom
        lines << QVector3D(-e, t, e) << QVector3D(-e - tick, t, e + tick);        // data Z, front left
    }
    upload(*this, lines, PrimitiveType::Lines);
}

} // namespace s7studio::viewers
