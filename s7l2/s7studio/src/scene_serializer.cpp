#include "scene_serializer.hpp"
#include "diagram_node_roles.hpp"
#include "json_utils.hpp"
#include "runtime_schema_extractor.hpp"

#include <s7/element_registry.hpp>

#include <qschematic/items/node.hpp>
#include <qschematic/items/connector.hpp>
#include <qschematic/items/wire.hpp>
#include <qschematic/scene.hpp>

#include <QGraphicsColorizeEffect>
#include <QGraphicsRectItem>
#include <QGraphicsTextItem>
#include <QPen>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <string_view>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <unordered_map>

#include <QLineF>
#include <vector>

namespace s7studio::diagram {

namespace {

constexpr double kDefaultNodeWidth = 180.0;
constexpr double kDefaultNodeHeight = 100.0;
constexpr double kDefaultTextFrameWidth = 260.0;
constexpr double kDefaultTextFrameHeight = 120.0;

struct NodeModel {
    std::string id;
    std::string type = "Element";
    boost::json::object params;
    boost::json::value nestedSchema;
    QString title;
    QPointF position;
    bool hasExplicitPosition = false;
    int inputPorts = 0;
    int outputPorts = 0;
    int channelCount = 1;
    QString placement; // node_uuid
};

struct ConnectionModel {
    std::string fromElement;
    int fromPort = 0;
    std::string toElement;
    int toPort = 0;
};

struct NodeWiring {
    std::vector<std::shared_ptr<QSchematic::Items::Connector>> inputs;
    std::vector<std::shared_ptr<QSchematic::Items::Connector>> outputs;
};

auto makeStyledConnector() -> std::shared_ptr<QSchematic::Items::Connector>
{
    auto connector = std::make_shared<QSchematic::Items::Connector>();
    auto* colorize = new QGraphicsColorizeEffect();
    colorize->setColor(QColor(143, 211, 255));
    colorize->setStrength(1.0);
    connector->setGraphicsEffect(colorize);
    return connector;
}

auto parseEncodedParams(const QString& encodedParams) -> boost::json::object
{
    if (encodedParams.isEmpty()) {
        return {};
    }

    boost::system::error_code ec;
    const auto parsed = boost::json::parse(encodedParams.toStdString(), ec);
    if (ec || !parsed.is_object()) {
        return {};
    }

    return parsed.as_object();
}

auto defaultTextFrameParams() -> boost::json::object
{
    return boost::json::object{
        {"text", "Text frame"},
        {"text_color", "#202020"},
        {"line_color", "#444444"},
        {"fill_color", "#fff4cc"},
        {"font_family", "Sans Serif"},
        {"font_size", 12},
        {"width", kDefaultTextFrameWidth},
        {"height", kDefaultTextFrameHeight},
    };
}

auto mergedTextFrameParams(const boost::json::object& custom) -> boost::json::object
{
    auto merged = defaultTextFrameParams();
    for (const auto& [key, value] : custom) {
        merged[key] = value;
    }
    return merged;
}

struct PortCounts {
    int inputs = 0;
    int outputs = 0;
};

auto nestedSchemaPortCounts(const boost::json::value& schemaValue) -> PortCounts
{
    if (!schemaValue.is_object()) {
        return {};
    }

    const auto& schemaObject = schemaValue.as_object();
    PortCounts counts;

    if (const auto* inputsValue = json_utils::findObjectValue(schemaObject, "exposed_inputs");
        inputsValue && inputsValue->is_array()) {
        counts.inputs = static_cast<int>(inputsValue->as_array().size());
    }

    if (const auto* outputsValue = json_utils::findObjectValue(schemaObject, "exposed_outputs");
        outputsValue && outputsValue->is_array()) {
        counts.outputs = static_cast<int>(outputsValue->as_array().size());
    }

    return counts;
}

auto declaredPortCounts(std::string_view type,
                        const boost::json::object& params = {},
                        const int fallbackChannelCount = 1,
                        const boost::json::value* nestedSchema = nullptr) -> PortCounts
{
    if (nestedSchema && nestedSchema->is_object()) {
        return nestedSchemaPortCounts(*nestedSchema);
    }

    if (type == "Constant" || type == "AnalogOscillator") {
        return {0, 1};
    }

    if (type == "Gain" ||
        type == "Delay" ||
        type == "Accumulator" ||
        type == "FirFilter" ||
        type == "IirFilter" ||
        type == "PreviousValue" ||
        type == "Convert") {
        return {1, 1};
    }

    if (type == "Adder") {
        int inputCount = 2;
        if (const auto it = params.find("weights"); it != params.end() && it->value().is_array()) {
            const auto& weights = it->value().as_array();
            if (!weights.empty()) {
                inputCount = static_cast<int>(weights.size());
            }
        }
        return {std::max(2, inputCount), 1};
    }
    if (type == "Multiply") {
        int inputCount = 2;
        if (const auto it = params.find("factors"); it != params.end() && it->value().is_array()) {
            const auto& factors = it->value().as_array();
            if (!factors.empty()) {
                inputCount = static_cast<int>(factors.size());
            }
        }
        return {std::max(2, inputCount), 1};
    }
    if (type == "ReImToComplex") {
        return {2, 1};
    }

    if (type == "ComplexToReIm" || type == "ComplexToMagnitudePhase") {
        return {1, 2};
    }

    if (type == "Scope") {
        const int channelCount = std::max(1, json_utils::intValue(params, "channel_count", fallbackChannelCount));
        return {channelCount, 0};
    }

    if (const auto* descriptor = s7::ElementRegistry::instance().find(std::string(type))) {
        const auto& metadata = descriptor->metadata();
        return {
            static_cast<int>(metadata.input_ports.size()),
            static_cast<int>(metadata.output_ports.size())
        };
    }

    return {};
}

// Connectors snap to the scene grid, so ports are one grid step (or more)
// apart and a node is tall enough for its ports.
constexpr double kPortPitch = 20.0;

auto nodeHeightFor(const int inputPorts, const int outputPorts) -> double
{
    const int ports = std::max({inputPorts, outputPorts, 1});
    return std::max(kDefaultNodeHeight, kPortPitch * static_cast<double>(ports + 1) + 20.0);
}

// Ports evenly spaced on the grid and centred on the node.
auto portOffsetY(const double nodeHeight, const int portIndex, const int portCount) -> double
{
    const int count = std::max(portCount, 1);
    const double pitch = std::max(kPortPitch, std::floor(nodeHeight / (count + 1) / kPortPitch) * kPortPitch);
    const double first = std::round((nodeHeight - pitch * (count - 1)) / 2.0 / kPortPitch) * kPortPitch;
    return first + pitch * static_cast<double>(portIndex);
}

auto sortedSceneNodes(const QSchematic::Scene& scene) -> std::vector<std::shared_ptr<QSchematic::Items::Node>>
{
    const auto nodes = scene.items<QSchematic::Items::Node>();
    std::vector<std::shared_ptr<QSchematic::Items::Node>> ordered(nodes.begin(), nodes.end());
    std::sort(ordered.begin(), ordered.end(), [](const auto& lhs, const auto& rhs) {
        if (lhs == rhs) {
            return false;
        }
        if (lhs->pos().y() != rhs->pos().y()) {
            return lhs->pos().y() < rhs->pos().y();
        }
        if (lhs->pos().x() != rhs->pos().x()) {
            return lhs->pos().x() < rhs->pos().x();
        }
        return lhs.get() < rhs.get();
    });
    return ordered;
}

auto connectorAtPort(const std::vector<std::shared_ptr<QSchematic::Items::Connector>>& connectors,
                     const int portIndex) -> std::shared_ptr<QSchematic::Items::Connector>
{
    if (portIndex < 0 || portIndex >= static_cast<int>(connectors.size())) {
        return {};
    }

    return connectors[portIndex];
}

void appendOrthogonalWirePath(QSchematic::Items::Wire& wire, const QPointF& startPoint, const QPointF& endPoint)
{
    wire.append_point(startPoint);

    if (std::abs(startPoint.x() - endPoint.x()) < 0.5 || std::abs(startPoint.y() - endPoint.y()) < 0.5) {
        wire.append_point(endPoint);
        return;
    }

    const double midX = (startPoint.x() + endPoint.x()) / 2.0;
    wire.append_point(QPointF(midX, startPoint.y()));
    wire.append_point(QPointF(midX, endPoint.y()));
    wire.append_point(endPoint);
}


// Layered automatic layout for diagrams without stored positions. Elements
// are placed in columns by data flow (an element one column right of its
// inputs; connections that close a loop are left out of the ordering), and
// every wire runs in tracks of its own between the columns, so wires never
// cross nodes and wires of different outputs never touch (QSchematic would
// merge touching wires into one net). Wires that go further than the next
// column, or back, use a lane of their own below all nodes.
class LayeredLayout {
public:
    static constexpr double kColumnGap = 80.0;
    static constexpr double kRowGap = 80.0;
    static constexpr double kTrackStep = 20.0;

    LayeredLayout(const std::vector<std::string>& nodes, const std::vector<ConnectionModel>& connections,
                  const std::unordered_map<std::string, double>& heights)
    {
        std::unordered_map<std::string, std::vector<std::string>> successors;
        std::unordered_map<std::string, std::size_t> order;
        for (std::size_t index = 0; index < nodes.size(); ++index) {
            order[nodes[index]] = index;
            column_[nodes[index]] = 0;
        }
        for (const auto& connection : connections) {
            if (order.contains(connection.fromElement) && order.contains(connection.toElement)) {
                successors[connection.fromElement].push_back(connection.toElement);
            }
        }
        // Depth-first search in declaration order; an edge to a node on the
        // stack closes a loop and does not order the columns.
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
        // Longest path over the remaining (acyclic) edges, in topological order.
        for (auto it = finished.rbegin(); it != finished.rend(); ++it) {
            for (const auto& next : successors[*it]) {
                if (!backEdges.contains({*it, next})) {
                    column_[next] = std::max(column_[next], column_[*it] + 1);
                }
            }
        }
        // Rows: declaration order within a column, then ordered by the mean
        // row of the inputs (fewer crossings).
        std::map<int, std::vector<std::string>> columns;
        for (const auto& node : nodes) {
            columns[column_[node]].push_back(node);
        }
        std::unordered_map<std::string, std::vector<std::string>> predecessors;
        for (const auto& connection : connections) {
            predecessors[connection.toElement].push_back(connection.fromElement);
        }
        for (auto& [column, members] : columns) {
            std::unordered_map<std::string, double> key;
            for (std::size_t index = 0; index < members.size(); ++index) {
                double sum = 0.0;
                int count = 0;
                for (const auto& from : predecessors[members[index]]) {
                    if (const auto row = row_.find(from); row != row_.end() && column_[from] < column) {
                        sum += row->second;
                        ++count;
                    }
                }
                key[members[index]] = count > 0 ? sum / count : static_cast<double>(index);
            }
            std::stable_sort(members.begin(), members.end(), [&](const auto& a, const auto& b) { return key[a] < key[b]; });
            for (std::size_t index = 0; index < members.size(); ++index) {
                row_[members[index]] = static_cast<int>(index);
                rows_ = std::max(rows_, static_cast<int>(index) + 1);
            }
            columns_ = std::max(columns_, column + 1);
        }
        // Tracks needed in each gap (gap g lies right of column g; gap -1 is
        // left of the first column), and lanes below the nodes.
        std::map<int, std::set<std::string>> tracks;
        int lanes = 0;
        for (const auto& connection : connections) {
            if (!order.contains(connection.fromElement) || !order.contains(connection.toElement)) {
                continue;
            }
            const int from = column_[connection.fromElement];
            const int to = column_[connection.toElement];
            tracks[from].insert(outputKey(connection));
            if (to != from + 1) {
                tracks[to - 1].insert(connectionKey(connection));
                ++lanes;
            }
        }
        double x = gapWidth(tracks, -1);
        for (int column = 0; column < columns_; ++column) {
            columnLeft_.push_back(x);
            x += kDefaultNodeWidth + gapWidth(tracks, column);
        }
        static_cast<void>(lanes);
        // Rows as tall as their tallest node.
        std::vector<double> rowHeight(static_cast<std::size_t>(std::max(rows_, 1)), kDefaultNodeHeight);
        for (const auto& [node, row] : row_) {
            if (const auto it = heights.find(node); it != heights.end()) {
                rowHeight[static_cast<std::size_t>(row)] = std::max(rowHeight[static_cast<std::size_t>(row)], it->second);
            }
        }
        double y = 0.0;
        for (const auto height : rowHeight) {
            rowTop_.push_back(y);
            y += height + kRowGap;
        }
        bottom_ = y - kRowGap;
    }

    [[nodiscard]] QPointF position(const std::string& node) const
    {
        return {columnLeft_.at(static_cast<std::size_t>(column_.at(node))), rowTop_.at(static_cast<std::size_t>(row_.at(node)))};
    }

    // The route of a connection between connector positions. The first
    // wire of an output starts at its connector; further wires of the same
    // output are branches starting on that wire, at its bend onto the
    // output's track (QSchematic attaches one wire per connector).
    struct Route {
        std::vector<QPointF> points;
        bool branch{false};
    };
    [[nodiscard]] Route route(const ConnectionModel& connection, const QPointF& start, const QPointF& end)
    {
        const int from = column_.at(connection.fromElement);
        const int to = column_.at(connection.toElement);
        const double first = track(from, outputKey(connection));
        const bool branch = !trunks_.insert(outputKey(connection)).second;
        const QPointF junction(first, start.y());
        std::vector<QPointF> points = branch ? std::vector<QPointF>{junction} : std::vector<QPointF>{start, junction};
        if (to == from + 1) {
            if (!branch && std::abs(start.y() - end.y()) < 0.5) {
                return {{start, end}, false};
            }
            points.push_back({first, end.y()});
        } else {
            const double second = track(to - 1, connectionKey(connection));
            const double lane = bottom_ + kTrackStep * static_cast<double>(++lanesUsed_);
            points.push_back({first, lane});
            points.push_back({second, lane});
            points.push_back({second, end.y()});
        }
        if (std::abs(points.back().y() - end.y()) > 0.5 || std::abs(points.back().x() - end.x()) > 0.5) {
            points.push_back(end);
        }
        return {std::move(points), branch};
    }

private:
    static auto outputKey(const ConnectionModel& c) -> std::string { return c.fromElement + ":" + std::to_string(c.fromPort); }
    static auto connectionKey(const ConnectionModel& c) -> std::string
    {
        return outputKey(c) + "->" + c.toElement + ":" + std::to_string(c.toPort);
    }
    static auto gapWidth(const std::map<int, std::set<std::string>>& tracks, const int gap) -> double
    {
        const auto it = tracks.find(gap);
        const auto count = it == tracks.end() ? 0 : it->second.size();
        return std::max(kColumnGap, kTrackStep * static_cast<double>(count + 1));
    }
    // x of the track for `key` in gap g (allocated on first use).
    auto track(const int gap, const std::string& key) -> double
    {
        auto& used = trackIndex_[gap];
        const auto [it, inserted] = used.emplace(key, used.size());
        const double gapLeft = gap < 0 ? 0.0 : columnLeft_.at(static_cast<std::size_t>(gap)) + kDefaultNodeWidth;
        return gapLeft + kTrackStep * static_cast<double>(it->second + 1);
    }

    std::unordered_map<std::string, int> column_;
    std::unordered_map<std::string, int> row_;
    int columns_{0};
    int rows_{0};
    std::vector<double> columnLeft_;
    std::vector<double> rowTop_;
    double bottom_{0.0};
    int lanesUsed_{0};
    std::map<int, std::map<std::string, std::size_t>> trackIndex_;
    std::set<std::string> trunks_; // outputs whose first wire is placed
};

void attachWireEndpoints(QSchematic::Scene& scene,
                         const std::shared_ptr<QSchematic::Items::Wire>& wire,
                         const std::shared_ptr<QSchematic::Items::Connector>& startConnector,
                         const std::shared_ptr<QSchematic::Items::Connector>& endConnector)
{
    auto wireManager = scene.wire_manager();
    if (!wireManager || !wire || wire->points_count() < 2) {
        return;
    }

    if (startConnector) {
        wireManager->attach_wire_to_connector(wire.get(), 0, startConnector.get());
    }
    if (endConnector) {
        wireManager->attach_wire_to_connector(wire.get(), wire->points_count() - 1, endConnector.get());
    }
}

void attachWireEndpointsByPosition(QSchematic::Scene& scene,
                                   const std::shared_ptr<QSchematic::Items::Wire>& wire)
{
    if (!wire || wire->points_count() < 2) {
        return;
    }

    std::shared_ptr<QSchematic::Items::Connector> startConnector;
    std::shared_ptr<QSchematic::Items::Connector> endConnector;
    const auto firstPoint = wire->points().first().toPoint();
    const auto lastPoint = wire->points().last().toPoint();

    for (const auto& connector : scene.connectors()) {
        if (!connector) {
            continue;
        }

        const auto connectorPoint = connector->scenePos().toPoint();
        if (!startConnector && connectorPoint == firstPoint) {
            startConnector = connector;
        }
        if (!endConnector && connectorPoint == lastPoint) {
            endConnector = connector;
        }

        if (startConnector && endConnector) {
            break;
        }
    }

    attachWireEndpoints(scene, wire, startConnector, endConnector);
}

} // namespace

auto SceneSerializer::serialize(const QSchematic::Scene& scene,
                               const SerializationOptions& options) const -> DiagramDocument {
    DiagramDocument doc;
    doc.id = options.schemaName;
    doc.dt = options.dt;
    doc.pageLayout.orientation = options.sheetOrientation;
    doc.pageLayout.widthMm = options.sheetWidthMm;
    doc.pageLayout.heightMm = options.sheetHeightMm;
    
    if (options.includeDiagram) {
        RuntimeSchemaExtractor extractor;
        std::vector<WireSource> connected;
        doc.runtimeSchema = extractor.extract(scene, options, &connected);
        doc.diagram = extractDiagramLayout(scene, connected);
    }
    
    return doc;
}

void SceneSerializer::deserialize(const DiagramDocument& document,
                                  QSchematic::Scene& scene) const {
    scene.clear();

    constexpr double xSpacing = 260.0;
    constexpr double ySpacing = 180.0;

    std::unordered_map<std::string, NodeModel> nodesById;
    std::vector<std::string> nodeOrder;
    std::vector<ConnectionModel> connections;
    std::vector<std::vector<QPointF>> diagramWireRoutes;
    // Routes that name their connection: "from:port->to:port" -> points.
    std::unordered_map<std::string, std::vector<QPointF>> keyedWireRoutes;
    auto routeKey = [](const std::string& from, int fromPort, const std::string& to, int toPort) {
        return from + ":" + std::to_string(fromPort) + "->" + to + ":" + std::to_string(toPort);
    };

    if (document.runtimeSchema.is_object()) {
        const auto& schemaObject = document.runtimeSchema.as_object();

        if (const auto* elementsValue = json_utils::findObjectValue(schemaObject, "elements"); elementsValue && elementsValue->is_array()) {
            int elementIndex = 0;
            for (const auto& elementValue : elementsValue->as_array()) {
                if (!elementValue.is_object()) {
                    continue;
                }

                const auto& element = elementValue.as_object();
                NodeModel model;
                model.id = json_utils::stringValue(element, "id", "element_" + std::to_string(elementIndex++));
                model.type = json_utils::stringValue(element, "type", "Element");
                if (const auto* placement = json_utils::findObjectValue(element, "node"); placement && placement->is_string()) {
                    model.placement = QString::fromStdString(std::string(placement->as_string()));
                }
                if (const auto* paramsValue = json_utils::findObjectValue(element, "params"); paramsValue && paramsValue->is_object()) {
                    model.params = paramsValue->as_object();
                }
                if (const auto* schemaValue = json_utils::findObjectValue(element, "schema"); schemaValue && schemaValue->is_object()) {
                    model.nestedSchema = *schemaValue;
                }
                if (model.type == "Scope") {
                    model.channelCount = std::max(1, json_utils::intValue(model.params, "channel_count", 1));
                }
                const auto declaredPorts = declaredPortCounts(model.type, model.params, model.channelCount, &model.nestedSchema);
                model.inputPorts = std::max(model.inputPorts, declaredPorts.inputs);
                model.outputPorts = std::max(model.outputPorts, declaredPorts.outputs);
                model.title = QString::fromUtf8(model.id.c_str());

                nodeOrder.push_back(model.id);
                nodesById.emplace(model.id, std::move(model));
            }
        }

        if (const auto* connectionsValue = json_utils::findObjectValue(schemaObject, "connections"); connectionsValue && connectionsValue->is_array()) {
            for (const auto& connectionValue : connectionsValue->as_array()) {
                if (!connectionValue.is_object()) {
                    continue;
                }

                const auto& connectionObject = connectionValue.as_object();
                ConnectionModel model;
                model.fromElement = json_utils::stringValue(connectionObject, "from_element");
                model.fromPort = json_utils::intValue(connectionObject, "from_port");
                model.toElement = json_utils::stringValue(connectionObject, "to_element");
                model.toPort = json_utils::intValue(connectionObject, "to_port");
                connections.push_back(model);

                if (auto it = nodesById.find(model.fromElement); it != nodesById.end()) {
                    it->second.outputPorts = std::max(it->second.outputPorts, model.fromPort + 1);
                }
                if (auto it = nodesById.find(model.toElement); it != nodesById.end()) {
                    it->second.inputPorts = std::max(it->second.inputPorts, model.toPort + 1);
                }
            }
        }
    }

    if (const auto nodesIt = document.diagram.find("nodes"); nodesIt != document.diagram.end() && nodesIt->value().is_array()) {
        int fallbackIndex = static_cast<int>(nodeOrder.size());
        for (const auto& nodeValue : nodesIt->value().as_array()) {
            if (!nodeValue.is_object()) {
                continue;
            }

            const auto& nodeObject = nodeValue.as_object();
            const auto id = json_utils::stringValue(nodeObject, "id", "node_" + std::to_string(fallbackIndex++));

            auto [it, inserted] = nodesById.try_emplace(id);
            if (inserted) {
                it->second.id = id;
                it->second.type = json_utils::stringValue(nodeObject, "type", "Element");
                it->second.title = QString::fromUtf8(id.c_str());
                nodeOrder.push_back(id);
            }

            if (const auto name = json_utils::stringValue(nodeObject, "name"); !name.empty()) {
                it->second.title = QString::fromUtf8(name.c_str());
            }
            if (const auto type = json_utils::stringValue(nodeObject, "type"); !type.empty()) {
                it->second.type = type;
            }
            if (const auto* paramsValue = json_utils::findObjectValue(nodeObject, "params");
                paramsValue && paramsValue->is_object()) {
                it->second.params = paramsValue->as_object();
            }
            if (const auto* schemaValue = json_utils::findObjectValue(nodeObject, "schema");
                schemaValue && schemaValue->is_object()) {
                it->second.nestedSchema = *schemaValue;
            }
            if (it->second.type == "Scope") {
                it->second.channelCount = std::max(1, json_utils::intValue(nodeObject, "channel_count", it->second.channelCount));
            }
            const auto declaredPorts = declaredPortCounts(it->second.type,
                                                         it->second.params,
                                                         it->second.channelCount,
                                                         &it->second.nestedSchema);
            it->second.inputPorts = std::max(it->second.inputPorts, declaredPorts.inputs);
            it->second.outputPorts = std::max(it->second.outputPorts, declaredPorts.outputs);

            it->second.position = json_utils::pointValue(nodeObject, "position", it->second.position);
            it->second.hasExplicitPosition = true;
        }
    }

    if (const auto wiresIt = document.diagram.find("wires"); wiresIt != document.diagram.end() && wiresIt->value().is_array()) {
        for (const auto& wireValue : wiresIt->value().as_array()) {
            if (!wireValue.is_object()) {
                continue;
            }

            const auto& wireObject = wireValue.as_object();
            const auto* pointsValue = json_utils::findObjectValue(wireObject, "points");
            if (!pointsValue || !pointsValue->is_array()) {
                continue;
            }

            std::vector<QPointF> points;
            for (const auto& pointValue : pointsValue->as_array()) {
                if (!pointValue.is_object()) {
                    continue;
                }

                const auto& pointObject = pointValue.as_object();
                const auto* xValue = json_utils::findObjectValue(pointObject, "x");
                const auto* yValue = json_utils::findObjectValue(pointObject, "y");
                if (!xValue || !yValue) {
                    continue;
                }

                points.emplace_back(json_utils::numberValue(*xValue), json_utils::numberValue(*yValue));
            }

            if (points.size() >= 2) {
                if (json_utils::findObjectValue(wireObject, "from_element") && json_utils::findObjectValue(wireObject, "to_element")) {
                    keyedWireRoutes[routeKey(json_utils::stringValue(wireObject, "from_element", ""),
                                             json_utils::intValue(wireObject, "from_port", 0),
                                             json_utils::stringValue(wireObject, "to_element", ""),
                                             json_utils::intValue(wireObject, "to_port", 0))] = points;
                }
                diagramWireRoutes.push_back(std::move(points));
            }
        }
    }

    if (nodeOrder.empty()) {
        return;
    }

    // Without any stored position the whole diagram is laid out by data flow.
    const bool autoLayout = std::none_of(nodeOrder.begin(), nodeOrder.end(),
                                         [&](const std::string& id) { return nodesById.at(id).hasExplicitPosition; });
    std::optional<LayeredLayout> layered;
    if (autoLayout) {
        std::unordered_map<std::string, double> heights;
        for (const auto& nodeId : nodeOrder) {
            const auto& model = nodesById.at(nodeId);
            heights[nodeId] = nodeHeightFor(model.inputPorts, model.type == "Scope" ? 0 : model.outputPorts);
        }
        layered.emplace(nodeOrder, connections, heights);
        for (const auto& nodeId : nodeOrder) {
            nodesById.at(nodeId).position = layered->position(nodeId);
            nodesById.at(nodeId).hasExplicitPosition = true;
        }
    }

    int autoIndex = 0;
    const int autoColumnCount = std::max(1, static_cast<int>(std::ceil(std::sqrt(static_cast<double>(nodeOrder.size())))));
    for (const auto& nodeId : nodeOrder) {
        auto& node = nodesById.at(nodeId);
        if (node.hasExplicitPosition) {
            continue;
        }

        const int column = autoIndex % autoColumnCount;
        const int row = autoIndex / autoColumnCount;
        node.position = QPointF(column * xSpacing, row * ySpacing);
        ++autoIndex;
    }

    std::unordered_map<std::string, std::shared_ptr<QSchematic::Items::Node>> sceneNodes;
    std::unordered_map<std::string, NodeWiring> nodeWirings;
    sceneNodes.reserve(nodesById.size());
    nodeWirings.reserve(nodesById.size());

    for (const auto& nodeId : nodeOrder) {
        const auto& model = nodesById.at(nodeId);

        auto node = std::make_shared<QSchematic::Items::Node>();
        node->setPos(model.position);
        node->setAllowMouseRotate(false);
        node->setConnectorsMovable(false);

        NodeWiring wiring;

        if (model.type == "TextFrame") {
            const auto params = mergedTextFrameParams(model.params);
            const auto* widthValue = json_utils::findObjectValue(params, "width");
            const auto* heightValue = json_utils::findObjectValue(params, "height");
            const double frameWidth = std::max(80.0, json_utils::numberValue(widthValue ? *widthValue : boost::json::value{}, kDefaultTextFrameWidth));
            const double frameHeight = std::max(50.0, json_utils::numberValue(heightValue ? *heightValue : boost::json::value{}, kDefaultTextFrameHeight));

            node->setSize(frameWidth, frameHeight);
            auto* frameRect = new QGraphicsRectItem(0.0, 0.0, frameWidth, frameHeight, node.get());
            frameRect->setPen(QPen(QColor(QString::fromStdString(json_utils::stringValue(params, "line_color", "#444444"))), 1.4));
            frameRect->setBrush(QColor(QString::fromStdString(json_utils::stringValue(params, "fill_color", "#fff4cc"))));

            auto* label = new QGraphicsTextItem(QString::fromStdString(json_utils::stringValue(params, "text", "Text frame")), node.get());
            label->setDefaultTextColor(QColor(QString::fromStdString(json_utils::stringValue(params, "text_color", "#202020"))));
            QFont labelFont(QString::fromStdString(json_utils::stringValue(params, "font_family", "Sans Serif")));
            labelFont.setPointSize(std::max(6, json_utils::intValue(params, "font_size", 12)));
            label->setFont(labelFont);
            label->setTextWidth(frameWidth - 20.0);
            label->setPos(10.0, 8.0);

            node->setData(kNodeIdRole, QString::fromStdString(model.id));
        node->setData(kNodePlacementRole, model.placement);
            node->setData(kNodeTypeRole, QStringLiteral("TextFrame"));
            node->setData(kNodeTitleRole, model.title);
            node->setData(kNodeParamsRole, QString::fromStdString(boost::json::serialize(params)));
            node->setData(kTextFrameTextRole, QString::fromStdString(json_utils::stringValue(params, "text", "Text frame")));
            node->setData(kTextFrameTextColorRole, QString::fromStdString(json_utils::stringValue(params, "text_color", "#202020")));
            node->setData(kTextFrameLineColorRole, QString::fromStdString(json_utils::stringValue(params, "line_color", "#444444")));
            node->setData(kTextFrameFillColorRole, QString::fromStdString(json_utils::stringValue(params, "fill_color", "#fff4cc")));
            node->setData(kTextFrameFontFamilyRole, QString::fromStdString(json_utils::stringValue(params, "font_family", "Sans Serif")));
            node->setData(kTextFrameFontSizeRole, std::max(6, json_utils::intValue(params, "font_size", 12)));

            scene.addItem(node);
            sceneNodes.emplace(nodeId, std::move(node));
            nodeWirings.emplace(nodeId, std::move(wiring));
            continue;
        }

        const int shownOutputs = model.type == "Scope" ? 0 : std::max(model.outputPorts, 0);
        const double nodeHeight = nodeHeightFor(std::max(model.inputPorts, 0), shownOutputs);
        node->setSize(kDefaultNodeWidth, nodeHeight);

        const int inputPortCount = std::max(model.inputPorts, 0);
        for (int portIndex = 0; portIndex < inputPortCount; ++portIndex) {
            auto connector = makeStyledConnector();
            const double y = portOffsetY(nodeHeight, portIndex, inputPortCount);
            connector->setPos(0.0, y);
            node->addConnector(connector);
            wiring.inputs.push_back(connector);
        }

        const int outputPortCount = model.type == "Scope" ? 0 : std::max(model.outputPorts, 0);
        for (int portIndex = 0; portIndex < outputPortCount; ++portIndex) {
            auto connector = makeStyledConnector();
            connector->setPos(kDefaultNodeWidth, portOffsetY(nodeHeight, portIndex, outputPortCount));
            node->addConnector(connector);
            wiring.outputs.push_back(connector);
        }

        auto* label = new QGraphicsTextItem(QStringLiteral("%1\n%2").arg(model.title, QString::fromUtf8(model.type.c_str())), node.get());
        label->setDefaultTextColor(Qt::black);
        label->setTextWidth(kDefaultNodeWidth - 20.0);
        label->setPos(10.0, 10.0);

        node->setData(kNodeIdRole, QString::fromStdString(model.id));
        node->setData(kNodePlacementRole, model.placement);
        node->setData(kNodeTypeRole, QString::fromStdString(model.type));
        node->setData(kNodeTitleRole, model.title);
        node->setData(kNodeParamsRole, QString::fromStdString(boost::json::serialize(model.params)));
            if (model.nestedSchema.is_object()) {
            node->setData(kNodeSchemaRole, QString::fromStdString(boost::json::serialize(model.nestedSchema)));
        }
        node->setData(kNodeChannelCountRole, model.channelCount);

        scene.addItem(node);
        sceneNodes.emplace(nodeId, std::move(node));
        nodeWirings.emplace(nodeId, std::move(wiring));
    }

    if (!connections.empty()) {
        for (std::size_t connectionIndex = 0; connectionIndex < connections.size(); ++connectionIndex) {
            const auto& connection = connections[connectionIndex];
            const auto fromIt = sceneNodes.find(connection.fromElement);
            const auto toIt = sceneNodes.find(connection.toElement);
            const auto fromWiringIt = nodeWirings.find(connection.fromElement);
            const auto toWiringIt = nodeWirings.find(connection.toElement);
            if (fromIt == sceneNodes.end() || toIt == sceneNodes.end() ||
                fromWiringIt == nodeWirings.end() || toWiringIt == nodeWirings.end()) {
                continue;
            }

            const auto startConnector = connectorAtPort(fromWiringIt->second.outputs, connection.fromPort);
            const auto endConnector = connectorAtPort(toWiringIt->second.inputs, connection.toPort);
            if (!startConnector || !endConnector) {
                continue;
            }

            auto wire = std::make_shared<QSchematic::Items::Wire>();
            // Routes are matched by their connection; files without
            // connection names in routes fall back to the route's position.
            const std::vector<QPointF>* route = nullptr;
            if (!keyedWireRoutes.empty()) {
                const auto it = keyedWireRoutes.find(
                    routeKey(connection.fromElement, connection.fromPort, connection.toElement, connection.toPort));
                route = it != keyedWireRoutes.end() ? &it->second : nullptr;
            } else if (connectionIndex < diagramWireRoutes.size()) {
                route = &diagramWireRoutes[connectionIndex];
            }
            // A branch starts on another wire of the same output, not at the
            // output connector, and is attached at its input end only.
            bool branch = false;
            if (route && route->size() >= 2) {
                const auto& savedPoints = *route;
                branch = QLineF(savedPoints.front(), startConnector->scenePos()).length() > 1.0;
                wire->append_point(branch ? savedPoints.front() : startConnector->scenePos());
                for (std::size_t pointIndex = 1; pointIndex + 1 < savedPoints.size(); ++pointIndex) {
                    wire->append_point(savedPoints[pointIndex]);
                }
                wire->append_point(endConnector->scenePos());
            } else if (layered) {
                auto planned = layered->route(connection, startConnector->scenePos(), endConnector->scenePos());
                branch = planned.branch;
                for (const auto& point : planned.points) {
                    wire->append_point(point);
                }
            } else {
                appendOrthogonalWirePath(*wire, startConnector->scenePos(), endConnector->scenePos());
            }

            if (scene.addWire(wire)) {
                attachWireEndpoints(scene, wire, branch ? nullptr : startConnector, endConnector);
            }
        }

        if (const auto wireManager = scene.wire_manager()) {
            wireManager->generate_junctions();
        }

        return;
    }

    if (!diagramWireRoutes.empty()) {
        for (const auto& savedPoints : diagramWireRoutes) {
            auto wire = std::make_shared<QSchematic::Items::Wire>();
            for (const auto& point : savedPoints) {
                wire->append_point(point);
            }

            if (scene.addWire(wire)) {
                attachWireEndpointsByPosition(scene, wire);
            }
        }
    }

    if (const auto wireManager = scene.wire_manager()) {
        wireManager->generate_junctions();
    }
}


auto SceneSerializer::extractDiagramLayout(const QSchematic::Scene& scene, const std::vector<WireSource>& connected) const
    -> boost::json::object {
    boost::json::object diagram;
    diagram["version"] = 1;
    diagram["nodes"] = boost::json::array();
    diagram["wires"] = boost::json::array();
    
    auto& diagramNodes = diagram["nodes"].as_array();
    auto& diagramWires = diagram["wires"].as_array();
    
    // Add nodes with visual info (position, name)
    const auto nodes = sortedSceneNodes(scene);
    int nodeIndex = 0;
    
    for (const auto& nodePtr : nodes) {
        auto node = nodePtr.get();
        if (!node) continue;
        
        const auto storedNodeId = node->data(kNodeIdRole).toString();
        std::string nodeId = storedNodeId.isEmpty()
            ? "node_" + std::to_string(nodeIndex++)
            : storedNodeId.toStdString();
        const auto storedType = node->data(kNodeTypeRole).toString();
        const auto storedTitle = node->data(kNodeTitleRole).toString();
        const auto storedParams = node->data(kNodeParamsRole).toString();

        boost::json::object nodeObj;
        nodeObj["id"] = nodeId;
        const auto nodeType = storedType.isEmpty() ? QStringLiteral("Element") : storedType;
        nodeObj["type"] = nodeType.toStdString();
        nodeObj["name"] = storedTitle.isEmpty() ? nodeId : storedTitle.toStdString();
        if (nodeType == QStringLiteral("Scope")) {
            nodeObj["channel_count"] = std::max(1, node->data(kNodeChannelCountRole).toInt());
        } else if (nodeType == QStringLiteral("TextFrame")) {
            auto params = mergedTextFrameParams(parseEncodedParams(storedParams));
            params["width"] = node->size().width();
            params["height"] = node->size().height();
            nodeObj["params"] = params;
        }

        boost::json::object posObj;
        posObj["x"] = node->pos().x();
        posObj["y"] = node->pos().y();
        nodeObj["position"] = posObj;
        
        diagramNodes.push_back(nodeObj);
    }
    
    // Routes of the connected wires; each names its connection, so a route
    // can never be attached to another connection (wires that connect
    // nothing are not part of the schema and are not saved).
    int wireIndex = 0;
    for (const auto& source : connected) {
        if (!source.wire) {
            continue;
        }
        boost::json::object wireObj;
        wireObj["id"] = "wire_" + std::to_string(wireIndex++);
        wireObj["from_element"] = source.element;
        wireObj["from_port"] = source.port;
        wireObj["to_element"] = source.toElement;
        wireObj["to_port"] = source.toPort;
        // Absolute scene coordinates, so endpoint attachments can be restored reliably.
        boost::json::array points;
        for (const auto& point : source.wire->pointsAbsolute()) {
            points.push_back(boost::json::object{{"x", point.x()}, {"y", point.y()}});
        }
        wireObj["points"] = std::move(points);
        diagramWires.push_back(std::move(wireObj));
    }

    return diagram;
}

} // namespace s7studio::diagram

