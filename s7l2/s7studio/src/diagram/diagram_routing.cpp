// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-S7-plugin-permission

#include "diagram/diagram_routing.hpp"

#include "diagram/diagram_geometry.hpp"
#include "diagram/net_model.hpp"
#include "diagram/wire_router.hpp"

#include <QElapsedTimer>
#include <QLoggingCategory>

#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace s7studio::diagram {

namespace {

Q_LOGGING_CATEGORY(lcDiagramRouting, "s7studio.diagram.performance")

constexpr int kRouteMargin = 12; // grid steps around the content the router may use

} // namespace

DiagramRouting::DiagramRouting(std::vector<std::shared_ptr<SignalWire>> wires, std::vector<QRectF> nodeRects)
    : wires_(std::move(wires)), nodeRects_(std::move(nodeRects))
{
}

auto DiagramRouting::anchorOf(const WireEnd& end) -> std::optional<QPointF>
{
    if (end.isPort()) {
        if (const auto port = end.port()) {
            return snap(port->scenePos());
        }
        return std::nullopt;
    }
    if (end.isJunction()) {
        return end.at;
    }
    return std::nullopt;
}

auto DiagramRouting::placeJunction(const SignalWire& branch) const -> std::optional<QPointF>
{
    const auto trunk = branch.upstream.wire.lock();
    if (!trunk) {
        return std::nullopt;
    }
    const auto route = trunk->pointsAbsolute();
    if (route.size() < 2) {
        return std::nullopt;
    }
    const QPointF at = branch.upstream.at;
    if (onRoute(at, route) && !same(at, route.front()) && !same(at, route.back())) {
        return snap(at);
    }
    // The point of the trunk nearest to where the branch goes next.
    const auto target = branch.userBends.isEmpty() ? anchorOf(branch.downstream) : std::optional<QPointF>(branch.userBends.front());
    const QPointF goal = target.value_or(route[route.size() / 2]);
    std::optional<QPointF> best;
    double bestDistance = std::numeric_limits<double>::max();
    for (int index = 0; index + 1 < route.size(); ++index) {
        const QPointF a = snap(route[index]);
        const QPointF b = snap(route[index + 1]);
        const QPointF step = QPointF(b.x() > a.x() ? kGridStep : (b.x() < a.x() ? -kGridStep : 0),
                                     b.y() > a.y() ? kGridStep : (b.y() < a.y() ? -kGridStep : 0));
        if (step.isNull()) {
            continue;
        }
        for (QPointF point = a;; point += step) {
            const bool end = same(point, route.front()) || same(point, route.back());
            if (!end) {
                const double distance = std::abs(point.x() - goal.x()) + std::abs(point.y() - goal.y());
                if (distance < bestDistance) {
                    bestDistance = distance;
                    best = point;
                }
            }
            if (same(point, b)) {
                break;
            }
        }
    }
    return best ? best : std::optional<QPointF>(route.front());
}

bool DiagramRouting::routeIsValid(const SignalWire& wire, const bool checkObstacles) const
{
    const auto route = wire.pointsAbsolute();
    const auto invalid = [&](const char* reason) {
        if (lcDiagramRouting().isDebugEnabled()) {
            const auto endName = [](const WireEnd& end) {
                if (const auto node = end.node.lock()) {
                    return QString::fromStdString(node->id());
                }
                return end.isJunction() ? QStringLiteral("junction") : QStringLiteral("missing");
            };
            qCDebug(lcDiagramRouting) << "invalid route:" << reason << endName(wire.upstream) << "->"
                                      << endName(wire.downstream) << "points=" << route.size();
        }
        return false;
    };
    if (route.isEmpty()) {
        return invalid("empty");
    }
    const auto start = anchorOf(wire.upstream);
    const auto end = anchorOf(wire.downstream);
    if (!start || !end || !same(route.front(), *start) || !same(route.back(), *end)) {
        return invalid("endpoints");
    }
    // Coincident port endpoints are represented by one point. Accept that
    // degenerate route so loading and repair do not repeatedly try to route
    // a path whose start and goal are identical.
    if (route.size() == 1) {
        if (!wire.upstream.isJunction() && wire.userBends.isEmpty() && same(*start, *end)) {
            return true;
        }
        return invalid("unsupported-single-point");
    }
    if (wire.upstream.isJunction()) {
        const auto trunk = wire.upstream.wire.lock();
        const auto trunkRoute = trunk ? trunk->pointsAbsolute() : QVector<QPointF>{};
        if (trunkRoute.size() < 2 || !onRoute(*start, trunkRoute) || same(*start, trunkRoute.front()) || same(*start, trunkRoute.back())) {
            return invalid("junction");
        }
    }
    for (int index = 0; index + 1 < route.size(); ++index) {
        const QPointF a = route[index];
        const QPointF b = route[index + 1];
        if (std::abs(a.x() - b.x()) > 0.5 && std::abs(a.y() - b.y()) > 0.5) {
            return invalid("diagonal");
        }
        if (checkObstacles) {
            const QRectF segment = QRectF(a, b).normalized();
            for (const auto& rect : nodeRects_) {
                const QRectF inner = rect.adjusted(1.0, 1.0, -1.0, -1.0);
                if (segment.right() >= inner.left() && segment.left() <= inner.right() && segment.bottom() >= inner.top() &&
                    segment.top() <= inner.bottom()) {
                    if (lcDiagramRouting().isDebugEnabled()) {
                        qCDebug(lcDiagramRouting) << "route obstacle detail: segment=" << a << b << "node=" << inner;
                    }
                    return invalid("obstacle");
                }
            }
        }
    }
    for (const auto& bend : wire.userBends) {
        if (!onRoute(bend, route)) {
            return invalid("bend");
        }
    }
    return true;
}

void DiagramRouting::route(std::vector<SignalWire*> forced, const Attached& attached, const std::function<void()>& routeProgress) const
{
    if (forced.empty()) {
        return;
    }
    const auto& wires = wires_;
    const auto& rects = nodeRects_;
    QRect bounds;
    auto include = [&](const QPointF point) {
        const QRect cell(toGrid(point), QSize(1, 1));
        bounds = bounds.isNull() ? cell : bounds.united(cell);
    };
    for (const auto& rect : rects) {
        include(rect.topLeft());
        include(rect.bottomRight());
    }
    for (const auto& wire : wires) {
        for (const auto& point : wire->pointsAbsolute()) {
            include(point);
        }
        for (const auto& bend : wire->userBends) {
            include(bend);
        }
        if (wire->upstream.isJunction()) {
            include(wire->upstream.at);
        }
    }
    bounds.adjust(-kRouteMargin, -kRouteMargin, kRouteMargin, kRouteMargin);

    WireRouter router(bounds);
    for (const auto& rect : rects) {
        router.addObstacle(QRect(toGrid(rect.topLeft()), toGrid(rect.bottomRight())));
    }
    std::unordered_map<const SignalWire*, int> nets;
    auto netOf = [&](const SignalWire& wire) {
        const SignalWire* root = NetModel::rootOf(wire);
        const auto [it, inserted] = nets.emplace(root ? root : &wire, static_cast<int>(nets.size()));
        return it->second;
    };
    auto gridPoints = [](const QVector<QPointF>& points) {
        QVector<QPoint> result;
        for (const auto& point : points) {
            result.append(toGrid(point));
        }
        return result;
    };
    std::unordered_set<const SignalWire*> forcedSet(forced.begin(), forced.end());
    for (const auto& wire : wires) {
        if (!forcedSet.contains(wire.get())) {
            router.addWire(gridPoints(wire->pointsAbsolute()), netOf(*wire));
        }
    }
    // Trunks before branches, otherwise in scene order: the routes depend on
    // the order, so it must not depend on where the wires are in memory.
    std::unordered_map<const SignalWire*, std::size_t> sceneOrder;
    for (const auto& wire : wires) {
        sceneOrder.emplace(wire.get(), sceneOrder.size());
    }
    std::erase_if(forced, [&](const SignalWire* wire) { return !sceneOrder.contains(wire); });
    std::sort(forced.begin(), forced.end(), [&](const SignalWire* a, const SignalWire* b) {
        return std::make_pair(NetModel::depthOf(*a), sceneOrder.at(a)) < std::make_pair(NetModel::depthOf(*b), sceneOrder.at(b));
    });
    forced.erase(std::unique(forced.begin(), forced.end()), forced.end());
    for (auto* wire : forced) {
        std::optional<QPointF> start;
        if (wire->upstream.isJunction()) {
            start = placeJunction(*wire);
            if (start) {
                wire->upstream.at = *start;
            }
        } else {
            start = anchorOf(wire->upstream);
        }
        const auto end = anchorOf(wire->downstream);
        if (!start || !end) {
            continue;
        }
        WireRouter::Request request;
        request.anchors.append(toGrid(*start));
        for (const auto& bend : wire->userBends) {
            request.anchors.append(toGrid(bend));
        }
        request.anchors.append(toGrid(*end));
        request.net = netOf(*wire);
        request.leaveEast = wire->upstream.isOutput();
        request.enterEast = wire->downstream.isInput();
        const auto path = router.route(request);
        QVector<QPointF> points;
        for (const auto& point : path) {
            points.append(fromGrid(point));
        }
        wire->setRoute(points);
        if (attached) {
            attached(*wire);
        }
        router.addWire(path, request.net);
        if (routeProgress) {
            routeProgress();
        }
    }
}

void DiagramRouting::repairRoutes(std::vector<SignalWire*> forced, const Attached& attached, const std::function<void()>& routeProgress) const
{
    if (forced.empty()) {
        return;
    }

    QElapsedTimer timer;
    timer.start();
    const auto initiallyForced = forced.size();
    std::size_t checkedRoutes = 0;
    std::size_t reroutedRoutes = forced.size();
    int validationPasses = 0;
    route(std::move(forced), attached, routeProgress);
    // A moved element can invalidate any wire's route, not just those on
    // forced's own nets (e.g. a dragged element now overlapping an unrelated
    // wire), so the settle passes below re-check every wire in the scene.
    std::vector<SignalWire*> candidates;
    for (const auto& wire : wires_) {
        candidates.push_back(wire.get());
    }
    // Rerouted wires may leave others invalid (a junction off its trunk, a
    // wire under a moved element); a few passes settle every tree.
    for (int pass = 0; pass < 4; ++pass) {
        std::vector<SignalWire*> invalid;
        ++validationPasses;
        checkedRoutes += candidates.size();
        for (auto* wire : candidates) {
            if (!routeIsValid(*wire) && anchorOf(wire->downstream) && (wire->upstream.isJunction() || anchorOf(wire->upstream))) {
                invalid.push_back(wire);
            }
        }
        if (invalid.empty()) {
            break;
        }
        std::vector<QVector<QPointF>> before;
        before.reserve(invalid.size());
        for (const auto* wire : invalid) {
            before.push_back(wire->pointsAbsolute());
        }
        reroutedRoutes += invalid.size();
        route(invalid, attached, routeProgress);
        bool changed = false;
        for (std::size_t index = 0; index < invalid.size(); ++index) {
            changed = changed || invalid[index]->pointsAbsolute() != before[index];
        }
        if (!changed) {
            break;
        }
    }
    qCDebug(lcDiagramRouting) << "route repair: forced=" << initiallyForced << "rerouted=" << reroutedRoutes
                              << "validated=" << checkedRoutes << "passes=" << validationPasses
                              << "elapsed_ms=" << timer.elapsed();
}

auto DiagramRouting::previewRoute(const WireEnd& start, const QVector<QPointF>& bends, const QPointF end, const WireEnd* target) const
    -> QVector<QPointF>
{
    const auto startAnchor = anchorOf(start);
    if (!startAnchor) {
        return {};
    }
    // Route from upstream to downstream, as the wire will be.
    const bool reversed = start.isInput() || (target && target->isOutput());
    QVector<QPointF> anchors{*startAnchor};
    anchors += bends;
    anchors.append(end);
    if (reversed) {
        std::reverse(anchors.begin(), anchors.end());
    }
    const WireEnd* upstream = reversed ? target : &start;
    const WireEnd* downstream = reversed ? &start : target;

    QRect bounds;
    auto include = [&](const QPointF point) {
        const QRect cell(toGrid(point), QSize(1, 1));
        bounds = bounds.isNull() ? cell : bounds.united(cell);
    };
    const auto& rects = nodeRects_;
    for (const auto& rect : rects) {
        include(rect.topLeft());
        include(rect.bottomRight());
    }
    for (const auto& anchor : anchors) {
        include(anchor);
    }
    const auto& wires = wires_;
    for (const auto& wire : wires) {
        for (const auto& point : wire->pointsAbsolute()) {
            include(point);
        }
    }
    bounds.adjust(-kRouteMargin, -kRouteMargin, kRouteMargin, kRouteMargin);
    WireRouter router(bounds);
    for (const auto& rect : rects) {
        router.addObstacle(QRect(toGrid(rect.topLeft()), toGrid(rect.bottomRight())));
    }
    const SignalWire* startNet = start.isJunction() ? NetModel::rootOf(*start.wire.lock()) : nullptr;
    std::unordered_map<const SignalWire*, int> nets;
    for (const auto& wire : wires) {
        const SignalWire* root = NetModel::rootOf(*wire);
        const auto [it, inserted] = nets.emplace(root ? root : wire.get(), static_cast<int>(nets.size()) + 1);
        QVector<QPoint> points;
        for (const auto& point : wire->pointsAbsolute()) {
            points.append(toGrid(point));
        }
        router.addWire(points, root && root == startNet ? 0 : it->second);
    }
    WireRouter::Request request;
    for (const auto& anchor : anchors) {
        request.anchors.append(toGrid(anchor));
    }
    request.net = 0;
    request.leaveEast = upstream && upstream->isOutput();
    request.enterEast = downstream && downstream->isInput();
    QVector<QPointF> points;
    for (const auto& point : router.route(request)) {
        points.append(fromGrid(point));
    }
    return points;
}

} // namespace s7studio::diagram
