// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-S7-plugin-permission

#include "diagram/diagram_interaction.hpp"

#include "diagram/diagram_geometry.hpp"
#include "diagram/diagram_routing.hpp"
#include "diagram/diagram_scene.hpp"
#include "diagram/wire_context_menu.hpp"

#include <QGraphicsPathItem>
#include <QGraphicsSceneMouseEvent>
#include <QPainterPath>
#include <QPen>
#include <QTransform>

#include <algorithm>
#include <cmath>
#include <unordered_set>

namespace s7studio::diagram {

namespace {

constexpr double kSegmentHit = 4.0; // a press this close to a segment grabs it

auto distanceToSegment(const QPointF point, const QPointF a, const QPointF b) -> double
{
    const QPointF d = b - a;
    const double length2 = QPointF::dotProduct(d, d);
    const double t = length2 > 0.0 ? std::clamp(QPointF::dotProduct(point - a, d) / length2, 0.0, 1.0) : 0.0;
    return QLineF(point, a + t * d).length();
}

} // namespace

struct DiagramInteraction::Gesture {
    enum class Kind { Connect, Draw, Bend };

    Kind kind = Kind::Connect;
    // Connect and Draw: the end the wire starts at, the bends clicked so far.
    WireEnd start;
    QPointF press;
    bool moved = false;
    QVector<QPointF> bends;
    // The last target checked, and why it was refused (empty: accepted).
    std::optional<WireEnd> checkedTarget;
    QString checkedProblem;
    // Bend: the wire, its bends and route when the drag started, and the
    // dragged bend (routeIndex) or segment (routeIndex to routeIndex + 1).
    std::shared_ptr<SignalWire> wire;
    QVector<QPointF> baseBends;
    QVector<QPointF> baseRoute;
    int routeIndex = -1;
    bool segment = false;
};

struct DiagramInteraction::MoveGesture {
    QPointF press;
    std::vector<std::pair<std::shared_ptr<QSchematic::Items::Item>, QPointF>> items;
    std::vector<std::shared_ptr<SignalWire>> incidentWires;
};

DiagramInteraction::DiagramInteraction(DiagramScene& scene) : scene_(scene) {}
DiagramInteraction::~DiagramInteraction() = default;

void DiagramInteraction::clearForSceneReset()
{
    gesture_.reset();
    moveGesture_.reset();
    preview_ = nullptr; // the scene deletes the item when it clears
    mouseDown_ = false;
}

void DiagramInteraction::modeChanged(const int mode)
{
    if (mode != DiagramScene::DrawMode && gesture_ && gesture_->kind == Gesture::Kind::Draw) {
        cancelDrawing();
    }
}

auto DiagramInteraction::portAt(const QPointF scenePos) const -> std::shared_ptr<PortConnector>
{
    for (auto* item : scene_.QGraphicsScene::items(scenePos)) {
        if (auto* port = dynamic_cast<PortConnector*>(item)) {
            if (auto* element = port->element()) {
                return element->port(port->direction(), port->index());
            }
        }
    }
    return nullptr;
}

auto DiagramInteraction::wireAt(const QPointF scenePos) const -> std::shared_ptr<SignalWire>
{
    for (auto* item : scene_.QGraphicsScene::items(scenePos)) {
        if (auto* wire = dynamic_cast<SignalWire*>(item)) {
            const auto route = wire->pointsAbsolute();
            for (int index = 0; index + 1 < route.size(); ++index) {
                if (distanceToSegment(scenePos, route[index], route[index + 1]) <= kSegmentHit + 2.0) {
                    return scene_.sharedWire(wire);
                }
            }
        }
    }
    return nullptr;
}

auto DiagramInteraction::endAt(const QPointF scenePos, const bool wires) const -> std::optional<WireEnd>
{
    if (const auto port = portAt(scenePos)) {
        auto* element = port->element();
        return WireEnd::onPort(element->sharedPtr<ElementNode>(), port->direction(), port->index());
    }
    if (wires) {
        if (const auto wire = wireAt(scenePos)) {
            return WireEnd::onWire(wire, nearestOnRoute(scenePos, wire->pointsAbsolute()));
        }
    }
    return std::nullopt;
}

bool DiagramInteraction::insideElement(const QPointF scenePos) const
{
    for (const auto& rect : scene_.nodeRects()) {
        if (rect.adjusted(1.0, 1.0, -1.0, -1.0).contains(scenePos)) {
            return true;
        }
    }
    return false;
}

void DiagramInteraction::markTargets(const WireEnd* start)
{
    for (const auto& node : scene_.elementNodes()) {
        for (const auto* ports : {&node->inputs(), &node->outputs()}) {
            for (const auto& port : *ports) {
                auto mark = PortConnector::Mark::None;
                if (start) {
                    const auto end = WireEnd::onPort(node, port->direction(), port->index());
                    if (!end.sameAs(*start)) {
                        mark = scene_.checkConnection(*start, end).isEmpty() ? PortConnector::Mark::Target : PortConnector::Mark::Refused;
                    }
                }
                port->setMark(mark);
            }
        }
    }
}

void DiagramInteraction::showPreview(const QVector<QPointF>& points, const bool valid)
{
    if (!preview_) {
        preview_ = new QGraphicsPathItem;
        preview_->setZValue(1000.0);
        scene_.QGraphicsScene::addItem(preview_);
    }
    QPainterPath path;
    if (!points.isEmpty()) {
        path.moveTo(points.front());
        for (int index = 1; index < points.size(); ++index) {
            path.lineTo(points[index]);
        }
    }
    preview_->setPen(QPen(valid ? QColor(0x2e, 0x9d, 0x46) : QColor(0x1e, 0x5b, 0xd8), 1.6, Qt::DashLine));
    preview_->setPath(path);
    preview_->setVisible(true);
}

void DiagramInteraction::hidePreview()
{
    if (preview_) {
        preview_->setVisible(false);
    }
}

auto DiagramInteraction::previewRoute(const WireEnd& start, const QVector<QPointF>& bends, const QPointF end, const WireEnd* target) const
    -> QVector<QPointF>
{
    return DiagramRouting(scene_.signalWires(), scene_.nodeRects()).previewRoute(start, bends, end, target);
}

void DiagramInteraction::startDrawing(const WireEnd& start)
{
    gesture_ = std::make_unique<Gesture>();
    gesture_->kind = Gesture::Kind::Draw;
    gesture_->start = start;
    markTargets(&start);
    if (scene_.mode() != DiagramScene::DrawMode) {
        scene_.setMode(DiagramScene::DrawMode);
    }
    Q_EMIT scene_.statusMessage(QStringLiteral("Click to place bends, then a port or a wire to finish (Esc cancels, Backspace removes a bend)."));
}

void DiagramInteraction::cancelDrawing()
{
    gesture_.reset();
    markTargets(nullptr);
    hidePreview();
}

void DiagramInteraction::updateDrawingPreview(const QPointF scenePos)
{
    if (!gesture_) {
        return;
    }
    auto& gesture = *gesture_;
    const auto target = endAt(scenePos, true);
    bool valid = false;
    if (target && !target->sameAs(gesture.start)) {
        if (!gesture.checkedTarget || !gesture.checkedTarget->sameAs(*target)) {
            gesture.checkedTarget = target;
            gesture.checkedProblem = scene_.checkConnection(gesture.start, *target);
        }
        valid = gesture.checkedProblem.isEmpty();
    }
    const QPointF end = valid ? scene_.anchorOf(*target).value_or(snap(scenePos)) : snap(scenePos);
    showPreview(previewRoute(gesture.start, gesture.bends, end, valid ? &*target : nullptr), valid);
}

bool DiagramInteraction::handleKey(const int key, const Qt::KeyboardModifiers)
{
    if (key == Qt::Key_Escape) {
        if (gesture_ && gesture_->kind == Gesture::Kind::Draw) {
            cancelDrawing();
        } else if (gesture_) {
            if (gesture_->kind == Gesture::Kind::Bend) {
                gesture_->wire->userBends = gesture_->baseBends;
                gesture_->wire->setRoute(gesture_->baseRoute);
                scene_.attach(*gesture_->wire);
            }
            gesture_.reset();
            markTargets(nullptr);
            hidePreview();
        } else if (scene_.mode() == DiagramScene::DrawMode) {
            scene_.setMode(DiagramScene::NormalMode);
        }
        return true;
    }
    if (key == Qt::Key_Delete || key == Qt::Key_Backspace) {
        if (gesture_ && gesture_->kind == Gesture::Kind::Draw) {
            if (gesture_->bends.isEmpty()) {
                cancelDrawing();
            } else {
                gesture_->bends.removeLast();
            }
            return true;
        }
        if (scene_.mode() == DiagramScene::NormalMode && !gesture_) {
            scene_.deleteSelection();
            return true;
        }
    }
    return false;
}

void DiagramInteraction::showWireMenu(SignalWire& wire, const QPointF scenePos, const QPoint screenPos)
{
    if (scene_.mode() == DiagramScene::DrawMode || gesture_) {
        return;
    }
    showWireContextMenu(scene_, wire, scenePos, screenPos);
}

void DiagramInteraction::mousePressEvent(QGraphicsSceneMouseEvent* event, const std::function<void()>& base)
{
    moveGesture_.reset();
    const QPointF pos = event->scenePos();
    if (event->button() == Qt::LeftButton) {
        mouseDown_ = true;
    }
    if (scene_.mode() == DiagramScene::DrawMode) {
        event->accept();
        if (event->button() == Qt::RightButton) {
            if (gesture_) {
                cancelDrawing();
            } else {
                scene_.setMode(DiagramScene::NormalMode);
            }
            return;
        }
        if (event->button() != Qt::LeftButton) {
            return;
        }
        if (!gesture_ || gesture_->kind != Gesture::Kind::Draw) {
            if (const auto end = endAt(pos, true)) {
                startDrawing(*end);
            } else {
                Q_EMIT scene_.statusMessage(QStringLiteral("Start a wire on a port or on a wire."));
            }
            return;
        }
        auto& gesture = *gesture_;
        if (const auto end = endAt(pos, true)) {
            if (end->sameAs(gesture.start)) {
                return;
            }
            if (const auto problem = scene_.checkConnection(gesture.start, *end); !problem.isEmpty()) {
                Q_EMIT scene_.statusMessage(problem);
                return;
            }
            const auto start = gesture.start;
            const auto bends = gesture.bends;
            cancelDrawing();
            scene_.connectEnds(start, *end, bends);
            return;
        }
        if (insideElement(pos)) {
            Q_EMIT scene_.statusMessage(QStringLiteral("A bend cannot be placed on an element."));
            return;
        }
        const QPointF bend = snap(pos);
        if (gesture.bends.isEmpty() || !same(gesture.bends.back(), bend)) {
            gesture.bends.append(bend);
        }
        return;
    }

    QSchematic::Items::Node* moveTarget = nullptr;
    if (scene_.mode() == DiagramScene::NormalMode && event->button() == Qt::LeftButton) {
        // Editing the bends of a selected wire (a press on a port connects instead).
        if (const auto wire = wireAt(pos); wire && wire->isSelected() && !portAt(pos)) {
            const auto route = wire->pointsAbsolute();
            int routeIndex = -1;
            bool segment = false;
            for (int index = 1; index + 1 < route.size(); ++index) {
                if (QLineF(route[index], pos).length() <= kWireHandleHit) {
                    routeIndex = index;
                }
            }
            if (routeIndex < 0) {
                for (int index = 0; index + 1 < route.size(); ++index) {
                    if (distanceToSegment(pos, route[index], route[index + 1]) <= kSegmentHit) {
                        routeIndex = index;
                        segment = true;
                        break;
                    }
                }
            }
            if (routeIndex >= 0) {
                gesture_ = std::make_unique<Gesture>();
                gesture_->kind = Gesture::Kind::Bend;
                gesture_->wire = wire;
                gesture_->baseBends = wire->userBends;
                gesture_->baseRoute = route;
                gesture_->routeIndex = routeIndex;
                gesture_->segment = segment;
                gesture_->press = snap(pos);
                event->accept();
                return;
            }
        }
        // Connecting from a port, or (Alt) from a wire.
        std::optional<WireEnd> start;
        if (portAt(pos)) {
            start = endAt(pos, false);
        } else if (event->modifiers().testFlag(Qt::AltModifier)) {
            start = endAt(pos, true);
        }
        if (start) {
            gesture_ = std::make_unique<Gesture>();
            gesture_->kind = Gesture::Kind::Connect;
            gesture_->start = *start;
            gesture_->press = snap(pos);
            markTargets(&*start);
            event->accept();
            return;
        }
        if (auto* item = scene_.itemAt(pos, QTransform())) {
            if (auto* node = dynamic_cast<QSchematic::Items::Node*>(item);
                node && node->mode() == QSchematic::Items::Node::Mode::None) {
                moveTarget = node;
            }
        }
    }
    base();
    if (moveTarget) {
        auto move = std::make_unique<MoveGesture>();
        move->press = pos;
        std::unordered_set<SignalWire*> seen;
        for (const auto& item : scene_.selectedTopLevelItems()) {
            if (item && item->isMovable()) {
                move->items.emplace_back(item, item->pos());
                if (const auto node = std::dynamic_pointer_cast<ElementNode>(item)) {
                    for (const auto& wire : scene_.wiresOf(*node)) {
                        if (seen.insert(wire.get()).second) {
                            move->incidentWires.push_back(wire);
                        }
                    }
                }
            }
        }
        if (!move->items.empty()) {
            moveGesture_ = std::move(move);
        }
    }
}

void DiagramInteraction::mouseMoveEvent(QGraphicsSceneMouseEvent* event, const std::function<void()>& base)
{
    const QPointF pos = event->scenePos();
    if (gesture_ && gesture_->kind == Gesture::Kind::Draw) {
        updateDrawingPreview(pos);
        event->accept();
        return;
    }
    if (gesture_ && gesture_->kind == Gesture::Kind::Connect) {
        gesture_->moved = gesture_->moved || !same(snap(pos), gesture_->press);
        if (gesture_->moved) {
            updateDrawingPreview(pos);
        }
        event->accept();
        return;
    }
    if (gesture_ && gesture_->kind == Gesture::Kind::Bend) {
        auto& gesture = *gesture_;
        const auto& baseRoute = gesture.baseRoute;
        const QPointF delta = snap(pos) - gesture.press;
        QVector<QPointF> bends = gesture.baseBends;
        if (!gesture.segment) {
            const QPointF point = baseRoute[gesture.routeIndex];
            if (const auto existing = bends.indexOf(point); existing >= 0) {
                bends[existing] = point + delta;
            } else {
                bends.insert(bendsBefore(baseRoute, bends, gesture.routeIndex), point + delta);
            }
        } else {
            const int first = gesture.routeIndex;
            const QPointF a = baseRoute[first];
            const QPointF b = baseRoute[first + 1];
            const bool horizontal = std::abs(a.y() - b.y()) < 0.5;
            const QPointF move = horizontal ? QPointF(0.0, delta.y()) : QPointF(delta.x(), 0.0);
            const QPointF along = QLineF(a, b).length() > 0.0 ? (b - a) / QLineF(a, b).length() * kGridStep : QPointF();
            // Ends of the wire stay; a stub one step along the segment moves instead.
            const QPointF movedA = (first == 0 ? a + along : a) + move;
            const QPointF movedB = (first + 2 == baseRoute.size() ? b - along : b) + move;
            int insertAt = bendsBefore(baseRoute, bends, first);
            QVector<QPointF> kept;
            for (int index = 0; index < bends.size(); ++index) {
                if (onSegment(bends[index], a, b)) {
                    insertAt = std::min(insertAt, index);
                    continue;
                }
                kept.append(bends[index]);
            }
            insertAt = std::min<int>(insertAt, kept.size());
            kept.insert(insertAt, movedB);
            kept.insert(insertAt, movedA);
            bends = withoutRepeats(kept);
        }
        gesture.wire->userBends = bends;
        DiagramRouting(scene_.signalWires(), scene_.nodeRects()).route(
            {gesture.wire.get()}, [this](SignalWire& wire) { scene_.attach(wire); });
        event->accept();
        return;
    }
    if (moveGesture_ && event->buttons().testFlag(Qt::LeftButton)) {
        const QPointF delta = pos - moveGesture_->press;
        for (const auto& [item, initialPosition] : moveGesture_->items) {
            item->setPos(initialPosition + delta);
        }
        // Maintain the active connection geometry without simplifying every
        // unrelated wire on every pointer event.
        for (const auto& wire : moveGesture_->incidentWires) {
            wire->simplify();
        }
        event->accept();
        return;
    }
    base();
}

void DiagramInteraction::mouseReleaseEvent(QGraphicsSceneMouseEvent* event, const std::function<void()>& base)
{
    if (event->button() == Qt::LeftButton) {
        mouseDown_ = false;
    }
    if (gesture_ && gesture_->kind == Gesture::Kind::Draw) {
        event->accept();
        return;
    }
    if (gesture_ && gesture_->kind == Gesture::Kind::Connect) {
        event->accept();
        const auto start = gesture_->start;
        if (!gesture_->moved) {
            // A click on a port (no drag) starts drawing click by click.
            startDrawing(start);
            return;
        }
        gesture_.reset();
        markTargets(nullptr);
        hidePreview();
        if (const auto target = endAt(event->scenePos(), true); target && !target->sameAs(start)) {
            scene_.connectEnds(start, *target);
        }
        return;
    }
    if (gesture_ && gesture_->kind == Gesture::Kind::Bend) {
        event->accept();
        const auto wire = gesture_->wire;
        const auto bends = wire->userBends;
        wire->userBends = gesture_->baseBends;
        const auto baseRoute = gesture_->baseRoute;
        gesture_.reset();
        if (bends != wire->userBends) {
            scene_.setUserBends(wire, bends);
        } else {
            wire->setRoute(baseRoute);
            scene_.attach(*wire);
        }
        return;
    }
    base();
    moveGesture_.reset();
    scene_.checkMovedNodes();
}

void DiagramInteraction::mouseDoubleClickEvent(QGraphicsSceneMouseEvent* event, const std::function<void()>& base)
{
    if (scene_.mode() == DiagramScene::DrawMode || gesture_) {
        event->accept();
        return;
    }
    base();
}

} // namespace s7studio::diagram
