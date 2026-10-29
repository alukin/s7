// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-S7-plugin-permission

#pragma once

#include "diagram/items.hpp"

#include <QRectF>

#include <functional>
#include <memory>
#include <optional>
#include <vector>

namespace s7studio::diagram {

// Routes the current wire trees around the supplied node rectangles. The
// scene retains ownership of graphics attachments and calls `attached` after
// each route update, before dependent branches are routed.
class DiagramRouting final
{
public:
    using Attached = std::function<void(SignalWire&)>;

    DiagramRouting(std::vector<std::shared_ptr<SignalWire>> wires, std::vector<QRectF> nodeRects);

    [[nodiscard]] static auto anchorOf(const WireEnd& end) -> std::optional<QPointF>;
    [[nodiscard]] bool routeIsValid(const SignalWire& wire, bool checkObstacles = true) const;
    // A single routing pass for a live bend preview; repairRoutes also settles
    // any other wires invalidated by the result.
    void route(std::vector<SignalWire*> forced, const Attached& attached,
               const std::function<void()>& progress = {}) const;
    void repairRoutes(std::vector<SignalWire*> forced, const Attached& attached,
                      const std::function<void()>& progress = {}) const;
    [[nodiscard]] auto previewRoute(const WireEnd& start, const QVector<QPointF>& bends, QPointF end,
                                    const WireEnd* target) const -> QVector<QPointF>;

private:
    [[nodiscard]] auto placeJunction(const SignalWire& branch) const -> std::optional<QPointF>;

    std::vector<std::shared_ptr<SignalWire>> wires_;
    std::vector<QRectF> nodeRects_;
};

} // namespace s7studio::diagram
