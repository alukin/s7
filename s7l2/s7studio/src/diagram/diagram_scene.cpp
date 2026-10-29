// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-S7-plugin-permission

#include "diagram/diagram_scene.hpp"

#include "diagram/connection_planner.hpp"
#include "diagram/diagram_edit_policy.hpp"
#include "diagram/diagram_geometry.hpp"
#include "diagram/diagram_interaction.hpp"
#include "diagram/diagram_routing.hpp"
#include "diagram/element_ports.hpp"
#include "diagram/net_model.hpp"
#include "diagram/wire_removal_planner.hpp"
#include "local_runtime_v2.hpp"

#include <qschematic/wire_system/manager.hpp>

#include <QGraphicsSceneMouseEvent>
#include <QElapsedTimer>
#include <QScopedValueRollback>
#include <QLoggingCategory>
#include <QUndoCommand>
#include <QUndoStack>

#include <algorithm>
#include <unordered_set>

namespace s7studio::diagram {

namespace {

Q_LOGGING_CATEGORY(lcDiagramPerformance, "s7studio.diagram.performance")

auto nodeRect(const QSchematic::Items::Node& node) -> QRectF
{
    return {node.pos(), node.size()};
}

class ChangeCommand final : public QUndoCommand
{
public:
    ChangeCommand(DiagramScene* scene, DiagramChange change, const QString& text)
        : QUndoCommand(text)
        , scene_(scene)
        , change_(std::move(change))
    {
    }

    void redo() override { scene_->apply(change_, false); }
    void undo() override { scene_->apply(change_, true); }

private:
    DiagramScene* scene_;
    DiagramChange change_;
};

} // namespace

bool DiagramChange::empty() const
{
    return addedNodes.empty() && removedNodes.empty() && addedWires.empty() && removedWires.empty() && rehung.empty() && bends.empty() &&
           elements.empty() && textFrames.empty() && !probes && !observersPanel;
}

DiagramScene::DiagramScene(QObject* parent)
    : Scene(parent), interaction_(std::make_unique<DiagramInteraction>(*this))
{
    setWireFactory([] { return std::make_shared<SignalWire>(); });
    connect(undoStack(), &QUndoStack::indexChanged, this, [this] {
        if (!applying_ && !interaction_->mouseDown()) {
            checkMovedNodes();
        }
    });
    connect(this, &QSchematic::Scene::modeChanged, this, [this](const int mode) {
        interaction_->modeChanged(mode);
    });
}

DiagramScene::~DiagramScene()
{
    // The base clears the undo stack on destruction; nothing may react to that.
    disconnect(undoStack(), nullptr, this, nullptr);
    disconnect(this, nullptr, this, nullptr);
}

// Content

auto DiagramScene::elementNodes() const -> std::vector<std::shared_ptr<ElementNode>>
{
    return items<ElementNode>();
}

auto DiagramScene::elementNode(const std::string& id) const -> std::shared_ptr<ElementNode>
{
    for (const auto& node : elementNodes()) {
        if (node->id() == id) {
            return node;
        }
    }
    return nullptr;
}

auto DiagramScene::textFrames() const -> std::vector<std::shared_ptr<TextFrameNode>>
{
    return items<TextFrameNode>();
}

auto DiagramScene::signalWires() const -> std::vector<std::shared_ptr<SignalWire>>
{
    return items<SignalWire>();
}

auto DiagramScene::sharedWire(const SignalWire* wire) const -> std::shared_ptr<SignalWire>
{
    for (const auto& candidate : signalWires()) {
        if (candidate.get() == wire) {
            return candidate;
        }
    }
    return nullptr;
}

void DiagramScene::clearDiagram()
{
    observersPanel_.clear(); observersPanelManual_ = false;
    interaction_->clearForSceneReset();
    clear();
    setMode(NormalMode);
    knownPositions_.clear();
    typesResolve_.reset();
    if (!probes_.empty()) {
        probes_.clear();
        Q_EMIT probesChanged();
    }
}

void DiagramScene::addNodeItem(const std::shared_ptr<QSchematic::Items::Node>& node)
{
    addItem(node);
    knownPositions_[node.get()] = node->pos();
}

void DiagramScene::addWireItem(const std::shared_ptr<SignalWire>& wire)
{
    addWire(wire);
}

void DiagramScene::rerouteAll()
{
    std::vector<SignalWire*> all;
    for (const auto& wire : signalWires()) {
        all.push_back(wire.get());
    }
    for (auto* wire : all) {
        attach(*wire);
    }
    repairRoutes(all);
    rememberPositions();
}

void DiagramScene::restoreRoutes(const std::function<void(int)>& progress)
{
    QElapsedTimer timer;
    timer.start();
    std::vector<SignalWire*> invalid;
    const auto wires = signalWires();
    const DiagramRouting routing(wires, nodeRects());
    for (std::size_t index = 0; index < wires.size(); ++index) {
        const auto& wire = wires[index];
        attach(*wire);
        // A saved route is a display cache for the current node positions.
        // Existing diagrams may intentionally contain overlaps that the
        // automatic router cannot eliminate, so only structural cache
        // validity is checked during load.
        if (!routing.routeIsValid(*wire, false)) {
            invalid.push_back(wire.get());
        }
        if (progress) {
            progress(wires.empty() ? 30 : static_cast<int>((index + 1) * 30 / wires.size()));
        }
    }
    const auto cacheMisses = invalid.size();
    const int routesToRepair = std::max(1, static_cast<int>(invalid.size()));
    int routed = 0;
    const auto routeProgress = [&] {
        ++routed;
        if (progress) {
            progress(30 + std::min(60, routed * 60 / routesToRepair));
        }
    };
    if (!invalid.empty()) {
        repairRoutes(std::move(invalid), routeProgress);
    }
    if (progress) {
        progress(100);
    }
    rememberPositions();
    qCDebug(lcDiagramPerformance) << "route restore: wires=" << wires.size() << "cache_misses=" << cacheMisses
                                  << "elapsed_ms=" << timer.elapsed();
}

// Undoable edits

void DiagramScene::applyChange(const DiagramChange& change, const QString& text)
{
    if (!change.empty()) {
        undoStack()->push(new ChangeCommand(this, completeDiagramChange(change, probes_, observersPanel_), text));
    }
}

void DiagramScene::apply(const DiagramChange& change, const bool revert)
{
    const QScopedValueRollback applyingGuard(applying_, true);
    for (const auto& edit : change.textFrames) edit.node->setParams(revert ? edit.before : edit.after);
    std::vector<SignalWire*> forced;
    auto setUpstream = [&](const std::shared_ptr<SignalWire>& wire, const WireEnd& end) {
        if (const auto trunk = wire->upstream.wire.lock()) {
            trunk->disconnectWire(wire.get());
        }
        wire->upstream = end;
        forced.push_back(wire.get());
    };
    auto setElement = [&](const std::shared_ptr<ElementNode>& node, const ElementNode::Element& element) {
        node->setElement(element);
        for (const auto& wire : wiresOf(*node)) {
            forced.push_back(wire.get());
        }
    };
    if (!revert) {
        for (const auto& wire : change.removedWires) {
            removeWire(wire);
        }
        for (const auto& rehang : change.rehung) {
            setUpstream(rehang.wire, rehang.after);
        }
        for (const auto& node : change.removedNodes) {
            removeItem(node);
        }
        for (const auto& edit : change.elements) {
            setElement(edit.node, edit.after);
        }
        for (const auto& node : change.addedNodes) {
            addItem(node);
        }
        for (const auto& wire : change.addedWires) {
            addWire(wire);
            forced.push_back(wire.get());
        }
        for (const auto& bends : change.bends) {
            bends.wire->userBends = bends.after;
            forced.push_back(bends.wire.get());
        }
    } else {
        for (const auto& wire : change.addedWires) {
            removeWire(wire);
        }
        for (const auto& node : change.addedNodes) {
            removeItem(node);
        }
        for (const auto& edit : change.elements) {
            setElement(edit.node, edit.before);
        }
        for (const auto& node : change.removedNodes) {
            addItem(node);
        }
        for (const auto& wire : change.removedWires) {
            addWire(wire);
            forced.push_back(wire.get());
        }
        for (const auto& rehang : change.rehung) {
            setUpstream(rehang.wire, rehang.before);
        }
        for (const auto& bends : change.bends) {
            bends.wire->userBends = bends.before;
            forced.push_back(bends.wire.get());
        }
    }

    std::unordered_set<const SignalWire*> present;
    for (const auto& wire : signalWires()) {
        present.insert(wire.get());
        attach(*wire);
    }
    std::erase_if(forced, [&](const SignalWire* wire) { return !present.contains(wire); });
    repairRoutes(forced);
    rememberPositions();
    typesResolve_.reset();
    applying_ = false;
    if (change.observersPanel) {
        observersPanel_ = revert ? change.observersPanel->first : change.observersPanel->second;
        Q_EMIT observersPanelChanged();
    }
    if (change.probes) {
        probes_ = revert ? change.probes->first : change.probes->second;
        Q_EMIT probesChanged();
    }
    Q_EMIT diagramChanged();
}

void DiagramScene::setObserversPanel(boost::json::array selection, bool undoable, bool manual)
{
    const bool changedMode = manual && !observersPanelManual_;
    if (manual) observersPanelManual_ = true;
    if (selection == observersPanel_) { if (changedMode) { Q_EMIT observersPanelChanged(); Q_EMIT diagramChanged(); } return; }
    if (undoable) {
        DiagramChange change; change.observersPanel = std::pair{observersPanel_, std::move(selection)};
        applyChange(change, QStringLiteral("Choose observer previews"));
    } else { observersPanel_ = std::move(selection); Q_EMIT observersPanelChanged(); }
}

auto DiagramScene::addElement(ElementNode::Element element, const QPointF position) -> std::shared_ptr<ElementNode>
{
    auto node = std::make_shared<ElementNode>(std::move(element));
    node->setPos(snap(position));
    DiagramChange change;
    change.addedNodes.push_back(node);
    applyChange(change, QStringLiteral("Add %1").arg(QString::fromStdString(node->id())));
    return node;
}

auto DiagramScene::addTextFrame(const std::string& id, const boost::json::object& params, const QPointF position)
    -> std::shared_ptr<TextFrameNode>
{
    auto node = std::make_shared<TextFrameNode>(id, params);
    node->setPos(snap(position));
    DiagramChange change;
    change.addedNodes.push_back(node);
    applyChange(change, QStringLiteral("Add text frame"));
    return node;
}

void DiagramScene::changeElement(const std::shared_ptr<ElementNode>& node, ElementNode::Element element)
{
    if (!node || node->element() == element) {
        return;
    }
    const auto ports = elementPorts(element.type, element.params, element.schema);
    const int inputs = std::max(static_cast<int>(ports.inputs.size()), node->minimumInputs());
    const int outputs = std::max(static_cast<int>(ports.outputs.size()), node->minimumOutputs());

    DiagramChange change;
    change.elements.push_back({node, node->element(), element});
    std::vector<std::shared_ptr<SignalWire>> lost;
    for (const auto& wire : signalWires()) {
        const bool lostInput = wire->downstream.isInput() && wire->downstream.node.lock() == node && wire->downstream.index >= inputs;
        const auto* root = NetModel::rootOf(*wire);
        const bool lostDriver = root && root->upstream.isOutput() && root->upstream.node.lock() == node && root->upstream.index >= outputs;
        if (lostInput || lostDriver) {
            lost.push_back(wire);
        }
    }
    change = WireRemovalPlanner(signalWires()).plan(std::move(change), std::move(lost));
    applyChange(change, QStringLiteral("Change %1").arg(QString::fromStdString(node->id())));
}

void DiagramScene::changeTextFrame(const std::shared_ptr<TextFrameNode>& node, boost::json::object params)
{
    if (!node || node->params() == params) return;
    DiagramChange change;
    change.textFrames.push_back({node, node->params(), std::move(params)});
    applyChange(change, QStringLiteral("Edit text frame"));
}

void DiagramScene::deleteSelection()
{
    DiagramChange change;
    std::vector<std::shared_ptr<SignalWire>> wires;
    for (const auto& item : selectedTopLevelItems()) {
        if (auto wire = std::dynamic_pointer_cast<SignalWire>(item)) {
            wires.push_back(std::move(wire));
        } else if (auto node = std::dynamic_pointer_cast<QSchematic::Items::Node>(item);
                   node && (std::dynamic_pointer_cast<ElementNode>(node) || std::dynamic_pointer_cast<TextFrameNode>(node))) {
            change.removedNodes.push_back(std::move(node));
        }
    }
    change = WireRemovalPlanner(signalWires()).plan(std::move(change), std::move(wires));
    applyChange(change, QStringLiteral("Delete"));
}

void DiagramScene::deleteWire(const std::shared_ptr<SignalWire>& wire)
{
    DiagramChange change;
    change = WireRemovalPlanner(signalWires()).plan(std::move(change), {wire});
    applyChange(change, QStringLiteral("Delete wire"));
}

void DiagramScene::setUserBends(const std::shared_ptr<SignalWire>& wire, QVector<QPointF> bends)
{
    bends = withoutRepeats(bends);
    if (!wire || bends == wire->userBends) {
        return;
    }
    DiagramChange change;
    change.bends.push_back({wire, wire->userBends, std::move(bends)});
    applyChange(change, QStringLiteral("Edit wire"));
}

// Connections

auto DiagramScene::branchesOf(const SignalWire& wire) const -> std::vector<std::shared_ptr<SignalWire>>
{
    std::vector<std::shared_ptr<SignalWire>> branches;
    for (const auto& candidate : signalWires()) {
        if (candidate->upstream.isJunction() && candidate->upstream.wire.lock().get() == &wire) {
            branches.push_back(candidate);
        }
    }
    return branches;
}

auto DiagramScene::wiresOf(const ElementNode& node) const -> std::vector<std::shared_ptr<SignalWire>>
{
    std::vector<std::shared_ptr<SignalWire>> wires;
    for (const auto& wire : signalWires()) {
        if ((wire->upstream.isPort() && wire->upstream.node.lock().get() == &node) ||
            (wire->downstream.isPort() && wire->downstream.node.lock().get() == &node)) {
            wires.push_back(wire);
        }
    }
    return wires;
}

auto DiagramScene::checkConnection(const WireEnd& a, const WireEnd& b) const -> QString
{
    ConnectionPlanner planner(elementNodes(), signalWires(), [this] { return NetModel(*this).schemaJson("connection_check"); }, typesResolve_);
    const auto problem = planner.check(a, b);
    typesResolve_ = planner.typesResolve();
    return problem;
}

auto DiagramScene::connectEnds(const WireEnd& a, const WireEnd& b, QVector<QPointF> bends) -> QString
{
    ConnectionPlanner planner(elementNodes(), signalWires(), [this] { return NetModel(*this).schemaJson("connection_check"); }, typesResolve_);
    auto result = planner.plan(a, b, std::move(bends));
    typesResolve_ = planner.typesResolve();
    if (!result.problem.isEmpty()) {
        Q_EMIT statusMessage(result.problem);
        return result.problem;
    }
    applyChange(result.change, QStringLiteral("Connect"));
    return {};
}

// Routing

auto DiagramScene::nodeRects() const -> std::vector<QRectF>
{
    std::vector<QRectF> rects;
    for (const auto& node : elementNodes()) {
        rects.push_back(nodeRect(*node));
    }
    for (const auto& frame : textFrames()) {
        rects.push_back(nodeRect(*frame));
    }
    return rects;
}

auto DiagramScene::anchorOf(const WireEnd& end) const -> std::optional<QPointF>
{
    return DiagramRouting::anchorOf(end);
}

void DiagramScene::attach(SignalWire& wire)
{
    const auto manager = wire_manager();
    if (!manager) {
        return;
    }
    for (const auto& connector : connectors()) {
        if (const auto record = manager->attached_wire(connector.get()); record && record->wire == &wire) {
            manager->detach_wire(connector.get());
        }
    }
    if (wire.points_count() < 2) {
        return;
    }
    if (const auto port = wire.upstream.port()) {
        manager->detach_wire(port.get());
        manager->attach_wire_to_connector(&wire, 0, port.get());
    }
    if (const auto port = wire.downstream.port()) {
        manager->detach_wire(port.get());
        manager->attach_wire_to_connector(&wire, wire.points_count() - 1, port.get());
    }
    if (wire.upstream.isJunction()) {
        if (const auto trunk = wire.upstream.wire.lock()) {
            manager->connect_wire(trunk.get(), &wire, 0);
        }
    }
}

void DiagramScene::repairRoutes(std::vector<SignalWire*> forced, const std::function<void()>& routeProgress)
{
    DiagramRouting(signalWires(), nodeRects()).repairRoutes(
        std::move(forced), [this](SignalWire& wire) { attach(wire); }, routeProgress);
}

void DiagramScene::rememberPositions()
{
    knownPositions_.clear();
    for (const auto& item : items()) {
        if (std::dynamic_pointer_cast<QSchematic::Items::Node>(item)) {
            knownPositions_[item.get()] = item->pos();
        }
    }
}

void DiagramScene::checkMovedNodes()
{
    std::vector<SignalWire*> forced;
    bool moved = false;
    for (const auto& item : items()) {
        const auto node = std::dynamic_pointer_cast<QSchematic::Items::Node>(item);
        if (!node) {
            continue;
        }
        const auto it = knownPositions_.find(item.get());
        if (it != knownPositions_.end() && it->second == item->pos()) {
            continue;
        }
        moved = true;
        if (const auto element = std::dynamic_pointer_cast<ElementNode>(node)) {
            for (const auto& wire : wiresOf(*element)) {
                forced.push_back(wire.get());
            }
        }
    }
    if (moved) {
        repairRoutes(forced);
        rememberPositions();
    }
}

// Gestures

bool DiagramScene::handleKey(const int key, const Qt::KeyboardModifiers modifiers)
{
    return interaction_->handleKey(key, modifiers);
}

void DiagramScene::showWireMenu(SignalWire& wire, const QPointF scenePos, const QPoint screenPos)
{
    interaction_->showWireMenu(wire, scenePos, screenPos);
}

bool DiagramScene::addProbe(PortRef point, QString name, QString unit)
{
    if (std::ranges::any_of(probes_, [&](const auto& probe) { return probe.point == point; })) {
        return false;
    }
    auto after = probes_;
    after.push_back(DiagramProbe{std::move(point), std::move(name), std::move(unit)});
    DiagramChange change; change.probes = std::pair{probes_, std::move(after)};
    applyChange(change, QStringLiteral("Add probe"));
    return true;
}

void DiagramScene::removeProbe(const PortRef& point)
{
    auto after = probes_;
    std::erase_if(after, [&](const auto& probe) { return probe.point == point; });
    if (after == probes_) return;
    DiagramChange change; change.probes = std::pair{probes_, std::move(after)};
    applyChange(change, QStringLiteral("Remove probe"));
}

bool DiagramScene::renameProbe(const PortRef& point, QString name)
{
    auto after = probes_;
    for (auto& probe : after) if (probe.point == point) {
        if (probe.name == name) return true;
        probe.name = std::move(name);
        DiagramChange change; change.probes = std::pair{probes_, std::move(after)};
        applyChange(change, QStringLiteral("Rename probe"));
        return true;
    }
    return false;
}

void DiagramScene::setProbes(std::vector<DiagramProbe> probes)
{
    probes_ = std::move(probes);
    Q_EMIT probesChanged();
}

void DiagramScene::mousePressEvent(QGraphicsSceneMouseEvent* event)
{
    interaction_->mousePressEvent(event, [this, event] { Scene::mousePressEvent(event); });
}

void DiagramScene::mouseMoveEvent(QGraphicsSceneMouseEvent* event)
{
    interaction_->mouseMoveEvent(event, [this, event] { Scene::mouseMoveEvent(event); });
}

void DiagramScene::mouseReleaseEvent(QGraphicsSceneMouseEvent* event)
{
    interaction_->mouseReleaseEvent(event, [this, event] { Scene::mouseReleaseEvent(event); });
}

void DiagramScene::mouseDoubleClickEvent(QGraphicsSceneMouseEvent* event)
{
    interaction_->mouseDoubleClickEvent(event, [this, event] { Scene::mouseDoubleClickEvent(event); });
}

} // namespace s7studio::diagram
