#include "scene_serializer.hpp"

#include "elements/basic_node_item.hpp"
#include "s7ui_json_dto.hpp"
#include "scene_roles.hpp"

#include <qschematic/items/connector.hpp>
#include <qschematic/items/node.hpp>
#include <qschematic/items/wire.hpp>
#include <qschematic/scene.hpp>

#include <unordered_map>
#include <unordered_set>

namespace s7ui::ui {
namespace {

using ConnectorEndpoint = s7ui::dto::ConnectorEndpoint;

auto connectorIsOutput(const QString& connectorName) -> bool
{
    return connectorName.startsWith("out");
}

} // namespace

boost::json::value SceneSerializer::serialize(const QSchematic::Scene& scene,
                                              const SceneSerializationOptions& options) const
{
    boost::json::object root;
    root["schema_version"] = 1;
    root["id"] = options.schemaName.toStdString();
    root["dt"] = options.dt;
    root["nodes"] = boost::json::array();
    root["nets"] = boost::json::array();
    root["wires"] = boost::json::array();

    if (options.includeDiagram)
    {
        root["diagram_version"] = 1;
        root["units"] = "mm";
        root["sheet"] = boost::json::object{
            {"standard", options.sheetStandard.toStdString()},
            {"orientation", "landscape"},
            {"width_mm", options.sheetWidthMm},
            {"height_mm", options.sheetHeightMm}
        };
        root["diagram"] = boost::json::object{{"version", 1},
                                                {"nodes", boost::json::array()},
                                                {"nets", boost::json::array()},
                                                {"wires", boost::json::array()}};
    }

    auto& rootNodes = root["nodes"].as_array();
    auto& rootNets = root["nets"].as_array();
    auto& rootWires = root["wires"].as_array();

    boost::json::array* diagramNodes = nullptr;
    boost::json::array* diagramNets = nullptr;
    boost::json::array* diagramWires = nullptr;
    if (options.includeDiagram)
    {
        auto& diagram = root["diagram"].as_object();
        diagramNodes = &diagram["nodes"].as_array();
        diagramNets = &diagram["nets"].as_array();
        diagramWires = &diagram["wires"].as_array();
    }

    auto nodeItems = scene.items<QSchematic::Items::Node>();
    std::unordered_map<const QSchematic::Items::Connector*, ConnectorEndpoint> runtimeEndpointByConnector;
    const auto wireManager = scene.wire_manager();

    for (const auto& node : nodeItems)
    {
        if (!node) continue;

        const bool isVisualSink = node->data(kRoleVisualSink).toBool();
        const QString nodeId = node->data(kRoleNodeId).toString();
        const QString typeName = node->data(kRoleNodeType).toString();
        if (nodeId.isEmpty() || typeName.isEmpty()) continue;

        boost::json::object nodeJson;
        nodeJson["id"] = nodeId.toStdString();

        if (isVisualSink)
        {
            int inputCount = 0;
            for (const auto& connector : node->connectors())
            {
                if (!connectorIsOutput(connector->text())) ++inputCount;
            }
            nodeJson["type"] = (typeName == "ComplexScopeSink") ? "ComplexProbe" : "Probe";
            nodeJson["params"] = boost::json::object{{"channels", std::max(1, inputCount)}};
        }
        else
        {
            nodeJson["type"] = typeName.toStdString();
            QString params = "{}";
            if (const auto basic = std::dynamic_pointer_cast<s7ui::elements::BasicNodeItem>(node))
            {
                params = basic->paramsJson();
            }
            try
            {
                nodeJson["params"] = boost::json::parse(params.toStdString());
            }
            catch (...)
            {
                nodeJson["params"] = boost::json::object();
            }
        }

        rootNodes.push_back(nodeJson);

        if (diagramNodes)
        {
            boost::json::object diagramNode = nodeJson;
            diagramNode["type"] = typeName.toStdString();
            diagramNode["name"] = node->data(kRoleNodeName).toString().toStdString();
            diagramNode["visual"] = isVisualSink;
            diagramNode["position"] = s7ui::dto::JsonPoint::toJson(node->scenePos());

            int inPorts = 0;
            int outPorts = 0;
            for (const auto& connector : node->connectors())
            {
                if (connectorIsOutput(connector->text())) ++outPorts;
                else ++inPorts;
            }
            diagramNode["input_ports"] = inPorts;
            diagramNode["output_ports"] = outPorts;
            diagramNode["ports"] = boost::json::array();
            for (const auto& connector : node->connectors())
            {
                const bool out = connectorIsOutput(connector->text());
                diagramNode["ports"].as_array().push_back(boost::json::object{
                    {"name", connector->text().toStdString()},
                    {"direction", out ? "output" : "input"},
                    {"signal_type", "any"},
                    {"domain", "stream"}
                });
            }
            diagramNodes->push_back(std::move(diagramNode));
        }

        for (const auto& connector : node->connectors())
        {
            runtimeEndpointByConnector[connector.get()] = ConnectorEndpoint{.nodeId = nodeId, .connectorName = connector->text()};
        }
    }

    auto wireItems = scene.items<QSchematic::Items::Wire>();
    for (const auto& wire : wireItems)
    {
        if (!wire) continue;

        ConnectorEndpoint from;
        ConnectorEndpoint to;
        bool fromSet = false;
        bool toSet = false;

        for (const auto& [connectorPtr, endpoint] : runtimeEndpointByConnector)
        {
            const auto attached = wireManager ? wireManager->attached_wire(connectorPtr) : std::nullopt;
            if (!attached || attached->wire != wire.get()) continue;

            if (connectorIsOutput(endpoint.connectorName) && !fromSet)
            {
                from = endpoint;
                fromSet = true;
            }
            else if (!connectorIsOutput(endpoint.connectorName) && !toSet)
            {
                to = endpoint;
                toSet = true;
            }
        }

        if (!fromSet || !toSet) continue;

        boost::json::object wireJson;
        wireJson["from"] = boost::json::object{{"node", from.nodeId.toStdString()}, {"connector", from.connectorName.toStdString()}};
        wireJson["to"] = boost::json::object{{"node", to.nodeId.toStdString()}, {"connector", to.connectorName.toStdString()}};
        wireJson["points"] = boost::json::array();
        for (const auto& point : wire->pointsAbsolute())
        {
            wireJson["points"].as_array().push_back(s7ui::dto::JsonPoint::toJson(point));
        }

        rootWires.push_back(wireJson);
        if (diagramWires) diagramWires->push_back(std::move(wireJson));
    }

    if (wireManager)
    {
        for (const auto& net : wireManager->nets())
        {
            std::unordered_set<const wire_system::wire*> netWires;
            for (const auto& wire : net->wires()) netWires.insert(wire.get());

            boost::json::array connectionsJson;
            int outputs = 0;
            int inputs = 0;

            for (const auto& [connectorPtr, endpoint] : runtimeEndpointByConnector)
            {
                const auto attached = wireManager->attached_wire(connectorPtr);
                if (!attached || !attached->wire || !netWires.contains(attached->wire)) continue;

                connectionsJson.push_back(boost::json::object{{"node", endpoint.nodeId.toStdString()},
                                                              {"connector", endpoint.connectorName.toStdString()}});

                if (connectorIsOutput(endpoint.connectorName)) ++outputs;
                else ++inputs;
            }

            QString netName = net->name();
            if (netName.trimmed().isEmpty()) netName = QString("net_%1").arg(rootNets.size());

            if (outputs == 1 && inputs >= 1)
            {
                boost::json::object netJson{{"name", netName.toStdString()}, {"connections", std::move(connectionsJson)}};
                rootNets.push_back(netJson);
                if (diagramNets) diagramNets->push_back(std::move(netJson));
            }
        }
    }

    return root;
}

} // namespace s7ui::ui

