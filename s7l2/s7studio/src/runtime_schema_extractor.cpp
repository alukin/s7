#include "runtime_schema_extractor.hpp"

#include "diagram_node_roles.hpp"
#include "scene_serializer.hpp"

#include <qschematic/items/connector.hpp>
#include <qschematic/items/node.hpp>
#include <qschematic/items/wire.hpp>
#include <qschematic/scene.hpp>
#include <qschematic/wire_system/wire.hpp>

#include <QString>

#include <algorithm>
#include <QVector>
#include <QLineF>
#include <functional>
#include <boost/system/error_code.hpp>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace s7studio::diagram {

namespace {

struct ConnectorEndpointInfo {
    std::string nodeId;
    int portIndex = 0;
    bool isOutput = false;
};

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

auto orderedNodeConnectors(const std::shared_ptr<QSchematic::Items::Node>& node,
                           const bool outputs) -> std::vector<std::shared_ptr<QSchematic::Items::Connector>>
{
    std::vector<std::shared_ptr<QSchematic::Items::Connector>> ordered;
    if (!node) {
        return ordered;
    }

    const auto splitX = node->size().width() / 2.0;
    for (const auto& connector : node->connectors()) {
        if (!connector) {
            continue;
        }

        const bool isOutput = connector->pos().x() > splitX;
        if (isOutput == outputs) {
            ordered.push_back(connector);
        }
    }

    std::sort(ordered.begin(), ordered.end(), [](const auto& lhs, const auto& rhs) {
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

auto parseParamsFromNode(const QSchematic::Items::Node& node) -> boost::json::object
{
    const auto encoded = node.data(kNodeParamsRole).toString();
    if (encoded.isEmpty()) {
        return {};
    }

    boost::system::error_code ec;
    const auto parsed = boost::json::parse(encoded.toStdString(), ec);
    if (ec || !parsed.is_object()) {
        return {};
    }
    return parsed.as_object();
}

auto parseNestedSchemaFromNode(const QSchematic::Items::Node& node) -> boost::json::value
{
    const auto encoded = node.data(kNodeSchemaRole).toString();
    if (encoded.isEmpty()) {
        return {};
    }

    boost::system::error_code ec;
    const auto parsed = boost::json::parse(encoded.toStdString(), ec);
    if (ec) {
        return {};
    }

    return parsed;
}

} // namespace

auto RuntimeSchemaExtractor::extract(const QSchematic::Scene& scene,
                                     const SerializationOptions& options,
                                     std::vector<WireSource>* wireSources) const -> boost::json::value
{
    boost::json::object root;
    root["name"] = options.schemaName.toStdString();
    root["elements"] = boost::json::array();
    root["connections"] = boost::json::array();

    auto& elementsArray = root["elements"].as_array();
    auto& connectionsArray = root["connections"].as_array();

    const auto nodes = sortedSceneNodes(scene);
    std::unordered_map<const QSchematic::Items::Connector*, ConnectorEndpointInfo> connectorInfo;

    int nodeIndex = 0;
    for (const auto& nodePtr : nodes) {
        auto node = nodePtr.get();
        if (!node) {
            continue;
        }

        const auto storedId = node->data(kNodeIdRole).toString();
        const std::string nodeId = storedId.isEmpty()
            ? "node_" + std::to_string(nodeIndex++)
            : storedId.toStdString();
        const auto storedType = node->data(kNodeTypeRole).toString();
        if (storedType == QStringLiteral("TextFrame")) {
            continue;
        }

        boost::json::object params = parseParamsFromNode(*node);
        if (storedType == "Scope") {
            const auto storedChannelCount = node->data(kNodeChannelCountRole).toInt();
            if (storedChannelCount > 0) {
                params["channel_count"] = storedChannelCount;
            }
        }

        boost::json::object elementObj;
        elementObj["id"] = nodeId;
        elementObj["type"] = storedType.isEmpty() ? "Element" : storedType.toStdString();
        elementObj["params"] = params;
        if (const auto placement = node->data(kNodePlacementRole).toString(); !placement.isEmpty()) {
            elementObj["node"] = placement.toStdString();
        }
        const auto nestedSchema = parseNestedSchemaFromNode(*node);
        if (nestedSchema.is_object()) {
            elementObj["schema"] = nestedSchema;
        }
        elementsArray.push_back(elementObj);

        const auto inputs = orderedNodeConnectors(nodePtr, false);
        for (int portIndex = 0; portIndex < static_cast<int>(inputs.size()); ++portIndex) {
            connectorInfo.emplace(inputs[portIndex].get(), ConnectorEndpointInfo{nodeId, portIndex, false});
        }

        const auto outputs = orderedNodeConnectors(nodePtr, true);
        for (int portIndex = 0; portIndex < static_cast<int>(outputs.size()); ++portIndex) {
            connectorInfo.emplace(outputs[portIndex].get(), ConnectorEndpointInfo{nodeId, portIndex, true});
        }
    }

    // Connections come from wire nets. QSchematic attaches one wire per
    // connector, so an output feeding several inputs is one wire from the
    // output with further wires branching off it at junctions. A wire end
    // belongs to a connector it is attached to, or lies on; wires are in one
    // net when an end of one lies on the other. A net with one output
    // connects it to every input the net reaches.
    const auto wires = scene.items<QSchematic::Items::Wire>();
    const auto wireManager = scene.wire_manager();
    struct WireEnds {
        QSchematic::Items::Wire* wire{};
        QVector<QPointF> points;
        std::vector<ConnectorEndpointInfo> outputs;
        std::vector<ConnectorEndpointInfo> inputs;
    };
    std::vector<WireEnds> ends;
    for (const auto& wirePtr : wires) {
        if (!wirePtr || wirePtr->pointsAbsolute().size() < 2) {
            continue;
        }
        WireEnds entry{wirePtr.get(), wirePtr->pointsAbsolute(), {}, {}};
        const QPointF first = entry.points.front();
        const QPointF last = entry.points.back();
        for (const auto& [connector, endpoint] : connectorInfo) {
            bool attached = false;
            if (wireManager) {
                if (const auto record = wireManager->attached_wire(connector); record && record->wire == wirePtr.get()) {
                    attached = true;
                }
            }
            const auto position = connector->scenePos();
            const bool atEnd = QLineF(position, first).length() < 1.0 || QLineF(position, last).length() < 1.0;
            if (attached || atEnd) {
                (endpoint.isOutput ? entry.outputs : entry.inputs).push_back(endpoint);
            }
        }
        ends.push_back(std::move(entry));
    }

    // Union of wires joined at junctions.
    std::vector<std::size_t> parent(ends.size());
    for (std::size_t index = 0; index < parent.size(); ++index) {
        parent[index] = index;
    }
    std::function<std::size_t(std::size_t)> root_of = [&](std::size_t index) {
        while (parent[index] != index) {
            index = parent[index] = parent[parent[index]];
        }
        return index;
    };
    auto onWire = [](const QPointF& point, const QVector<QPointF>& points) {
        for (int index = 0; index + 1 < points.size(); ++index) {
            const QLineF segment(points[index], points[index + 1]);
            const QPointF d = segment.p2() - segment.p1();
            const double length2 = d.x() * d.x() + d.y() * d.y();
            const double t = length2 > 0 ? std::clamp(QPointF::dotProduct(point - segment.p1(), d) / length2, 0.0, 1.0) : 0.0;
            if (QLineF(point, segment.p1() + t * d).length() < 0.5) {
                return true;
            }
        }
        return false;
    };
    for (std::size_t a = 0; a < ends.size(); ++a) {
        for (std::size_t b = 0; b < ends.size(); ++b) {
            if (a != b && (onWire(ends[a].points.front(), ends[b].points) || onWire(ends[a].points.back(), ends[b].points))) {
                parent[root_of(a)] = root_of(b);
            }
        }
    }
    std::unordered_map<std::size_t, std::vector<ConnectorEndpointInfo>> netOutputs;
    for (std::size_t index = 0; index < ends.size(); ++index) {
        auto& outputs = netOutputs[root_of(index)];
        for (const auto& output : ends[index].outputs) {
            const bool known = std::any_of(outputs.begin(), outputs.end(), [&](const auto& o) {
                return o.nodeId == output.nodeId && o.portIndex == output.portIndex;
            });
            if (!known) {
                outputs.push_back(output);
            }
        }
    }

    auto addConnection = [&](QSchematic::Items::Wire* wire, const ConnectorEndpointInfo& source, const ConnectorEndpointInfo& target) {
        if (wireSources) {
            wireSources->push_back(WireSource{wire, source.nodeId, source.portIndex, target.nodeId, target.portIndex});
        }
        boost::json::object connObj;
        connObj["from_element"] = source.nodeId;
        connObj["from_port"] = source.portIndex;
        connObj["to_element"] = target.nodeId;
        connObj["to_port"] = target.portIndex;
        connectionsArray.push_back(connObj);
    };
    for (std::size_t index = 0; index < ends.size(); ++index) {
        const auto& outputs = netOutputs[root_of(index)];
        for (const auto& target : ends[index].inputs) {
            if (outputs.size() == 1) {
                addConnection(ends[index].wire, outputs.front(), target);
            } else if (ends[index].outputs.size() == 1) {
                // Several outputs meet in one net (a drawing error): only the
                // wire's own ends count.
                addConnection(ends[index].wire, ends[index].outputs.front(), target);
            }
        }
    }

    return root;
}

} // namespace s7studio::diagram
