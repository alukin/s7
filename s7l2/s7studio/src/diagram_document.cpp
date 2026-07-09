#include "diagram_document.hpp"

#include <stdexcept>

namespace s7studio::diagram {

auto DiagramPageLayout::fromJson(const boost::json::object& obj) -> DiagramPageLayout {
    DiagramPageLayout layout;
    
    if (auto it = obj.find("orientation"); it != obj.end() && it->value().is_string()) {
        const auto orientStr = it->value().as_string();
        if (orientStr == "portrait") {
            layout.orientation = PageOrientation::Portrait;
        }
    }
    
    if (auto it = obj.find("width_mm"); it != obj.end() && it->value().is_double()) {
        layout.widthMm = it->value().as_double();
    } else if (auto it = obj.find("width_mm"); it != obj.end() && it->value().is_int64()) {
        layout.widthMm = static_cast<double>(it->value().as_int64());
    }
    
    if (auto it = obj.find("height_mm"); it != obj.end() && it->value().is_double()) {
        layout.heightMm = it->value().as_double();
    } else if (auto it = obj.find("height_mm"); it != obj.end() && it->value().is_int64()) {
        layout.heightMm = static_cast<double>(it->value().as_int64());
    }
    
    return layout;
}

auto DiagramPageLayout::toJson() const -> boost::json::object {
    boost::json::object obj;
    obj["orientation"] = (orientation == PageOrientation::Portrait) ? "portrait" : "landscape";
    obj["width_mm"] = widthMm;
    obj["height_mm"] = heightMm;
    return obj;
}

auto DiagramDocument::fromJson(const boost::json::value& json) -> DiagramDocument {
    if (!json.is_object()) {
        throw std::invalid_argument("Expected object for DiagramDocument");
    }
    
    const auto& root = json.as_object();
    DiagramDocument doc;
    
    if (auto it = root.find("id"); it != root.end() && it->value().is_string()) {
        doc.id = QString::fromStdString(it->value().as_string().c_str());
    }
    
    if (auto it = root.find("dt"); it != root.end() && it->value().is_double()) {
        doc.dt = it->value().as_double();
    } else if (auto it = root.find("dt"); it != root.end() && it->value().is_int64()) {
        doc.dt = static_cast<double>(it->value().as_int64());
    }
    
    if (auto it = root.find("sheet"); it != root.end() && it->value().is_object()) {
        doc.pageLayout = DiagramPageLayout::fromJson(it->value().as_object());
    }
    
    if (auto it = root.find("diagram"); it != root.end() && it->value().is_object()) {
        doc.diagram = it->value().as_object();
    }
    
    // Store the runtime schema (or reconstruct from diagram if needed)
    if (auto it = root.find("schema"); it != root.end()) {
        doc.runtimeSchema = it->value();
    } else {
        // If no explicit schema field, treat the document itself as containing runtime data
        doc.runtimeSchema = json;
    }
    
    return doc;
}

auto DiagramDocument::toJson() const -> boost::json::value {
    boost::json::object root;
    root["id"] = id.toStdString();
    root["dt"] = dt;
    root["document_kind"] = "s7.diagram";
    root["diagram_magic"] = "S7STUDIO_DIAGRAM_V1";
    root["diagram_version"] = 1;
    root["units"] = "mm";
    root["sheet"] = pageLayout.toJson();
    
    if (runtimeSchema.is_object()) {
        root["schema"] = runtimeSchema;
    }
    
    if (!diagram.empty()) {
        root["diagram"] = diagram;
    }
    
    return root;
}

} // namespace s7studio::diagram
