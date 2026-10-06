#include "diagram/diagram_scene.hpp"

#include "diagram/element_ports.hpp"
#include "diagram/net_model.hpp"
#include "diagram/wire_router.hpp"
#include "local_runtime_v2.hpp"

#include <qschematic/wire_system/manager.hpp>

#include <s7/type_inference.hpp>

#include <QGraphicsPathItem>
#include <QGraphicsSceneMouseEvent>
#include <QMenu>
#include <QPainterPath>
#include <QPen>
#include <QUndoCommand>
#include <QUndoStack>

#include <algorithm>
#include <cmath>
#include <unordered_set>

namespace s7studio::diagram {

namespace {

constexpr double kHandleHit = 6.0;  // a press this close to a bend grabs it
constexpr double kSegmentHit = 4.0; // a press this close to a segment grabs it
constexpr int kRouteMargin = 12;    // grid steps around the content the router may use

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

// Whether `point` lies on the orthogonal segment a-b.
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

auto distanceToSegment(const QPointF point, const QPointF a, const QPointF b) -> double
{
    const QPointF d = b - a;
    const double length2 = QPointF::dotProduct(d, d);
    const double t = length2 > 0.0 ? std::clamp(QPointF::dotProduct(point - a, d) / length2, 0.0, 1.0) : 0.0;
    return QLineF(point, a + t * d).length();
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
           elements.empty();
}

struct DiagramScene::Gesture {
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

DiagramScene::DiagramScene(QObject* parent)
    : Scene(parent)
{
    setWireFactory([] { return std::make_shared<SignalWire>(); });
    connect(undoStack(), &QUndoStack::indexChanged, this, [this] {
        if (!applying_ && !mouseDown_) {
            checkMovedNodes();
        }
    });
    connect(this, &QSchematic::Scene::modeChanged, this, [this](const int mode) {
        if (mode != DrawMode && gesture_ && gesture_->kind == Gesture::Kind::Draw) {
            cancelDrawing();
        }
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
    gesture_.reset();
    preview_ = nullptr; // deleted by clear()
    clear();
    knownPositions_.clear();
    typesResolve_.reset();
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

// Undoable edits

void DiagramScene::applyChange(const DiagramChange& change, const QString& text)
{
    if (!change.empty()) {
        undoStack()->push(new ChangeCommand(this, change, text));
    }
}

void DiagramScene::apply(const DiagramChange& change, const bool revert)
{
    applying_ = true;
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
    Q_EMIT diagramChanged();
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
    collectRemoval(change, lost);
    applyChange(change, QStringLiteral("Change %1").arg(QString::fromStdString(node->id())));
}

void DiagramScene::collectRemoval(DiagramChange& change, std::vector<std::shared_ptr<SignalWire>> wires) const
{
    std::unordered_set<const QSchematic::Items::Item*> removedNodes;
    for (const auto& node : change.removedNodes) {
        removedNodes.insert(node.get());
    }
    std::unordered_set<const SignalWire*> removed;
    for (const auto& wire : wires) {
        removed.insert(wire.get());
    }
    // Wires into removed elements, and every wire of a net whose driver is removed.
    for (const auto& wire : signalWires()) {
        const auto target = wire->downstream.node.lock();
        const auto* root = NetModel::rootOf(*wire);
        const auto driver = root ? root->upstream.node.lock() : nullptr;
        if ((target && removedNodes.contains(target.get())) || (driver && removedNodes.contains(driver.get()))) {
            removed.insert(wire.get());
        }
    }
    auto all = signalWires();
    for (const auto& wire : all) {
        if (removed.contains(wire.get())) {
            change.removedWires.push_back(wire);
        }
    }

    // Wires hanging on removed wires move up to the nearest remaining wire,
    // or become the first wire of their output.
    std::vector<std::shared_ptr<SignalWire>> survivors;
    for (const auto& wire : all) {
        if (!removed.contains(wire.get()) && wire->upstream.isJunction()) {
            const auto trunk = wire->upstream.wire.lock();
            if (trunk && removed.contains(trunk.get())) {
                survivors.push_back(wire);
            }
        }
    }
    std::stable_sort(survivors.begin(), survivors.end(),
                     [](const auto& a, const auto& b) { return NetModel::depthOf(*a) < NetModel::depthOf(*b); });
    std::vector<std::pair<WireEnd, std::shared_ptr<SignalWire>>> newTrunks;
    for (const auto& wire : survivors) {
        WireEnd end = wire->upstream;
        while (end.isJunction()) {
            const auto trunk = end.wire.lock();
            if (!trunk || !removed.contains(trunk.get())) {
                break;
            }
            end = trunk->upstream;
        }
        if (end.isOutput()) {
            std::shared_ptr<SignalWire> trunk;
            for (const auto& candidate : all) {
                if (!removed.contains(candidate.get()) && candidate != wire && candidate->upstream.sameAs(end)) {
                    trunk = candidate;
                }
            }
            for (const auto& [port, taken] : newTrunks) {
                if (port.sameAs(end)) {
                    trunk = taken;
                }
            }
            if (trunk) {
                end = WireEnd::onWire(trunk, anchorOf(end).value_or(QPointF()));
            } else {
                newTrunks.emplace_back(end, wire);
            }
        }
        change.rehung.push_back({wire, wire->upstream, end});
    }
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
    collectRemoval(change, wires);
    applyChange(change, QStringLiteral("Delete"));
}

void DiagramScene::deleteWire(const std::shared_ptr<SignalWire>& wire)
{
    DiagramChange change;
    collectRemoval(change, {wire});
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

auto DiagramScene::trunkOf(const WireEnd& output) const -> std::shared_ptr<SignalWire>
{
    for (const auto& wire : signalWires()) {
        if (wire->upstream.sameAs(output)) {
            return wire;
        }
    }
    return nullptr;
}

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

auto DiagramScene::driverEnd(const WireEnd& end) const -> std::optional<WireEnd>
{
    if (end.isOutput()) {
        return end;
    }
    if (end.isJunction()) {
        if (const auto wire = end.wire.lock()) {
            if (const auto* root = NetModel::rootOf(*wire); root && root->upstream.isOutput()) {
                return root->upstream;
            }
        }
    }
    return std::nullopt;
}

auto DiagramScene::typeProblem(const WireEnd& driver, const WireEnd& input) const -> QString
{
    const auto from = driver.node.lock();
    const auto to = input.node.lock();
    if (!from || !to) {
        return {};
    }
    // The resolver's first error, empty if the schema resolves; unknown
    // element types (plugins not loaded) count as not resolving.
    auto resolveError = [](const boost::json::object& schema) -> std::optional<QString> {
        try {
            const auto built = runtime::buildSchema(schema);
            const auto resolved = s7::TypeResolver::resolve(built);
            if (resolved.ok()) {
                return std::nullopt;
            }
            return QString::fromStdString(resolved.errors.front());
        } catch (const std::exception& ex) {
            return QString::fromUtf8(ex.what());
        }
    };
    auto schema = NetModel(*this).schemaJson("connection_check");
    if (!typesResolve_) {
        typesResolve_ = !resolveError(schema).has_value();
    }
    if (!*typesResolve_) {
        return {};
    }
    schema["connections"].as_array().push_back(boost::json::object{
        {"from_element", from->id()}, {"from_port", driver.index}, {"to_element", to->id()}, {"to_port", input.index}});
    if (const auto error = resolveError(schema)) {
        return QStringLiteral("Types do not match: %1").arg(*error);
    }
    return {};
}

auto DiagramScene::checkConnection(const WireEnd& a, const WireEnd& b) const -> QString
{
    auto kind = [](const WireEnd& end) { return end.isOutput() ? 0 : (end.isInput() ? 1 : (end.isJunction() ? 2 : 3)); };
    const int ka = kind(a);
    const int kb = kind(b);
    if (ka == 3 || kb == 3) {
        return QStringLiteral("A wire connects ports and wires.");
    }
    if (a.sameAs(b)) {
        return QStringLiteral("Pick another port.");
    }
    if (ka == 0 && kb == 0) {
        return QStringLiteral("Two outputs cannot be connected.");
    }
    if (ka == 1 && kb == 1) {
        return QStringLiteral("An input connects to an output or to a wire carrying a signal.");
    }
    if (ka == 2 && kb == 2) {
        return QStringLiteral("A wire joins another wire only to feed an input.");
    }
    const WireEnd& input = ka == 1 ? a : b;
    const WireEnd& upstream = ka == 1 ? b : a;
    if (ka + kb == 2) {
        const WireEnd& output = ka == 0 ? a : b;
        const WireEnd& onWire = ka == 0 ? b : a;
        const auto driver = driverEnd(onWire);
        const auto node = driver ? driver->node.lock() : nullptr;
        if (driver && driver->sameAs(output)) {
            return QStringLiteral("The wire already carries this output.");
        }
        return QStringLiteral("The wire already carries a signal (%1); a net has one output.")
            .arg(node ? QStringLiteral("%1.%2").arg(node->title(), driver->port() ? driver->port()->name() : QString()) : QStringLiteral("?"));
    }
    const auto inputPort = input.port();
    if (!inputPort) {
        return QStringLiteral("The input is gone.");
    }
    for (const auto& wire : signalWires()) {
        if (wire->downstream.sameAs(input)) {
            return QStringLiteral("Input %1.%2 is already connected.").arg(input.node.lock()->title(), inputPort->name());
        }
    }
    const auto driver = driverEnd(upstream);
    if (!driver || !driver->port()) {
        return QStringLiteral("The wire is not connected to an output.");
    }
    return typeProblem(*driver, input);
}

auto DiagramScene::connectEnds(const WireEnd& a, const WireEnd& b, QVector<QPointF> bends) -> QString
{
    if (auto problem = checkConnection(a, b); !problem.isEmpty()) {
        Q_EMIT statusMessage(problem);
        return problem;
    }
    WireEnd upstream = a;
    WireEnd downstream = b;
    if (a.isInput()) {
        std::swap(upstream, downstream);
        std::reverse(bends.begin(), bends.end());
    }
    if (upstream.isOutput()) {
        // An output has one first wire; further wires branch off it.
        if (const auto trunk = trunkOf(upstream)) {
            upstream = WireEnd::onWire(trunk, anchorOf(upstream).value_or(QPointF()));
        }
    }
    auto wire = std::make_shared<SignalWire>();
    wire->upstream = upstream;
    wire->downstream = downstream;
    wire->userBends = withoutRepeats(bends);
    DiagramChange change;
    change.addedWires.push_back(wire);
    applyChange(change, QStringLiteral("Connect"));
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

auto DiagramScene::placeJunction(const SignalWire& branch) const -> std::optional<QPointF>
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

bool DiagramScene::routeIsValid(const SignalWire& wire) const
{
    const auto route = wire.pointsAbsolute();
    if (route.size() < 2) {
        return false;
    }
    const auto start = anchorOf(wire.upstream);
    const auto end = anchorOf(wire.downstream);
    if (!start || !end || !same(route.front(), *start) || !same(route.back(), *end)) {
        return false;
    }
    if (wire.upstream.isJunction()) {
        const auto trunk = wire.upstream.wire.lock();
        const auto trunkRoute = trunk ? trunk->pointsAbsolute() : QVector<QPointF>{};
        if (trunkRoute.size() < 2 || !onRoute(*start, trunkRoute) || same(*start, trunkRoute.front()) || same(*start, trunkRoute.back())) {
            return false;
        }
    }
    for (int index = 0; index + 1 < route.size(); ++index) {
        const QPointF a = route[index];
        const QPointF b = route[index + 1];
        if (std::abs(a.x() - b.x()) > 0.5 && std::abs(a.y() - b.y()) > 0.5) {
            return false;
        }
        const QRectF segment = QRectF(a, b).normalized();
        for (const auto& rect : nodeRects()) {
            const QRectF inner = rect.adjusted(1.0, 1.0, -1.0, -1.0);
            if (segment.right() >= inner.left() && segment.left() <= inner.right() && segment.bottom() >= inner.top() &&
                segment.top() <= inner.bottom()) {
                return false;
            }
        }
    }
    for (const auto& bend : wire.userBends) {
        if (!onRoute(bend, route)) {
            return false;
        }
    }
    return true;
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

void DiagramScene::route(std::vector<SignalWire*> forced)
{
    if (forced.empty()) {
        return;
    }
    const auto wires = signalWires();
    const auto rects = nodeRects();
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
    std::sort(forced.begin(), forced.end());
    forced.erase(std::unique(forced.begin(), forced.end()), forced.end());
    std::stable_sort(forced.begin(), forced.end(), [](const SignalWire* a, const SignalWire* b) {
        return NetModel::depthOf(*a) < NetModel::depthOf(*b);
    });
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
        attach(*wire);
        router.addWire(path, request.net);
    }
}

void DiagramScene::repairRoutes(std::vector<SignalWire*> forced)
{
    route(std::move(forced));
    // Rerouted wires may leave others invalid (a junction off its trunk, a
    // wire under a moved element); a few passes settle every tree.
    for (int pass = 0; pass < 4; ++pass) {
        std::vector<SignalWire*> invalid;
        for (const auto& wire : signalWires()) {
            if (!routeIsValid(*wire) && anchorOf(wire->downstream) && (wire->upstream.isJunction() || anchorOf(wire->upstream))) {
                invalid.push_back(wire.get());
            }
        }
        if (invalid.empty()) {
            break;
        }
        route(std::move(invalid));
    }
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

auto DiagramScene::portAt(const QPointF scenePos) const -> std::shared_ptr<PortConnector>
{
    for (auto* item : QGraphicsScene::items(scenePos)) {
        if (auto* port = dynamic_cast<PortConnector*>(item)) {
            if (auto* element = port->element()) {
                return element->port(port->direction(), port->index());
            }
        }
    }
    return nullptr;
}

auto DiagramScene::wireAt(const QPointF scenePos) const -> std::shared_ptr<SignalWire>
{
    for (auto* item : QGraphicsScene::items(scenePos)) {
        if (auto* wire = dynamic_cast<SignalWire*>(item)) {
            const auto route = wire->pointsAbsolute();
            for (int index = 0; index + 1 < route.size(); ++index) {
                if (distanceToSegment(scenePos, route[index], route[index + 1]) <= kSegmentHit + 2.0) {
                    return sharedWire(wire);
                }
            }
        }
    }
    return nullptr;
}

auto DiagramScene::endAt(const QPointF scenePos, const bool wires) const -> std::optional<WireEnd>
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

bool DiagramScene::insideElement(const QPointF scenePos) const
{
    for (const auto& rect : nodeRects()) {
        if (rect.adjusted(1.0, 1.0, -1.0, -1.0).contains(scenePos)) {
            return true;
        }
    }
    return false;
}

void DiagramScene::markTargets(const WireEnd* start)
{
    for (const auto& node : elementNodes()) {
        for (const auto* ports : {&node->inputs(), &node->outputs()}) {
            for (const auto& port : *ports) {
                auto mark = PortConnector::Mark::None;
                if (start) {
                    const auto end = WireEnd::onPort(node, port->direction(), port->index());
                    if (!end.sameAs(*start)) {
                        mark = checkConnection(*start, end).isEmpty() ? PortConnector::Mark::Target : PortConnector::Mark::Refused;
                    }
                }
                port->setMark(mark);
            }
        }
    }
}

void DiagramScene::showPreview(const QVector<QPointF>& points, const bool valid)
{
    if (!preview_) {
        preview_ = new QGraphicsPathItem;
        preview_->setZValue(1000.0);
        QGraphicsScene::addItem(preview_);
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

void DiagramScene::hidePreview()
{
    if (preview_) {
        preview_->setVisible(false);
    }
}

auto DiagramScene::previewRoute(const WireEnd& start, const QVector<QPointF>& bends, const QPointF end, const WireEnd* target) const
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
    const auto rects = nodeRects();
    for (const auto& rect : rects) {
        include(rect.topLeft());
        include(rect.bottomRight());
    }
    for (const auto& anchor : anchors) {
        include(anchor);
    }
    const auto wires = signalWires();
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

void DiagramScene::startDrawing(const WireEnd& start)
{
    gesture_ = std::make_unique<Gesture>();
    gesture_->kind = Gesture::Kind::Draw;
    gesture_->start = start;
    markTargets(&start);
    if (mode() != DrawMode) {
        setMode(DrawMode);
    }
    Q_EMIT statusMessage(QStringLiteral("Click to place bends, then a port or a wire to finish (Esc cancels, Backspace removes a bend)."));
}

void DiagramScene::cancelDrawing()
{
    gesture_.reset();
    markTargets(nullptr);
    hidePreview();
}

void DiagramScene::updateDrawingPreview(const QPointF scenePos)
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
            gesture.checkedProblem = checkConnection(gesture.start, *target);
        }
        valid = gesture.checkedProblem.isEmpty();
    }
    const QPointF end = valid ? anchorOf(*target).value_or(snap(scenePos)) : snap(scenePos);
    showPreview(previewRoute(gesture.start, gesture.bends, end, valid ? &*target : nullptr), valid);
}

bool DiagramScene::handleKey(const int key, const Qt::KeyboardModifiers)
{
    if (key == Qt::Key_Escape) {
        if (gesture_ && gesture_->kind == Gesture::Kind::Draw) {
            cancelDrawing();
        } else if (gesture_) {
            if (gesture_->kind == Gesture::Kind::Bend) {
                gesture_->wire->userBends = gesture_->baseBends;
                gesture_->wire->setRoute(gesture_->baseRoute);
                attach(*gesture_->wire);
            }
            gesture_.reset();
            markTargets(nullptr);
            hidePreview();
        } else if (mode() == DrawMode) {
            setMode(NormalMode);
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
        if (mode() == NormalMode && !gesture_) {
            deleteSelection();
            return true;
        }
    }
    return false;
}

void DiagramScene::showWireMenu(SignalWire& wire, const QPointF scenePos, const QPoint screenPos)
{
    const auto shared = sharedWire(&wire);
    if (!shared || mode() == DrawMode || gesture_) {
        return;
    }
    const auto route = wire.pointsAbsolute();
    QMenu menu;
    auto* add = menu.addAction(QStringLiteral("Add Bend Here"));
    QAction* remove = nullptr;
    int nearBend = -1;
    for (int index = 0; index < wire.userBends.size(); ++index) {
        if (QLineF(wire.userBends[index], scenePos).length() <= kHandleHit + 2.0) {
            nearBend = index;
        }
    }
    if (nearBend >= 0) {
        remove = menu.addAction(QStringLiteral("Remove Bend"));
    }
    QAction* reroute = nullptr;
    if (!wire.userBends.isEmpty()) {
        reroute = menu.addAction(QStringLiteral("Re-route (Remove Hand-Placed Bends)"));
    }
    menu.addSeparator();
    auto* erase = menu.addAction(QStringLiteral("Delete Wire"));
    const auto* chosen = menu.exec(screenPos);
    if (chosen == add) {
        const QPointF point = nearestOnRoute(scenePos, route);
        int segment = 0;
        for (int index = 0; index + 1 < route.size(); ++index) {
            if (onSegment(point, route[index], route[index + 1])) {
                segment = index;
                break;
            }
        }
        auto bends = wire.userBends;
        bends.insert(bendsBefore(route, bends, segment + 1), point);
        setUserBends(shared, bends);
    } else if (remove && chosen == remove) {
        auto bends = wire.userBends;
        bends.removeAt(nearBend);
        setUserBends(shared, bends);
    } else if (reroute && chosen == reroute) {
        setUserBends(shared, {});
    } else if (chosen == erase) {
        deleteWire(shared);
    }
}

void DiagramScene::mousePressEvent(QGraphicsSceneMouseEvent* event)
{
    const QPointF pos = event->scenePos();
    if (event->button() == Qt::LeftButton) {
        mouseDown_ = true;
    }
    if (mode() == DrawMode) {
        event->accept();
        if (event->button() == Qt::RightButton) {
            if (gesture_) {
                cancelDrawing();
            } else {
                setMode(NormalMode);
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
                Q_EMIT statusMessage(QStringLiteral("Start a wire on a port or on a wire."));
            }
            return;
        }
        auto& gesture = *gesture_;
        if (const auto end = endAt(pos, true)) {
            if (end->sameAs(gesture.start)) {
                return;
            }
            if (const auto problem = checkConnection(gesture.start, *end); !problem.isEmpty()) {
                Q_EMIT statusMessage(problem);
                return;
            }
            const auto start = gesture.start;
            const auto bends = gesture.bends;
            cancelDrawing();
            connectEnds(start, *end, bends);
            return;
        }
        if (insideElement(pos)) {
            Q_EMIT statusMessage(QStringLiteral("A bend cannot be placed on an element."));
            return;
        }
        const QPointF bend = snap(pos);
        if (gesture.bends.isEmpty() || !same(gesture.bends.back(), bend)) {
            gesture.bends.append(bend);
        }
        return;
    }

    if (mode() == NormalMode && event->button() == Qt::LeftButton) {
        // Editing the bends of a selected wire (a press on a port connects instead).
        if (const auto wire = wireAt(pos); wire && wire->isSelected() && !portAt(pos)) {
            const auto route = wire->pointsAbsolute();
            int routeIndex = -1;
            bool segment = false;
            for (int index = 1; index + 1 < route.size(); ++index) {
                if (QLineF(route[index], pos).length() <= kHandleHit) {
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
    }
    Scene::mousePressEvent(event);
}

void DiagramScene::mouseMoveEvent(QGraphicsSceneMouseEvent* event)
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
        route({gesture.wire.get()});
        event->accept();
        return;
    }
    Scene::mouseMoveEvent(event);
}

void DiagramScene::mouseReleaseEvent(QGraphicsSceneMouseEvent* event)
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
            connectEnds(start, *target);
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
            setUserBends(wire, bends);
        } else {
            wire->setRoute(baseRoute);
            attach(*wire);
        }
        return;
    }
    Scene::mouseReleaseEvent(event);
    checkMovedNodes();
}

void DiagramScene::mouseDoubleClickEvent(QGraphicsSceneMouseEvent* event)
{
    if (mode() == DrawMode || gesture_) {
        event->accept();
        return;
    }
    Scene::mouseDoubleClickEvent(event);
}

} // namespace s7studio::diagram
