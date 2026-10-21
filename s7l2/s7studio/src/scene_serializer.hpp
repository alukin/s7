// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-S7-plugin-permission

#pragma once

#include "diagram_document.hpp"

#include <QString>

#include <functional>

namespace s7studio::diagram {

class DiagramScene;

struct SerializationOptions {
    QString schemaName;
    double dt = 0.001;
    bool includeDiagram = true;
    PageOrientation sheetOrientation = PageOrientation::Landscape;
    double sheetWidthMm = 297.0;
    double sheetHeightMm = 210.0;
};

// Diagram documents (format version 2, diagram_format.hpp) <-> DiagramScene.
class SceneSerializer {
public:
    [[nodiscard]] auto serialize(const DiagramScene& scene, const SerializationOptions& options) const -> DiagramDocument;

    // Replaces the scene's content. A document without a layout (a plain
    // runtime schema) is laid out by data flow and wired automatically, as
    // is a layout without `wires`. Throws std::runtime_error when the layout
    // does not match the schema.
    void deserialize(const DiagramDocument& document, DiagramScene& scene, const std::function<void(int)>& progress = {}) const;
};

} // namespace s7studio::diagram
