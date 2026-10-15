#include "scene_serializer.hpp"

#include "diagram/diagram_scene.hpp"
#include "diagram/net_model.hpp"
#include "json_utils.hpp"

#include <algorithm>
#include <cmath>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

namespace s7studio::diagram {

namespace {

constexpr int kElementColumns = 9; // element width in grid steps (ElementNode)
constexpr int kColumnGap = 5;      // grid steps between columns, at least
constexpr int kRowGap = 3;         // grid steps between rows

struct ConnectionModel {
    std::string fromElement;
    int fromPort = 0;
    std::string toElement;
    int toPort = 0;

    auto operator<=>(const ConnectionModel&) const = default;
};

auto error(const std::string& message) -> std::runtime_error
{
    return std::runtime_error("diagram: " + message);
}

auto gridPoint(const boost::json::value& value, const std::string& what) -> QPoint
{
    if (!value.is_array() || value.as_array().size() != 2 || !value.as_array()[0].is_number() || !value.as_array()[1].is_number()) {
        throw error(what + " must be [x, y] in grid steps");
    }
    return {static_cast<int>(std::lround(json_utils::numberValue(value.as_array()[0]))),
            static_cast<int>(std::lround(json_utils::numberValue(value.as_array()[1])))};
}

auto scenePoint(const QPoint grid) -> QPointF
{
    return {static_cast<double>(grid.x() * kGridStep), static_cast<double>(grid.y() * kGridStep)};
}

auto gridJson(const QPointF point) -> boost::json::array
{
    return boost::json::array{std::lround(point.x() / kGridStep), std::lround(point.y() / kGridStep)};
}

// Element rows (height in grid steps) as ElementNode sizes them.
auto elementRows(const ElementNode& node) -> int
{
    return static_cast<int>(std::lround(node.size().height() / kGridStep));
}

// Columns by data flow (an element one column right of its inputs; the
// connections closing a loop do not order the columns), rows ordered by the
// mean row of the inputs.
auto layeredPositions(const std::vector<std::string>& nodes, const std::vector<ConnectionModel>& connections,
                      const std::unordered_map<std::string, int>& rows) -> std::unordered_map<std::string, QPoint>
{
    std::unordered_map<std::string, std::vector<std::string>> successors;
    std::unordered_map<std::string, std::vector<std::string>> predecessors;
    std::unordered_map<std::string, int> column;
    for (const auto& node : nodes) {
        column[node] = 0;
    }
    for (const auto& connection : connections) {
        successors[connection.fromElement].push_back(connection.toElement);
        predecessors[connection.toElement].push_back(connection.fromElement);
    }
    std::unordered_map<std::string, int> state; // 1: on stack, 2: done
    std::vector<std::string> finished;
    std::set<std::pair<std::string, std::string>> backEdges;
    std::function<void(const std::string&)> visit = [&](const std::string& node) {
        state[node] = 1;
        for (const auto& next : successors[node]) {
            if (state[next] == 1) {
                backEdges.insert({node, next});
            } else if (state[next] == 0) {
                visit(next);
            }
        }
        state[node] = 2;
        finished.push_back(node);
    };
    for (const auto& node : nodes) {
        if (state[node] == 0) {
            visit(node);
        }
    }
    for (auto it = finished.rbegin(); it != finished.rend(); ++it) {
        for (const auto& next : successors[*it]) {
            if (!backEdges.contains({*it, next})) {
                column[next] = std::max(column[next], column[*it] + 1);
            }
        }
    }

    std::map<int, std::vector<std::string>> columns;
    for (const auto& node : nodes) {
        columns[column[node]].push_back(node);
    }
    std::unordered_map<std::string, int> row;
    int rowCount = 0;
    for (auto& [index, members] : columns) {
        std::unordered_map<std::string, double> key;
        for (std::size_t position = 0; position < members.size(); ++position) {
            double sum = 0.0;
            int count = 0;
            for (const auto& from : predecessors[members[position]]) {
                if (const auto it = row.find(from); it != row.end() && column[from] < index) {
                    sum += it->second;
                    ++count;
                }
            }
            key[members[position]] = count > 0 ? sum / count : static_cast<double>(position);
        }
        std::stable_sort(members.begin(), members.end(), [&](const auto& a, const auto& b) { return key[a] < key[b]; });
        for (std::size_t position = 0; position < members.size(); ++position) {
            row[members[position]] = static_cast<int>(position);
            rowCount = std::max(rowCount, static_cast<int>(position) + 1);
        }
    }

    // Each gap is wide enough for a vertical track per output leaving the column.
    std::map<int, std::set<std::pair<std::string, int>>> leaving;
    for (const auto& connection : connections) {
        leaving[column[connection.fromElement]].insert({connection.fromElement, connection.fromPort});
    }
    std::map<int, int> columnLeft;
    int x = 2;
    for (const auto& [index, members] : columns) {
        columnLeft[index] = x;
        x += kElementColumns + std::max(kColumnGap, static_cast<int>(leaving[index].size()) + 3);
    }
    std::vector<int> rowHeight(static_cast<std::size_t>(std::max(rowCount, 1)), 5);
    for (const auto& [node, index] : row) {
        rowHeight[static_cast<std::size_t>(index)] = std::max(rowHeight[static_cast<std::size_t>(index)], rows.at(node));
    }
    std::vector<int> rowTop;
    int y = 2;
    for (const auto height : rowHeight) {
        rowTop.push_back(y);
        y += height + kRowGap;
    }

    std::unordered_map<std::string, QPoint> positions;
    for (const auto& node : nodes) {
        positions[node] = QPoint(columnLeft[column[node]], rowTop[static_cast<std::size_t>(row[node])]);
    }
    return positions;
}

auto parseEnd(const boost::json::value& value, const std::string& what, DiagramScene& scene,
              const std::unordered_map<std::string, std::shared_ptr<SignalWire>>& wires, const PortDirection direction) -> WireEnd
{
    if (!value.is_object()) {
        throw error(what + " must be an object");
    }
    const auto& object = value.as_object();
    if (const auto* port = object.if_contains("port")) {
        const auto& parts = port->is_array() ? port->as_array() : boost::json::array{};
        if (parts.size() != 3 || !parts[0].is_string() || !parts[1].is_string() || !parts[2].is_number()) {
            throw error(what + ".port must be [element, \"in\" | \"out\", index]");
        }
        const std::string element(parts[0].as_string());
        const std::string side(parts[1].as_string());
        const int index = static_cast<int>(json_utils::numberValue(parts[2]));
        const auto node = scene.elementNode(element);
        if (!node) {
            throw error(what + " names unknown element '" + element + "'");
        }
        if (side != (direction == PortDirection::Output ? "out" : "in")) {
            throw error(what + " must be an " + (direction == PortDirection::Output ? "output" : "input") + " port");
        }
        if (!node->port(direction, index)) {
            throw error(what + ": element '" + element + "' has no " + side + " port " + std::to_string(index));
        }
        return WireEnd::onPort(node, direction, index);
    }
    if (const auto* wire = object.if_contains("wire"); wire && direction == PortDirection::Output) {
        const std::string id = wire->is_string() ? std::string(wire->as_string()) : std::string{};
        const auto it = wires.find(id);
        if (it == wires.end()) {
            throw error(what + " names unknown wire '" + id + "'");
        }
        const auto* at = object.if_contains("at");
        return WireEnd::onWire(it->second, scenePoint(gridPoint(at ? *at : boost::json::value{}, what + ".at")));
    }
    throw error(what + " must name a port" + std::string(direction == PortDirection::Output ? " or a wire" : ""));
}

} // namespace

auto SceneSerializer::serialize(const DiagramScene& scene, const SerializationOptions& options) const -> DiagramDocument
{
    DiagramDocument doc;
    doc.id = options.schemaName;
    doc.dt = options.dt;
    doc.pageLayout.orientation = options.sheetOrientation;
    doc.pageLayout.widthMm = options.sheetWidthMm;
    doc.pageLayout.heightMm = options.sheetHeightMm;

    const NetModel model(scene);
    doc.runtimeSchema = model.schemaJson(options.schemaName.toStdString());
    if (!options.includeDiagram) {
        return doc;
    }
    doc.probes = scene.probes();

    boost::json::array nodes;
    for (const auto& node : scene.elementNodes()) {
        boost::json::object object{{"id", node->id()}, {"position", gridJson(node->pos())}};
        if (node->element().type == "Scope" || node->element().type == "PhasePortrait" ||
            node->element().type == "PhaseTrajectory3D") {
            object["observer"] = true;
            object["type"] = node->element().type;
            object["params"] = node->element().params;
        }
        if (!node->element().title.isEmpty()) {
            object["title"] = node->element().title.toStdString();
        }
        nodes.push_back(std::move(object));
    }

    // Wires that connect nothing are not part of the schema and are not saved.
    std::unordered_map<const SignalWire*, std::string> ids;
    std::set<const SignalWire*> broken;
    for (const auto& [wire, problem] : model.problems()) {
        broken.insert(wire);
    }
    for (const auto& wire : scene.signalWires()) {
        if (!broken.contains(wire.get())) {
            ids.emplace(wire.get(), "w" + std::to_string(ids.size() + 1));
        }
    }
    boost::json::array wires;
    for (const auto& wire : scene.signalWires()) {
        const auto id = ids.find(wire.get());
        if (id == ids.end()) {
            continue;
        }
        boost::json::object from;
        if (wire->upstream.isJunction()) {
            from["wire"] = ids.at(wire->upstream.wire.lock().get());
            from["at"] = gridJson(wire->upstream.at);
        } else {
            from["port"] = boost::json::array{wire->upstream.node.lock()->id(), "out", wire->upstream.index};
        }
        boost::json::array bends;
        for (const auto& bend : wire->userBends) {
            bends.push_back(gridJson(bend));
        }
        boost::json::array route;
        for (const auto& point : wire->pointsAbsolute()) {
            route.push_back(gridJson(point));
        }
        wires.push_back(boost::json::object{
            {"id", id->second},
            {"from", std::move(from)},
            {"to", boost::json::object{{"port", boost::json::array{wire->downstream.node.lock()->id(), "in", wire->downstream.index}}}},
            {"bends", std::move(bends)},
            {"route", std::move(route)},
        });
    }

    boost::json::array frames;
    for (const auto& frame : scene.textFrames()) {
        frames.push_back(boost::json::object{{"id", frame->id()}, {"position", gridJson(frame->pos())}, {"params", frame->params()}});
    }

    doc.diagram = boost::json::object{
        {"grid", boost::json::object{{"step", kGridStep}, {"mm_per_step", kGridStep * 25.4 / 96.0}}},
        {"nodes", std::move(nodes)},
        {"wires", std::move(wires)},
        {"text_frames", std::move(frames)},
    };
    return doc;
}

void SceneSerializer::deserialize(const DiagramDocument& document, DiagramScene& scene, const std::function<void(int)>& progress) const
{
    scene.clearDiagram();
    if (progress) {
        progress(0);
    }
    // A layout of another version is refused unless it lays nothing out
    // (documents written by tools carry an empty one).
    static const boost::json::object noLayout;
    auto laysOut = [](const boost::json::object& layout) {
        for (const char* key : {"nodes", "wires", "text_frames"}) {
            if (const auto* list = layout.if_contains(key); list && list->is_array() && !list->as_array().empty()) {
                return true;
            }
        }
        return false;
    };
    if (document.diagramVersion != kDiagramVersion && laysOut(document.diagram)) {
        throw error("diagram_version " + std::to_string(document.diagramVersion) + " is not supported (this Studio reads version " +
                    std::to_string(kDiagramVersion) + "); open the plain runtime schema to lay it out again");
    }
    const auto& layout = document.diagramVersion == kDiagramVersion ? document.diagram : noLayout;

    // Elements and connections of the runtime schema.
    struct Model {
        ElementNode::Element element;
        std::optional<QPoint> position;
        int inputs = 0;
        int outputs = 0;
    };
    std::vector<Model> models;
    std::unordered_map<std::string, std::size_t> index;
    std::vector<ConnectionModel> connections;
    if (document.runtimeSchema.is_object()) {
        const auto& schema = document.runtimeSchema.as_object();
        if (const auto* elements = json_utils::findObjectValue(schema, "elements"); elements && elements->is_array()) {
            for (const auto& value : elements->as_array()) {
                if (!value.is_object()) {
                    continue;
                }
                const auto& object = value.as_object();
                Model model;
                model.element.id = json_utils::stringValue(object, "id", "element_" + std::to_string(models.size()));
                model.element.type = json_utils::stringValue(object, "type", "Element");
                model.element.placement = json_utils::stringValue(object, "node", "");
                if (const auto* params = json_utils::findObjectValue(object, "params"); params && params->is_object()) {
                    model.element.params = params->as_object();
                }
                if (const auto* nested = json_utils::findObjectValue(object, "schema"); nested && nested->is_object()) {
                    model.element.schema = *nested;
                }
                if (!index.emplace(model.element.id, models.size()).second) {
                    throw error("element id '" + model.element.id + "' is used twice");
                }
                models.push_back(std::move(model));
            }
        }
        if (const auto* list = json_utils::findObjectValue(schema, "connections"); list && list->is_array()) {
            for (const auto& value : list->as_array()) {
                if (!value.is_object()) {
                    continue;
                }
                const auto& object = value.as_object();
                ConnectionModel connection{json_utils::stringValue(object, "from_element"), json_utils::intValue(object, "from_port"),
                                           json_utils::stringValue(object, "to_element"), json_utils::intValue(object, "to_port")};
                const auto from = index.find(connection.fromElement);
                const auto to = index.find(connection.toElement);
                if (from == index.end() || to == index.end()) {
                    throw error("connection " + connection.fromElement + ":" + std::to_string(connection.fromPort) + " -> " +
                                connection.toElement + ":" + std::to_string(connection.toPort) + " names an unknown element");
                }
                models[from->second].outputs = std::max(models[from->second].outputs, connection.fromPort + 1);
                models[to->second].inputs = std::max(models[to->second].inputs, connection.toPort + 1);
                connections.push_back(std::move(connection));
            }
        }
    }
    if (progress) {
        progress(8);
    }

    // Layout: positions and titles.
    if (const auto* nodes = json_utils::findObjectValue(layout, "nodes"); nodes && nodes->is_array()) {
        for (const auto& value : nodes->as_array()) {
            const auto& object = value.is_object() ? value.as_object() : boost::json::object{};
            const auto id = json_utils::stringValue(object, "id");
            auto it = index.find(id);
            const auto* observerFlag = json_utils::findObjectValue(object, "observer");
            const auto observerType = json_utils::stringValue(object, "type");
            if (it == index.end() && observerFlag && observerFlag->is_bool() && observerFlag->as_bool() &&
                (observerType == "Scope" || observerType == "PhasePortrait" ||
                 observerType == "PhaseTrajectory3D")) {
                Model observer;
                observer.element.id = id;
                observer.element.type = observerType;
                if (const auto* params = json_utils::findObjectValue(object, "params"); params && params->is_object()) {
                    observer.element.params = params->as_object();
                }
                index.emplace(id, models.size());
                models.push_back(std::move(observer));
                it = index.find(id);
            }
            if (it == index.end()) {
                throw error("layout node '" + id + "' is not an element of the schema");
            }
            auto& model = models[it->second];
            if (const auto* position = json_utils::findObjectValue(object, "position")) {
                model.position = gridPoint(*position, "position of '" + id + "'");
            }
            model.element.title = QString::fromStdString(json_utils::stringValue(object, "title", ""));
        }
    }

    std::vector<std::shared_ptr<ElementNode>> nodes;
    std::unordered_map<std::string, int> rows;
    for (auto& model : models) {
        auto node = std::make_shared<ElementNode>(model.element);
        node->setMinimumPorts(model.inputs, model.outputs);
        rows[model.element.id] = elementRows(*node);
        nodes.push_back(std::move(node));
    }
    const bool anyPosition = std::any_of(models.begin(), models.end(), [](const Model& model) { return model.position.has_value(); });
    if (!anyPosition) {
        std::vector<std::string> ids;
        for (const auto& model : models) {
            ids.push_back(model.element.id);
        }
        const auto positions = layeredPositions(ids, connections, rows);
        for (auto& model : models) {
            model.position = positions.at(model.element.id);
        }
    } else {
        // Elements without a position go in a row below the others.
        int bottom = 0;
        for (std::size_t at = 0; at < models.size(); ++at) {
            if (models[at].position) {
                bottom = std::max(bottom, models[at].position->y() + rows[models[at].element.id]);
            }
        }
        int x = 2;
        for (auto& model : models) {
            if (!model.position) {
                model.position = QPoint(x, bottom + kRowGap);
                x += kElementColumns + kColumnGap;
            }
        }
    }
    for (std::size_t at = 0; at < models.size(); ++at) {
        nodes[at]->setPos(scenePoint(*models[at].position));
        scene.addNodeItem(nodes[at]);
        if (progress) {
            progress(8 + (models.empty() ? 0 : static_cast<int>((at + 1) * 20 / models.size())));
        }
    }

    if (const auto* frames = json_utils::findObjectValue(layout, "text_frames"); frames && frames->is_array()) {
        for (const auto& value : frames->as_array()) {
            const auto& object = value.is_object() ? value.as_object() : boost::json::object{};
            const auto* params = json_utils::findObjectValue(object, "params");
            auto frame = std::make_shared<TextFrameNode>(json_utils::stringValue(object, "id", "text_frame"),
                                                         params && params->is_object() ? params->as_object() : boost::json::object{});
            const auto* position = json_utils::findObjectValue(object, "position");
            frame->setPos(scenePoint(position ? gridPoint(*position, "text frame position") : QPoint(2, 2)));
            scene.addNodeItem(frame);
        }
    }
    if (progress) {
        progress(32);
    }

    // Wires: as the layout has them, or one tree per output.
    if (const auto* list = json_utils::findObjectValue(layout, "wires"); list && list->is_array()) {
        std::unordered_map<std::string, std::shared_ptr<SignalWire>> wires;
        std::vector<std::pair<std::shared_ptr<SignalWire>, const boost::json::object*>> ordered;
        for (const auto& value : list->as_array()) {
            if (!value.is_object()) {
                throw error("every wire must be an object");
            }
            const auto id = json_utils::stringValue(value.as_object(), "id");
            auto wire = std::make_shared<SignalWire>();
            if (id.empty() || !wires.emplace(id, wire).second) {
                throw error("wire ids must be unique and not empty ('" + id + "')");
            }
            ordered.emplace_back(wire, &value.as_object());
        }
        std::size_t wireIndex = 0;
        for (const auto& [wire, object] : ordered) {
            const auto id = json_utils::stringValue(*object, "id");
            const auto* from = object->if_contains("from");
            const auto* to = object->if_contains("to");
            wire->upstream = parseEnd(from ? *from : boost::json::value{}, "wire " + id + " from", scene, wires, PortDirection::Output);
            wire->downstream = parseEnd(to ? *to : boost::json::value{}, "wire " + id + " to", scene, wires, PortDirection::Input);
            if (const auto* bends = object->if_contains("bends"); bends && bends->is_array()) {
                for (const auto& bend : bends->as_array()) {
                    wire->userBends.append(scenePoint(gridPoint(bend, "a bend of wire " + id)));
                }
            }
            if (const auto* route = object->if_contains("route"); route && route->is_array() && !route->as_array().empty()) {
                QVector<QPointF> points;
                bool valid = true;
                for (const auto& point : route->as_array()) {
                    if (!point.is_array() || point.as_array().size() != 2 || !point.as_array()[0].is_number() ||
                        !point.as_array()[1].is_number()) {
                        valid = false;
                        break;
                    }
                    points.append(scenePoint(gridPoint(point, "a cached route point of wire " + id)));
                }
                if (valid) {
                    wire->setRoute(points);
                }
            }
            scene.addWireItem(wire);
            ++wireIndex;
            if (progress) {
                progress(32 + (ordered.empty() ? 0 : static_cast<int>(wireIndex * 18 / ordered.size())));
            }
        }
        const NetModel model(scene);
        if (!model.problems().empty()) {
            throw error(model.problems().front().second.toStdString());
        }
        std::multiset<ConnectionModel> drawn;
        for (const auto& connection : model.connections()) {
            drawn.insert({connection.from.element, connection.from.port, connection.to.element, connection.to.port});
        }
        const std::multiset<ConnectionModel> declared(connections.begin(), connections.end());
        if (drawn != declared) {
            for (const auto& connection : declared) {
                if (!drawn.contains(connection)) {
                    throw error("connection " + connection.fromElement + ":" + std::to_string(connection.fromPort) + " -> " +
                                connection.toElement + ":" + std::to_string(connection.toPort) + " has no wire");
                }
            }
            for (const auto& connection : drawn) {
                if (!declared.contains(connection)) {
                    throw error("the wires connect " + connection.fromElement + ":" + std::to_string(connection.fromPort) + " -> " +
                                connection.toElement + ":" + std::to_string(connection.toPort) + ", which the schema does not");
                }
            }
        }
    } else {
        std::set<std::pair<std::string, int>> fed;
        std::map<std::pair<std::string, int>, std::shared_ptr<SignalWire>> trunks;
        std::size_t connectionIndex = 0;
        for (const auto& connection : connections) {
            const auto from = scene.elementNode(connection.fromElement);
            const auto to = scene.elementNode(connection.toElement);
            if (!fed.insert({connection.toElement, connection.toPort}).second) {
                throw error("input " + connection.toElement + ":" + std::to_string(connection.toPort) + " has two connections");
            }
            auto wire = std::make_shared<SignalWire>();
            const auto output = WireEnd::onPort(from, PortDirection::Output, connection.fromPort);
            if (const auto trunk = trunks.find({connection.fromElement, connection.fromPort}); trunk != trunks.end()) {
                wire->upstream = WireEnd::onWire(trunk->second, output.port() ? output.port()->scenePos() : QPointF());
            } else {
                wire->upstream = output;
                trunks.emplace(std::make_pair(connection.fromElement, connection.fromPort), wire);
            }
            wire->downstream = WireEnd::onPort(to, PortDirection::Input, connection.toPort);
            scene.addWireItem(wire);
            ++connectionIndex;
            if (progress) {
                progress(32 + (connections.empty() ? 0 : static_cast<int>(connectionIndex * 18 / connections.size())));
            }
        }
    }
    if (progress) {
        progress(52);
    }
    scene.restoreRoutes([&](const int percent) {
        if (progress) {
            progress(52 + percent * 48 / 100);
        }
    });
    scene.setProbes(document.probes);
    if (progress) {
        progress(100);
    }
}

} // namespace s7studio::diagram
