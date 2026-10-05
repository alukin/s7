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

    struct WireEndpoints {
        std::optional<ConnectorEndpointInfo> source;
        std::optional<ConnectorEndpointInfo> target;
    };

    std::unordered_map<const wire_system::wire*, WireEndpoints> wireEndpoints;
    if (const auto wireManager = scene.wire_manager()) {
        for (const auto& [connector, endpoint] : connectorInfo) {
            const auto connection = wireManager->attached_wire(connector);
            if (!connection || !connection->wire) {
                continue;
            }

            auto& endpoints = wireEndpoints[connection->wire];
            if (endpoint.isOutput) {
                endpoints.source = endpoint;
            } else {
                endpoints.target = endpoint;
            }
        }
    }

    const auto wires = scene.items<QSchematic::Items::Wire>();
    for (const auto& wirePtr : wires) {
        auto wire = wirePtr.get();
        if (!wire) {
            continue;
        }

        const auto endpointsIt = wireEndpoints.find(wire);
        if (endpointsIt == wireEndpoints.end() || !endpointsIt->second.source || !endpointsIt->second.target) {
            continue;
        }

        const auto& source = *endpointsIt->second.source;
        const auto& target = *endpointsIt->second.target;

        if (wireSources) {
            wireSources->push_back(WireSource{wire, source.nodeId, source.portIndex});
        }

        boost::json::object connObj;
        connObj["from_element"] = source.nodeId;
        connObj["from_port"] = source.portIndex;
        connObj["to_element"] = target.nodeId;
        connObj["to_port"] = target.portIndex;
        connectionsArray.push_back(connObj);
    }

    return root;
}

} // namespace s7studio::diagram
