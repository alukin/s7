// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-S7-plugin-permission

#pragma once

#include <QPoint>
#include <QPointF>
#include <QVector>

namespace s7studio::diagram {

[[nodiscard]] auto snap(QPointF point) -> QPointF;
[[nodiscard]] auto toGrid(QPointF point) -> QPoint;
[[nodiscard]] auto fromGrid(QPoint point) -> QPointF;
[[nodiscard]] bool same(QPointF a, QPointF b);
[[nodiscard]] bool onSegment(QPointF point, QPointF a, QPointF b);
[[nodiscard]] bool onRoute(QPointF point, const QVector<QPointF>& route);

} // namespace s7studio::diagram
