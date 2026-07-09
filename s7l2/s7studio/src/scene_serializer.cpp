#include "scene_serializer.hpp"

#include <qschematic/items/node.hpp>
#include <qschematic/items/connector.hpp>
#include <qschematic/items/wire.hpp>
#include <qschematic/scene.hpp>

#include <QGraphicsTextItem>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>
#include <unordered_map>
#include <vector>

namespace s7studio::diagram {

namespace {

struct NodeModel {
    std::string id;
    std::string type = "Element";
    QString title;
    QPointF position;
    bool hasExplicitPosition = false;
    int inputPorts = 0;
    int outputPorts = 0;
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

struct ConnectorEndpointInfo {
    std::string nodeId;
    int portIndex = 0;
    bool isOutput = false;
};

auto findObjectValue(const boost::json::object& object, const char* key) -> const boost::json::value*
{
    if (const auto it = object.find(key); it != object.end()) {
        return &it->value();
    }

    return nullptr;
}

auto jsonString(const boost::json::object& object, const char* key, const std::string& fallback = {}) -> std::string
{
    if (const auto* value = findObjectValue(object, key); value && value->is_string()) {
        return std::string(value->as_string().c_str());
    }

    return fallback;
}

auto jsonNumber(const boost::json::value& value, const double fallback = 0.0) -> double
{
    if (value.is_double()) {
        return value.as_double();
    }
    if (value.is_int64()) {
        return static_cast<double>(value.as_int64());
    }
    if (value.is_uint64()) {
        return static_cast<double>(value.as_uint64());
    }

    return fallback;
}

auto jsonInt(const boost::json::object& object, const char* key, const int fallback = 0) -> int
{
    if (const auto* value = findObjectValue(object, key)) {
        return static_cast<int>(jsonNumber(*value, fallback));
    }

    return fallback;
}

auto jsonPoint(const boost::json::object& object, const char* key, QPointF fallback = {}) -> QPointF
{
    const auto* value = findObjectValue(object, key);
    if (!value || !value->is_object()) {
        return fallback;
    }

    const auto& pointObject = value->as_object();
    const auto* xValue = findObjectValue(pointObject, "x");
    const auto* yValue = findObjectValue(pointObject, "y");
    if (!xValue || !yValue) {
        return fallback;
    }

    return QPointF(jsonNumber(*xValue), jsonNumber(*yValue));
}

auto portOffsetY(const double nodeHeight, const int portIndex, const int portCount) -> double
{
    const int effectivePortCount = std::max(portCount, 1);
    return (nodeHeight * static_cast<double>(portIndex + 1)) / static_cast<double>(effectivePortCount + 1);
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
        doc.runtimeSchema = extractRuntimeSchema(scene, options);
        doc.diagram = extractDiagramLayout(scene);
    }
    
    return doc;
}

void SceneSerializer::deserialize(const DiagramDocument& document,
                                  QSchematic::Scene& scene) const {
    scene.clear();

    constexpr double nodeWidth = 180.0;
    constexpr double nodeHeight = 100.0;
    constexpr double xSpacing = 260.0;
    constexpr double ySpacing = 180.0;

    std::unordered_map<std::string, NodeModel> nodesById;
    std::vector<std::string> nodeOrder;
    std::vector<ConnectionModel> connections;
    std::vector<std::vector<QPointF>> diagramWireRoutes;

    if (document.runtimeSchema.is_object()) {
        const auto& schemaObject = document.runtimeSchema.as_object();

        if (const auto* elementsValue = findObjectValue(schemaObject, "elements"); elementsValue && elementsValue->is_array()) {
            int elementIndex = 0;
            for (const auto& elementValue : elementsValue->as_array()) {
                if (!elementValue.is_object()) {
                    continue;
                }

                const auto& element = elementValue.as_object();
                NodeModel model;
                model.id = jsonString(element, "id", "element_" + std::to_string(elementIndex++));
                model.type = jsonString(element, "type", "Element");
                model.title = QString::fromUtf8(model.id.c_str());

                nodeOrder.push_back(model.id);
                nodesById.emplace(model.id, std::move(model));
            }
        }

        if (const auto* connectionsValue = findObjectValue(schemaObject, "connections"); connectionsValue && connectionsValue->is_array()) {
            for (const auto& connectionValue : connectionsValue->as_array()) {
                if (!connectionValue.is_object()) {
                    continue;
                }

                const auto& connectionObject = connectionValue.as_object();
                ConnectionModel model;
                model.fromElement = jsonString(connectionObject, "from_element");
                model.fromPort = jsonInt(connectionObject, "from_port");
                model.toElement = jsonString(connectionObject, "to_element");
                model.toPort = jsonInt(connectionObject, "to_port");
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
            const auto id = jsonString(nodeObject, "id", "node_" + std::to_string(fallbackIndex++));

            auto [it, inserted] = nodesById.try_emplace(id);
            if (inserted) {
                it->second.id = id;
                it->second.type = jsonString(nodeObject, "type", "Element");
                it->second.title = QString::fromUtf8(id.c_str());
                nodeOrder.push_back(id);
            }

            if (const auto name = jsonString(nodeObject, "name"); !name.empty()) {
                it->second.title = QString::fromUtf8(name.c_str());
            }
            if (const auto type = jsonString(nodeObject, "type"); !type.empty()) {
                it->second.type = type;
            }

            it->second.position = jsonPoint(nodeObject, "position", it->second.position);
            it->second.hasExplicitPosition = true;
        }
    }

    if (const auto wiresIt = document.diagram.find("wires"); wiresIt != document.diagram.end() && wiresIt->value().is_array()) {
        for (const auto& wireValue : wiresIt->value().as_array()) {
            if (!wireValue.is_object()) {
                continue;
            }

            const auto& wireObject = wireValue.as_object();
            const auto* pointsValue = findObjectValue(wireObject, "points");
            if (!pointsValue || !pointsValue->is_array()) {
                continue;
            }

            std::vector<QPointF> points;
            for (const auto& pointValue : pointsValue->as_array()) {
                if (!pointValue.is_object()) {
                    continue;
                }

                const auto& pointObject = pointValue.as_object();
                points.emplace_back(
                    jsonNumber(*findObjectValue(pointObject, "x")),
                    jsonNumber(*findObjectValue(pointObject, "y")));
            }

            if (points.size() >= 2) {
                diagramWireRoutes.push_back(std::move(points));
            }
        }
    }

    if (nodeOrder.empty()) {
        return;
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
        node->setSize(nodeWidth, nodeHeight);
        node->setPos(model.position);
        node->setAllowMouseRotate(false);
        node->setConnectorsMovable(false);

        NodeWiring wiring;

        const int inputPortCount = std::max(model.inputPorts, 1);
        for (int portIndex = 0; portIndex < inputPortCount; ++portIndex) {
            auto connector = std::make_shared<QSchematic::Items::Connector>();
            connector->setPos(0.0, portOffsetY(nodeHeight, portIndex, inputPortCount));
            node->addConnector(connector);
            wiring.inputs.push_back(connector);
        }

        const int outputPortCount = std::max(model.outputPorts, 1);
        for (int portIndex = 0; portIndex < outputPortCount; ++portIndex) {
            auto connector = std::make_shared<QSchematic::Items::Connector>();
            connector->setPos(nodeWidth, portOffsetY(nodeHeight, portIndex, outputPortCount));
            node->addConnector(connector);
            wiring.outputs.push_back(connector);
        }

        auto* label = new QGraphicsTextItem(QStringLiteral("%1\n%2").arg(model.title, QString::fromUtf8(model.type.c_str())), node.get());
        label->setDefaultTextColor(Qt::black);
        label->setTextWidth(nodeWidth - 20.0);
        label->setPos(10.0, 10.0);

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
            if (connectionIndex < diagramWireRoutes.size() && diagramWireRoutes[connectionIndex].size() >= 2) {
                wire->append_point(startConnector->scenePos());
                const auto& savedPoints = diagramWireRoutes[connectionIndex];
                for (std::size_t pointIndex = 1; pointIndex + 1 < savedPoints.size(); ++pointIndex) {
                    wire->append_point(savedPoints[pointIndex]);
                }
                wire->append_point(endConnector->scenePos());
            } else {
                appendOrthogonalWirePath(*wire, startConnector->scenePos(), endConnector->scenePos());
            }

            if (scene.addWire(wire)) {
                attachWireEndpoints(scene, wire, startConnector, endConnector);
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

auto SceneSerializer::extractRuntimeSchema(const QSchematic::Scene& scene,
                                          const SerializationOptions& options) const -> boost::json::value {
    boost::json::object root;
    root["name"] = options.schemaName.toStdString();
    root["elements"] = boost::json::array();
    root["connections"] = boost::json::array();
    
    auto& elementsArray = root["elements"].as_array();
    auto& connectionsArray = root["connections"].as_array();
    
    // Extract nodes as elements
    const auto nodes = sortedSceneNodes(scene);
    std::unordered_map<QSchematic::Items::Node*, std::string> nodeIdMap;
    std::unordered_map<const QSchematic::Items::Connector*, ConnectorEndpointInfo> connectorInfo;

    int nodeIndex = 0;
    for (const auto& nodePtr : nodes) {
        auto node = nodePtr.get();
        if (!node) continue;
        
        // Generate node ID
        std::string nodeId = "node_" + std::to_string(nodeIndex++);
        nodeIdMap[node] = nodeId;
        
        boost::json::object elementObj;
        elementObj["id"] = nodeId;
        elementObj["type"] = "Element";  // Generic type; would need node-specific metadata
        
        boost::json::object params;
        elementObj["params"] = params;
        
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
        if (!wire) continue;

        const auto endpointsIt = wireEndpoints.find(wire);
        if (endpointsIt == wireEndpoints.end() || !endpointsIt->second.source || !endpointsIt->second.target) {
            continue;
        }

        const auto& source = *endpointsIt->second.source;
        const auto& target = *endpointsIt->second.target;

        boost::json::object connObj;
        connObj["from_element"] = source.nodeId;
        connObj["from_port"] = source.portIndex;
        connObj["to_element"] = target.nodeId;
        connObj["to_port"] = target.portIndex;
        connectionsArray.push_back(connObj);
    }
    
    return root;
}

auto SceneSerializer::extractDiagramLayout(const QSchematic::Scene& scene) const -> boost::json::object {
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
        
        std::string nodeId = "node_" + std::to_string(nodeIndex++);
        
        boost::json::object nodeObj;
        nodeObj["id"] = nodeId;
        nodeObj["type"] = "Element";
        nodeObj["name"] = nodeId;
        
        boost::json::object posObj;
        posObj["x"] = node->pos().x();
        posObj["y"] = node->pos().y();
        nodeObj["position"] = posObj;
        
        diagramNodes.push_back(nodeObj);
    }
    
    // Add wires with routing points
    const auto wires = scene.items<QSchematic::Items::Wire>();
    int wireIndex = 0;
    
    for (const auto& wirePtr : wires) {
        auto wire = wirePtr.get();
        if (!wire) continue;
        
        boost::json::object wireObj;
        wireObj["id"] = "wire_" + std::to_string(wireIndex++);
        
        // Extract routing points
        wireObj["points"] = boost::json::array();
        auto& points = wireObj["points"].as_array();
        
        // Get wire points in absolute scene coordinates so endpoint attachments can be restored reliably.
        const auto& pathPoints = wire->pointsAbsolute();
        for (const auto& point : pathPoints) {
            boost::json::object pointObj;
            pointObj["x"] = point.x();
            pointObj["y"] = point.y();
            points.push_back(pointObj);
        }
        
        diagramWires.push_back(wireObj);
    }
    
    return diagram;
}

} // namespace s7studio::diagram

