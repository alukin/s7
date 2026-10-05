#pragma once

#include <QString>

#include <vector>

namespace QSchematic {
class Scene;
}

namespace s7studio::diagram {

struct WireTypeLabel {
    QString element;
    int port = 0;
    QString type; // resolved type name, or "?" when the diagram's types do not resolve
};

// Resolves the scene's signal types (the same inference the runtime uses) and
// shows each wire net's type as its label. Returns the label of every
// connected wire; `error` receives the first type error, if any.
auto updateWireTypeLabels(QSchematic::Scene& scene, QString* error = nullptr) -> std::vector<WireTypeLabel>;

} // namespace s7studio::diagram
