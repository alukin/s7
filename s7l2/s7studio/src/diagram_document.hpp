// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-S7-plugin-permission

#pragma once

#include "diagram/net_model.hpp"

#include <QString>
#include <boost/json.hpp>

#include <vector>

namespace s7studio::diagram {

// Version of the `diagram` layout this Studio reads and writes (diagram_format.hpp).
constexpr int kDiagramVersion = 2;

// Page layout for diagram printing/display
enum class PageOrientation {
    Landscape,
    Portrait
};

struct DiagramPageLayout {
    PageOrientation orientation = PageOrientation::Landscape;
    double widthMm = 297.0;   // A4 landscape default
    double heightMm = 210.0;

    static auto fromJson(const boost::json::object& obj) -> DiagramPageLayout;
    auto toJson() const -> boost::json::object;
};

// Diagram document: extends runtime schema with visual layout metadata
struct DiagramDocument {
    QString id = QStringLiteral("s7studio_diagram");
    double dt = 0.001;                // Time step from runtime schema
    int diagramVersion = 0;           // `diagram_version` of the file, 0 if absent
    DiagramPageLayout pageLayout;
    boost::json::value runtimeSchema;  // The embedded runtime schema (elements + connections)
    boost::json::object diagram;       // Layout: element positions, wires, text frames (empty: none)
    // Deployment (s7/diagram_deployment.hpp): the nodes elements are placed on
    // and the streams between them, kept so redeployments continue them.
    boost::json::array runtimeNodes;
    boost::json::array streams;
    boost::json::object runConfiguration; // Studio local run settings, outside the functional graph
    // Observation points (DiagramProbe, ADR-011/012): not part of the
    // Functional Graph, so not inside `schema`.
    std::vector<DiagramProbe> probes;

    static auto fromJson(const boost::json::value& json) -> DiagramDocument;
    auto toJson() const -> boost::json::value;
};

} // namespace s7studio::diagram
