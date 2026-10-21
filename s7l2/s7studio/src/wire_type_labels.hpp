// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-S7-plugin-permission

#pragma once

#include <QString>

#include <vector>

namespace s7studio::diagram {

class DiagramScene;

struct WireTypeLabel {
    QString element;
    int port = 0;
    QString type; // resolved type name, or "?" when the diagram's types do not resolve
};

// Resolves the scene's signal types (the same inference the runtime uses),
// labels the first wire of every net with its type and marks wires that
// connect nothing. Returns the label of every connection (by its output);
// `error` receives the first type error, if any.
auto updateWireTypeLabels(DiagramScene& scene, QString* error = nullptr) -> std::vector<WireTypeLabel>;

} // namespace s7studio::diagram
