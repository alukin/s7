#include "scene_serializer.hpp"

#include <qschematic/items/node.hpp>
#include <qschematic/items/connector.hpp>
#include <qschematic/items/wire.hpp>
#include <qschematic/scene.hpp>

#include <unordered_map>
#include <unordered_set>
#include <sstream>

namespace s7studio::diagram {

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
    // TODO: Implement deserialization to populate QSchematic scene
    // This would create nodes and wires based on diagram layout
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
    const auto nodes = scene.items<QSchematic::Items::Node>();
    std::unordered_map<QSchematic::Items::Node*, std::string> nodeIdMap;
    
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
    }
    
    // Extract connections from wires
    // For now, we'll create a simple structure
    // Full implementation would need access to wire start/end connector info
    const auto wires = scene.items<QSchematic::Items::Wire>();
    
    for (const auto& wirePtr : wires) {
        auto wire = wirePtr.get();
        if (!wire) continue;
        
        // Extract wire endpoints - this is simplified
        // Real implementation would query connectors attached to wire
        boost::json::object connObj;
        if (!nodeIdMap.empty()) {
            connObj["from_element"] = nodeIdMap.begin()->second;
            connObj["from_port"] = 0;
            connObj["to_element"] = (++nodeIdMap.begin() != nodeIdMap.end()) ? (++nodeIdMap.begin())->second : nodeIdMap.begin()->second;
            connObj["to_port"] = 0;
        } else {
            connObj["from_element"] = "unknown";
            connObj["from_port"] = 0;
            connObj["to_element"] = "unknown";
            connObj["to_port"] = 0;
        }
        
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
    const auto nodes = scene.items<QSchematic::Items::Node>();
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
        
        // Get wire points (relative coordinates)
        const auto& pathPoints = wire->pointsRelative();
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

