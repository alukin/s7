#pragma once

#include "diagram_document.hpp"
#include <boost/json.hpp>
#include <QString>

#include <vector>

namespace QSchematic {
class Scene;
}

namespace s7studio::diagram {

struct WireSource;

struct SerializationOptions {
    QString schemaName;
    double dt = 0.001;
    bool includeDiagram = true;
    PageOrientation sheetOrientation = PageOrientation::Landscape;
    double sheetWidthMm = 297.0;
    double sheetHeightMm = 210.0;
};

// Serializes QSchematic scene to s7studio diagram JSON format
class SceneSerializer {
public:
    // Convert scene to diagram document (with visual layout info)
    [[nodiscard]] auto serialize(const QSchematic::Scene& scene,
                                 const SerializationOptions& options) const -> DiagramDocument;
    
    // Convert diagram to QSchematic scene (deserialize)
    void deserialize(const DiagramDocument& document,
                     QSchematic::Scene& scene) const;

private:
    // Node positions, and the route of every connected wire (with the
    // connection it belongs to) in the order of `connected`.
    [[nodiscard]] auto extractDiagramLayout(const QSchematic::Scene& scene, const std::vector<WireSource>& connected) const
        -> boost::json::object;
};

} // namespace s7studio::diagram
