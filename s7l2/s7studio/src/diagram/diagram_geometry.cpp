// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-S7-plugin-permission

#include "diagram/diagram_geometry.hpp"

#include "diagram/items.hpp"

#include <QLineF>

#include <algorithm>
#include <cmath>
#include <limits>

namespace s7studio::diagram {

auto snap(const QPointF point) -> QPointF
{
    return {std::round(point.x() / kGridStep) * kGridStep, std::round(point.y() / kGridStep) * kGridStep};
}

auto toGrid(const QPointF point) -> QPoint
{
    return {static_cast<int>(std::lround(point.x() / kGridStep)), static_cast<int>(std::lround(point.y() / kGridStep))};
}

auto fromGrid(const QPoint point) -> QPointF
{
    return {static_cast<double>(point.x() * kGridStep), static_cast<double>(point.y() * kGridStep)};
}

bool same(const QPointF a, const QPointF b)
{
    return std::abs(a.x() - b.x()) < 0.5 && std::abs(a.y() - b.y()) < 0.5;
}

bool onSegment(const QPointF point, const QPointF a, const QPointF b)
{
    if (std::abs(a.y() - b.y()) < 0.5) {
        return std::abs(point.y() - a.y()) < 0.5 && point.x() >= std::min(a.x(), b.x()) - 0.5 && point.x() <= std::max(a.x(), b.x()) + 0.5;
    }
    if (std::abs(a.x() - b.x()) < 0.5) {
        return std::abs(point.x() - a.x()) < 0.5 && point.y() >= std::min(a.y(), b.y()) - 0.5 && point.y() <= std::max(a.y(), b.y()) + 0.5;
    }
    return false;
}

bool onRoute(const QPointF point, const QVector<QPointF>& route)
{
    for (int index = 0; index + 1 < route.size(); ++index) {
        if (onSegment(point, route[index], route[index + 1])) {
            return true;
        }
    }
    return false;
}

// The grid point of the route nearest to `point`.
auto nearestOnRoute(const QPointF point, const QVector<QPointF>& route) -> QPointF
{
    QPointF best = route.isEmpty() ? snap(point) : route.front();
    double bestDistance = std::numeric_limits<double>::max();
    for (int index = 0; index + 1 < route.size(); ++index) {
        const QPointF a = route[index];
        const QPointF b = route[index + 1];
        const QPointF d = b - a;
        const double length2 = QPointF::dotProduct(d, d);
        const double t = length2 > 0.0 ? std::clamp(QPointF::dotProduct(point - a, d) / length2, 0.0, 1.0) : 0.0;
        const QPointF candidate = snap(a + t * d);
        if (!onSegment(candidate, a, b)) {
            continue;
        }
        if (const double distance = QLineF(point, candidate).length(); distance < bestDistance) {
            bestDistance = distance;
            best = candidate;
        }
    }
    return best;
}

// How many of the (ordered) user bends the route passes before reaching its
// point `routeIndex`.
auto bendsBefore(const QVector<QPointF>& route, const QVector<QPointF>& bends, const int routeIndex) -> int
{
    int count = 0;
    for (int segment = 0; segment < routeIndex && segment + 1 < route.size(); ++segment) {
        while (count < bends.size() && onSegment(bends[count], route[segment], route[segment + 1]) &&
               !(segment + 1 == routeIndex && same(bends[count], route[segment + 1]))) {
            ++count;
        }
    }
    return count;
}

auto withoutRepeats(const QVector<QPointF>& points) -> QVector<QPointF>
{
    QVector<QPointF> result;
    for (const auto& point : points) {
        if (result.isEmpty() || !same(result.back(), point)) {
            result.append(point);
        }
    }
    return result;
}

} // namespace s7studio::diagram
