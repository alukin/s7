#pragma once

#include <QString>
#include <boost/json.hpp>

namespace s7studio::diagram {

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
    DiagramPageLayout pageLayout;
    boost::json::value runtimeSchema;  // The embedded runtime schema (elements + connections)
    boost::json::object diagram;       // Visual layout: nodes (positions, names), wires (routing points)

    static auto fromJson(const boost::json::value& json) -> DiagramDocument;
    auto toJson() const -> boost::json::value;
};

} // namespace s7studio::diagram
