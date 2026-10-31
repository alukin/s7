// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-S7-plugin-permission

#pragma once

#include <QPoint>
#include <QPointF>
#include <QVector>

namespace s7studio::diagram {

constexpr double kWireHandleHit = 6.0;

[[nodiscard]] auto snap(QPointF point) -> QPointF;
[[nodiscard]] auto toGrid(QPointF point) -> QPoint;
[[nodiscard]] auto fromGrid(QPoint point) -> QPointF;
[[nodiscard]] bool same(QPointF a, QPointF b);
[[nodiscard]] bool onSegment(QPointF point, QPointF a, QPointF b);
[[nodiscard]] bool onRoute(QPointF point, const QVector<QPointF>& route);
[[nodiscard]] auto nearestOnRoute(QPointF point, const QVector<QPointF>& route) -> QPointF;
[[nodiscard]] auto bendsBefore(const QVector<QPointF>& route, const QVector<QPointF>& bends, int routeIndex) -> int;
[[nodiscard]] auto withoutRepeats(const QVector<QPointF>& points) -> QVector<QPointF>;
// Grid points an orthogonal route passes more than once: where it crosses
// itself or runs back along itself. 0 for a clean route.
[[nodiscard]] int revisitedGridPoints(const QVector<QPointF>& route);
// Tight U-turns of a route: a one-step segment between two segments that run
// in opposite directions, the small square hooks a route should not have.
[[nodiscard]] int tightUTurns(const QVector<QPointF>& route);

} // namespace s7studio::diagram
