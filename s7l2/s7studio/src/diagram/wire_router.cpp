#include "diagram/wire_router.hpp"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <limits>
#include <queue>
#include <tuple>

namespace s7studio::diagram {

namespace {

// Costs per unit step (a step costs kStep).
constexpr int kStep = 2;
constexpr int kBend = 5;
constexpr int kCross = 3;        // crossing a wire of another net
constexpr int kNearNode = 1;     // running next to a node
constexpr int kOwnOverlap = 4;   // running along a wire of the same net
constexpr int kForbidden = 400;  // through a node, along or touching another net

constexpr std::array<QPoint, 4> kDelta{QPoint(1, 0), QPoint(-1, 0), QPoint(0, -1), QPoint(0, 1)};

auto opposite(const int dir) -> int
{
    return dir ^ 1; // East<->West, North<->South
}

} // namespace

WireRouter::WireRouter(const QRect bounds)
    : bounds_(bounds)
    , width_(std::max(1, bounds.width()))
    , height_(std::max(1, bounds.height()))
    , node_(static_cast<std::size_t>(width_ * height_), 0)
    , hNet_(static_cast<std::size_t>(width_ * height_), -1)
    , vNet_(static_cast<std::size_t>(width_ * height_), -1)
    , vertexNet_(static_cast<std::size_t>(width_ * height_), -1)
{
}

bool WireRouter::inside(const QPoint point) const
{
    return bounds_.contains(point);
}

int WireRouter::cell(const QPoint point) const
{
    return (point.y() - bounds_.top()) * width_ + (point.x() - bounds_.left());
}

void WireRouter::addObstacle(const QRect& cells)
{
    for (int y = cells.top() - 1; y <= cells.bottom() + 1; ++y) {
        for (int x = cells.left() - 1; x <= cells.right() + 1; ++x) {
            const QPoint point(x, y);
            if (!inside(point)) {
                continue;
            }
            auto& value = node_[static_cast<std::size_t>(cell(point))];
            value = std::max<unsigned char>(value, cells.contains(point) ? 2 : 1);
        }
    }
}

void WireRouter::markSegment(const QPoint from, const QPoint to, const int net)
{
    auto mark = [net](int& slot) { slot = slot == -1 || slot == net ? net : -2; };
    const QPoint step(to.x() > from.x() ? 1 : (to.x() < from.x() ? -1 : 0), to.y() > from.y() ? 1 : (to.y() < from.y() ? -1 : 0));
    if (step.isNull()) {
        return;
    }
    for (QPoint point = from; point != to; point += step) {
        const QPoint next = point + step;
        const QPoint low(std::min(point.x(), next.x()), std::min(point.y(), next.y()));
        if (!inside(low) || !inside(next)) {
            continue;
        }
        mark(step.y() == 0 ? hNet_[static_cast<std::size_t>(cell(low))] : vNet_[static_cast<std::size_t>(cell(low))]);
    }
}

void WireRouter::addWire(const QVector<QPoint>& points, const int net)
{
    for (int index = 0; index < points.size(); ++index) {
        if (inside(points[index])) {
            auto& slot = vertexNet_[static_cast<std::size_t>(cell(points[index]))];
            slot = slot == -1 || slot == net ? net : -2;
        }
        if (index + 1 < points.size()) {
            markSegment(points[index], points[index + 1], net);
        }
    }
}

int WireRouter::horizontalNet(const QPoint point) const
{
    const QPoint left = point - QPoint(1, 0);
    const int here = inside(point) && inside(point + QPoint(1, 0)) ? hNet_[static_cast<std::size_t>(cell(point))] : -1;
    if (here != -1) {
        return here;
    }
    return inside(left) ? hNet_[static_cast<std::size_t>(cell(left))] : -1;
}

int WireRouter::verticalNet(const QPoint point) const
{
    const QPoint up = point - QPoint(0, 1);
    const int here = inside(point) && inside(point + QPoint(0, 1)) ? vNet_[static_cast<std::size_t>(cell(point))] : -1;
    if (here != -1) {
        return here;
    }
    return inside(up) ? vNet_[static_cast<std::size_t>(cell(up))] : -1;
}

auto WireRouter::leg(const QPoint start, const QPoint goal, const Dir startDir, const int net, const bool leaveEast,
                     const bool enterEast, Dir* endDir) const -> QVector<QPoint>
{
    if (start == goal || !inside(start) || !inside(goal)) {
        *endDir = startDir;
        return {start, goal};
    }
    const auto foreign = [net](const int other) { return other != -1 && other != net; };

    const std::size_t states = static_cast<std::size_t>(width_ * height_) * 5;
    std::vector<int> cost(states, std::numeric_limits<int>::max());
    std::vector<int> parent(states, -1);
    using Entry = std::tuple<int, int, int>; // f, g, state
    std::priority_queue<Entry, std::vector<Entry>, std::greater<>> open;
    auto heuristic = [&](const QPoint point) { return (std::abs(point.x() - goal.x()) + std::abs(point.y() - goal.y())) * kStep; };
    auto stateOf = [&](const QPoint point, const int dir) { return cell(point) * 5 + dir; };
    auto pointOf = [&](const int state) {
        const int index = state / 5;
        return QPoint(bounds_.left() + index % width_, bounds_.top() + index / width_);
    };

    const int first = stateOf(start, startDir);
    cost[static_cast<std::size_t>(first)] = 0;
    open.emplace(heuristic(start), 0, first);
    int reached = -1;
    while (!open.empty()) {
        const auto [f, g, state] = open.top();
        open.pop();
        if (g > cost[static_cast<std::size_t>(state)]) {
            continue;
        }
        const QPoint point = pointOf(state);
        const int dir = state % 5;
        if (point == goal) {
            reached = state;
            break;
        }
        for (int next = 0; next < 4; ++next) {
            if (dir != None && next == opposite(dir)) {
                continue;
            }
            if (point == start && leaveEast && next != East) {
                continue;
            }
            const QPoint to = point + kDelta[static_cast<std::size_t>(next)];
            if (!inside(to)) {
                continue;
            }
            if (to == goal && enterEast && next != East) {
                continue;
            }
            int step = kStep;
            const bool horizontal = next == East || next == West;
            if (dir != None && dir != next) {
                step += kBend;
                // Turning on another net's wire would touch it.
                if (point != start && (foreign(horizontalNet(point)) || foreign(verticalNet(point)))) {
                    step += kForbidden;
                }
            }
            const QPoint low(std::min(point.x(), to.x()), std::min(point.y(), to.y()));
            const int along = horizontal ? hNet_[static_cast<std::size_t>(cell(low))] : vNet_[static_cast<std::size_t>(cell(low))];
            if (along == net) {
                step += kOwnOverlap;
            } else if (along != -1) {
                step += kForbidden;
            }
            if (to != goal) {
                const auto nodeCost = node_[static_cast<std::size_t>(cell(to))];
                step += nodeCost == 2 ? kForbidden : (nodeCost == 1 ? kNearNode : 0);
                if (foreign(vertexNet_[static_cast<std::size_t>(cell(to))])) {
                    step += kForbidden;
                }
                if (foreign(horizontal ? verticalNet(to) : horizontalNet(to))) {
                    step += kCross;
                }
            }
            const int toState = stateOf(to, next);
            const int toCost = g + step;
            if (toCost < cost[static_cast<std::size_t>(toState)]) {
                cost[static_cast<std::size_t>(toState)] = toCost;
                parent[static_cast<std::size_t>(toState)] = state;
                open.emplace(toCost + heuristic(to), toCost, toState);
            }
        }
    }

    if (reached < 0) {
        *endDir = startDir;
        return {start, QPoint(goal.x(), start.y()), goal};
    }
    *endDir = static_cast<Dir>(reached % 5);
    QVector<QPoint> path;
    for (int state = reached; state >= 0; state = parent[static_cast<std::size_t>(state)]) {
        path.prepend(pointOf(state));
    }
    return path;
}

auto WireRouter::route(const Request& request) const -> QVector<QPoint>
{
    if (request.anchors.size() < 2) {
        return request.anchors;
    }
    // Earlier legs of the same wire count as its own net for the later ones.
    WireRouter scratch = *this;
    QVector<QPoint> path{request.anchors.front()};
    Dir dir = None;
    for (int index = 0; index + 1 < request.anchors.size(); ++index) {
        const bool firstLeg = index == 0;
        const bool lastLeg = index + 2 == request.anchors.size();
        const auto legPath = scratch.leg(request.anchors[index], request.anchors[index + 1], dir, request.net, firstLeg && request.leaveEast,
                                         lastLeg && request.enterEast, &dir);
        for (int point = 1; point < legPath.size(); ++point) {
            path.append(legPath[point]);
        }
        for (int point = 0; point + 1 < legPath.size(); ++point) {
            scratch.markSegment(legPath[point], legPath[point + 1], request.net);
        }
    }
    // Corners only.
    QVector<QPoint> corners;
    for (const auto& point : path) {
        if (!corners.isEmpty() && corners.back() == point) {
            continue;
        }
        while (corners.size() >= 2) {
            const QPoint a = corners[corners.size() - 2];
            const QPoint b = corners.back();
            const bool collinear = (a.x() == b.x() && b.x() == point.x()) || (a.y() == b.y() && b.y() == point.y());
            if (!collinear) {
                break;
            }
            corners.removeLast();
        }
        corners.append(point);
    }
    return corners;
}

} // namespace s7studio::diagram
