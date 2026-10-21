#include "diagram/diagram_scene.hpp"
#include "scene_serializer.hpp"
#include <QApplication>
#include <QClipboard>
#include <QMimeData>
#include <QUndoStack>
#include <map>
#include <cmath>
#include <set>

namespace s7studio::diagram {
namespace {
constexpr auto mime = "application/x-s7-diagram-selection";
}
void DiagramScene::selectAll()
{
    for (const auto& node : elementNodes()) node->setSelected(true);
    for (const auto& node : textFrames()) node->setSelected(true);
    for (const auto& wire : signalWires()) wire->setSelected(true);
}

bool DiagramScene::copySelection() const
{
    DiagramScene staging;
    std::map<std::string, std::shared_ptr<ElementNode>> nodes;
    for (const auto& node : elementNodes()) if (node->isSelected()) {
        auto clone = std::make_shared<ElementNode>(node->element());
        clone->setMinimumPorts(node->minimumInputs(), node->minimumOutputs());
        clone->setPos(node->pos());
        staging.addNodeItem(clone); nodes.emplace(node->id(), clone);
    }
    for (const auto& frame : textFrames()) if (frame->isSelected()) {
        auto clone = std::make_shared<TextFrameNode>(frame->id(), frame->params());
        clone->setPos(frame->pos()); staging.addNodeItem(clone);
    }
    if (nodes.empty() && staging.textFrames().empty()) return false;
    // Include every internal connection, including observer links. Preserve
    // junctions when their parent is copied; otherwise anchor at the driver.
    std::map<const SignalWire*, std::shared_ptr<SignalWire>> wires;
    for (const auto& wire : signalWires()) {
        const auto driver = NetModel::driverOf(*wire);
        const auto target = wire->downstream.node.lock();
        if (driver && target && nodes.contains(driver->element) && nodes.contains(target->id()))
            wires.emplace(wire.get(), std::make_shared<SignalWire>());
    }
    for (const auto& wire : signalWires()) if (wires.contains(wire.get())) {
        auto clone = wires.at(wire.get());
        const auto driver = *NetModel::driverOf(*wire);
        const auto parent = wire->upstream.wire.lock();
        clone->upstream = parent && wires.contains(parent.get())
            ? WireEnd::onWire(wires.at(parent.get()), wire->upstream.at)
            : WireEnd::onPort(nodes.at(driver.element), PortDirection::Output, driver.port);
        clone->downstream = WireEnd::onPort(nodes.at(wire->downstream.node.lock()->id()), PortDirection::Input, wire->downstream.index);
        clone->userBends = wire->userBends;
        staging.addWireItem(clone);
    }
    std::vector<DiagramProbe> probes;
    for (const auto& probe : probes_) if (nodes.contains(probe.point.element)) probes.push_back(probe);
    staging.setProbes(std::move(probes));
    staging.rerouteAll();
    auto* data = new QMimeData;
    data->setData(mime, QByteArray::fromStdString(boost::json::serialize(SceneSerializer{}.serialize(staging, {}).toJson())));
    QApplication::clipboard()->setMimeData(data);
    return true;
}

bool DiagramScene::pasteSelection(QPointF position)
{
    const auto* data = QApplication::clipboard()->mimeData();
    if (!data || !data->hasFormat(mime)) return false;
    const auto bytes = data->data(mime);
    if (bytes.size() > 16 * 1024 * 1024) {
        Q_EMIT statusMessage(QStringLiteral("Selection exceeds the 16 MiB clipboard limit.")); return false;
    }
    try {
        DiagramScene staging;
        SceneSerializer{}.deserialize(DiagramDocument::fromJson(boost::json::parse(bytes.toStdString())), staging);
        DiagramChange change;
        std::set<std::string> used;
        for (const auto& node : elementNodes()) used.insert(node->id());
        for (const auto& frame : textFrames()) used.insert(frame->id());
        auto unique = [&](const std::string& original) {
            int suffix = 1; std::string id;
            do { id = original + "_copy" + std::to_string(suffix++); } while (used.contains(id));
            used.insert(id); return id;
        };
        const auto bounds = staging.itemsBoundingRect();
        auto offset = QPointF(std::round((position.x() - bounds.left()) / kGridStep) * kGridStep,
                                    std::round((position.y() - bounds.top()) / kGridStep) * kGridStep);
        QRectF occupied;
        for (const auto& node : elementNodes()) occupied = occupied.united(node->sceneBoundingRect());
        for (const auto& frame : textFrames()) occupied = occupied.united(frame->sceneBoundingRect());
        if (!occupied.isEmpty() && occupied.adjusted(-kGridStep, -kGridStep, kGridStep, kGridStep).intersects(bounds.translated(offset)))
            offset.setY(std::ceil((occupied.bottom() + 2 * kGridStep - bounds.top()) / kGridStep) * kGridStep);
        std::map<std::string, std::string> ids;
        for (const auto& node : staging.elementNodes()) {
            auto element = node->element();
            ids.emplace(element.id, unique(element.id)); element.id = ids.at(element.id);
            node->setElement(std::move(element)); node->setPos(node->pos() + offset);
            change.addedNodes.push_back(node);
        }
        for (const auto& frame : staging.textFrames()) {
            auto clone = std::make_shared<TextFrameNode>(unique(frame->id()), frame->params());
            clone->setPos(frame->pos() + offset); change.addedNodes.push_back(clone);
        }
        for (const auto& wire : staging.signalWires()) {
            for (auto& bend : wire->userBends) bend += offset;
            if (wire->upstream.isJunction()) wire->upstream.at += offset;
            change.addedWires.push_back(wire);
        }
        auto probes = probes_;
        std::set<QString> names;
        for (const auto& probe : probes) names.insert(probe.name);
        for (auto probe : staging.probes()) {
            probe.point.element = ids.at(probe.point.element);
            const auto original = probe.name; int suffix = 1;
            while (names.contains(probe.name)) probe.name = original + QStringLiteral("_copy%1").arg(suffix++);
            names.insert(probe.name); probes.push_back(std::move(probe));
        }
        if (probes != probes_) change.probes = std::pair{probes_, std::move(probes)};
        // Detach retained shared items before their destination scene owns them.
        for (const auto& wire : change.addedWires) staging.removeWire(wire);
        for (const auto& node : staging.elementNodes()) staging.removeItem(node);
        clearSelection();
        applyChange(change, QStringLiteral("Paste selection"));
        for (const auto& node : change.addedNodes) node->setSelected(true);
        return true;
    } catch (const std::exception& error) {
        Q_EMIT statusMessage(QStringLiteral("Cannot paste diagram selection: %1").arg(QString::fromUtf8(error.what())));
        return false;
    }
}
} // namespace s7studio::diagram
