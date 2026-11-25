// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-S7-plugin-permission

#include "viewers/trajectory_geometry.hpp"

#include <QByteArray>
#include <QColor>
#include <QThread>

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

TrajectoryData prepareTrajectory(const QList<TrajectoryPoint> &points, const TrajectoryBounds &bounds,
                                 const qsizetype maxVertices, const bool colorByX,
                                 const std::function<bool()> &canceled) {
    TrajectoryData data;
    data.colorByX = colorByX;
    const auto count = points.size();
    if (maxVertices <= 0 || count <= maxVertices) {
        data.vertices.reserve(count);
        for (const auto &point : points) {
            if (canceled && canceled())
                return {};
            data.vertices.push_back(toScene(point, bounds));
        }
    } else {
        // Reduce like the 2D Phase Portrait: per bucket, keep its first and
        // last point, each axis's extremes and its first non-finite point,
        // in their original order. A short excursion stays visible however
        // long the trajectory, unlike every-Nth-point subsampling.
        constexpr qsizetype kept = 9;
        const auto buckets = std::max(qsizetype(1), (maxVertices - 2) / kept);
        const auto stride = (count - 2 + buckets - 1) / buckets;
        data.vertices.reserve(maxVertices);
        data.vertices.push_back(toScene(points.front(), bounds));
        for (qsizetype begin = 1; begin < count - 1; begin += stride) {
            if (canceled && canceled())
                return {};
            const auto end = std::min(begin + stride, count - 1);
            std::array<qsizetype, kept> indices;
            indices.fill(-1);
            indices[0] = begin;
            indices[1] = end - 1;
            for (qsizetype i = begin; i < end; ++i) {
                const auto &point = points[i];
                if (!std::isfinite(point[0]) || !std::isfinite(point[1]) || !std::isfinite(point[2])) {
                    if (indices[8] < 0) indices[8] = i;
                    continue;
                }
                for (std::size_t axis = 0; axis < 3; ++axis) {
                    auto &low = indices[2 + 2 * axis], &high = indices[3 + 2 * axis];
                    if (low < 0 || point[axis] < points[low][axis]) low = i;
                    if (high < 0 || point[axis] > points[high][axis]) high = i;
                }
            }
            std::ranges::sort(indices);
            qsizetype previous = -1;
            for (const auto index : indices) {
                if (index >= 0 && index != previous) data.vertices.push_back(toScene(points[index], bounds));
                if (index >= 0) previous = index;
            }
        }
        data.vertices.push_back(toScene(points.back(), bounds));
    }
    const int components = colorByX ? 7 : 3;
    data.buffer = QByteArray(data.vertices.size() * components * qsizetype(sizeof(float)), Qt::Uninitialized);
    auto *out = reinterpret_cast<float *>(data.buffer.data());
    for (const auto &v : data.vertices) {
        *out++ = v.x();
        *out++ = v.y();
        *out++ = v.z();
        if (colorByX) {
            const auto c = QColor::fromHsvF(
                .66 * std::clamp(1.0 - (double(v.x()) / kTrajectoryHalfExtent + 1) / 2, 0.0, 1.0), .9, .85);
            *out++ = float(c.redF());
            *out++ = float(c.greenF());
            *out++ = float(c.blueF());
            *out++ = 1;
        }
    }
    return data;
}
void TrajectoryGeometry::apply(const TrajectoryData &data) {
    Q_ASSERT(QThread::currentThread() == thread());
    vertices_ = data.vertices;
    clear();
    setStride((data.colorByX ? 7 : 3) * sizeof(float));
    setPrimitiveType(PrimitiveType::LineStrip);
    addAttribute(Attribute::PositionSemantic, 0, Attribute::F32Type);
    if (data.colorByX)
        addAttribute(Attribute::ColorSemantic, 3 * sizeof(float), Attribute::F32Type);
    setVertexData(data.buffer);
    setBounds({-100, -100, -100}, {100, 100, 100});
    update();
}
void TrajectoryGeometry::setTrajectory(const QList<TrajectoryPoint>& points, const TrajectoryBounds& bounds,
                                       const qsizetype maxVertices, const bool colorByX)
{
    apply(prepareTrajectory(points, bounds, maxVertices, colorByX));
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
