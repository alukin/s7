#pragma once

// Studio's side of deploying a diagram to runtime nodes
// (s7/diagram_deployment.hpp, s7/deployment_client.hpp).

#include "diagram_document.hpp"

#include <s7/diagram_deployment.hpp>

#include <QMap>
#include <QString>

#include <boost/json.hpp>

namespace QSchematic {
class Scene;
}

namespace s7studio::diagram {

struct NodeAddress {
    QString name;
    QString endpoint;
};

// The document as it is deployed: `runtime_nodes` lists every node an
// element is placed on, with the name and endpoint from `known` (keyed by
// node_uuid) where available, otherwise as the document had them.
[[nodiscard]] auto deploymentDiagram(const DiagramDocument& document, const QMap<QString, NodeAddress>& known) -> boost::json::object;

// Output port types by inference over the diagram's schema, built as Studio
// builds it (Studio-only elements such as Scope included).
[[nodiscard]] auto studioOutputTypes(const boost::json::object& diagram) -> s7::deployment::OutputTypeLookup;

// Shows "@ <node name>" under each placed element (names keyed by node_uuid).
void updatePlacementLabels(QSchematic::Scene& scene, const QMap<QString, QString>& nodeNames);

} // namespace s7studio::diagram
