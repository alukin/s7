#include "scene_serializer.hpp"
#include "diagram_node_roles.hpp"
#include "json_utils.hpp"
#include "runtime_schema_extractor.hpp"

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
#include <unordered_map>
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
    QString title;
    QPointF position;
    bool hasExplicitPosition = false;
    int inputPorts = 0;
    int outputPorts = 0;
    int channelCount = 1;
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
    colorize->setColor(QColor("#8fd3ff"));
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

auto declaredPortCounts(std::string_view type,
                        const boost::json::object& params = {},
                        const int fallbackChannelCount = 1) -> PortCounts
{
    if (type == "Constant" || type == "AnalogOscillator") {
        return {0, 1};
    }

    if (type == "Gain" ||
        type == "Delay" ||
        type == "Integrator" ||
        type == "FirFilter" ||
        type == "IirFilter" ||
        type == "PreviousValue" ||
        type == "Int12ToDouble" ||
        type == "DoubleToInt12") {
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

    return {};
}

auto portOffsetY(const double nodeHeight, const int portIndex, const int portCount) -> double
{
    const int effectivePortCount = std::max(portCount, 1);
    return (nodeHeight * static_cast<double>(portIndex + 1)) / static_cast<double>(effectivePortCount + 1);
}

auto scopeInputPortOffsetY(const double nodeHeight, const int portIndex, const int portCount) -> double
{
    if (portIndex <= 0 || portCount <= 1) {
        return nodeHeight / 2.0;
    }

    constexpr double kMargin = 12.0;
    const int extraPorts = std::max(1, portCount - 1);
    const double centerY = nodeHeight / 2.0;
    const double maxY = std::max(centerY, nodeHeight - kMargin);
    const double lowerSpan = std::max(0.0, maxY - centerY);
    const double step = lowerSpan / static_cast<double>(extraPorts);
    const double y = centerY + (step * static_cast<double>(portIndex));
    return std::clamp(y, centerY, maxY);
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
        doc.runtimeSchema = extractor.extract(scene, options);
        doc.diagram = extractDiagramLayout(scene);
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
                if (const auto* paramsValue = json_utils::findObjectValue(element, "params"); paramsValue && paramsValue->is_object()) {
                    model.params = paramsValue->as_object();
                }
                if (model.type == "Scope") {
                    model.channelCount = std::max(1, json_utils::intValue(model.params, "channel_count", 1));
                }
                const auto declaredPorts = declaredPortCounts(model.type, model.params, model.channelCount);
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
            if (it->second.type == "Scope") {
                it->second.channelCount = std::max(1, json_utils::intValue(nodeObject, "channel_count", it->second.channelCount));
            }
            const auto declaredPorts = declaredPortCounts(it->second.type, it->second.params, it->second.channelCount);
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

        node->setSize(kDefaultNodeWidth, kDefaultNodeHeight);

        const int inputPortCount = std::max(model.inputPorts, 0);
        for (int portIndex = 0; portIndex < inputPortCount; ++portIndex) {
            auto connector = makeStyledConnector();
            const double y = (model.type == "Scope")
                ? scopeInputPortOffsetY(kDefaultNodeHeight, portIndex, inputPortCount)
                : portOffsetY(kDefaultNodeHeight, portIndex, inputPortCount);
            connector->setPos(0.0, y);
            node->addConnector(connector);
            wiring.inputs.push_back(connector);
        }

        const int outputPortCount = model.type == "Scope" ? 0 : std::max(model.outputPorts, 0);
        for (int portIndex = 0; portIndex < outputPortCount; ++portIndex) {
            auto connector = makeStyledConnector();
            connector->setPos(kDefaultNodeWidth, portOffsetY(kDefaultNodeHeight, portIndex, outputPortCount));
            node->addConnector(connector);
            wiring.outputs.push_back(connector);
        }

        auto* label = new QGraphicsTextItem(QStringLiteral("%1\n%2").arg(model.title, QString::fromUtf8(model.type.c_str())), node.get());
        label->setDefaultTextColor(Qt::black);
        label->setTextWidth(kDefaultNodeWidth - 20.0);
        label->setPos(10.0, 10.0);

        node->setData(kNodeIdRole, QString::fromStdString(model.id));
        node->setData(kNodeTypeRole, QString::fromStdString(model.type));
        node->setData(kNodeTitleRole, model.title);
        node->setData(kNodeParamsRole, QString::fromStdString(boost::json::serialize(model.params)));
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

