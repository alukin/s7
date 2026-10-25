// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-S7-plugin-permission

#include "diagram_document.hpp"

#include <stdexcept>
#include <cmath>
#include <set>

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

void validateObserversPanel(const boost::json::value& panel) {
    if (!panel.is_array()) throw std::invalid_argument("diagram.observers_panel must be an array");
    std::set<std::string> identities;
    for (const auto& value : panel.as_array()) {
        if (!value.is_object()) throw std::invalid_argument("Observer selection must be an object");
        const auto& entry = value.as_object();
        const auto* id = entry.if_contains("element_id"), *type = entry.if_contains("type"), *collapsed = entry.if_contains("collapsed");
        if (!id || !id->is_string() || id->as_string().empty() || !type || !type->is_string() || type->as_string().empty() || !collapsed || !collapsed->is_bool())
            throw std::invalid_argument("Observer selection needs element_id, type and collapsed");
        if (!identities.insert(std::string(type->as_string()) + "\n" + std::string(id->as_string())).second)
            throw std::invalid_argument("Duplicate observer selection");
        if (const auto* channels = entry.if_contains("plotted_channels")) {
            if (!channels->is_array() || channels->as_array().size() > 8) throw std::invalid_argument("Select at most eight preview channels");
            std::set<std::int64_t> seen;
            for (const auto& channel : channels->as_array()) if (!channel.is_int64() || channel.as_int64() < 0 || channel.as_int64() > 1000000 || !seen.insert(channel.as_int64()).second)
                throw std::invalid_argument("Preview channel indices must be distinct nonnegative integers");
        }
    }
}
auto DiagramDocument::fromJson(const boost::json::value& json) -> DiagramDocument {
    if (!json.is_object()) {
        throw std::invalid_argument("Expected object for DiagramDocument");
    }
    
    const auto& root = json.as_object();
    DiagramDocument doc;
    if (const auto* description = root.if_contains("description")) {
        if (!description->is_string()) throw std::invalid_argument("description must be a string");
        doc.description = QString::fromStdString(std::string(description->as_string()));
    }
    
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
    
    if (auto it = root.find("diagram_version"); it != root.end() && it->value().is_number()) {
        doc.diagramVersion = static_cast<int>(it->value().to_number<double>());
    }

    if (auto it = root.find("diagram"); it != root.end() && it->value().is_object()) {
        doc.diagram = it->value().as_object();
        if (const auto* panel = doc.diagram.if_contains("observers_panel")) validateObserversPanel(*panel);
    }
    if (auto it = root.find("runtime_nodes"); it != root.end() && it->value().is_array()) {
        doc.runtimeNodes = it->value().as_array();
    }
    if (auto it = root.find("streams"); it != root.end() && it->value().is_array()) {
        doc.streams = it->value().as_array();
    }
    if (auto it = root.find("probes"); it != root.end() && it->value().is_array()) {
        for (const auto& value : it->value().as_array()) {
            if (!value.is_object()) {
                continue;
            }
            const auto& object = value.as_object();
            DiagramProbe probe;
            if (const auto* element = object.if_contains("element"); element && element->is_string()) {
                probe.point.element = element->as_string().c_str();
            }
            if (const auto* port = object.if_contains("port"); port && port->is_number()) {
                probe.point.port = static_cast<int>(port->to_number<double>());
            }
            if (const auto* name = object.if_contains("name"); name && name->is_string()) {
                probe.name = QString::fromStdString(name->as_string().c_str());
            }
            if (const auto* unit = object.if_contains("unit"); unit && unit->is_string()) {
                probe.unit = QString::fromStdString(unit->as_string().c_str());
            }
            doc.probes.push_back(std::move(probe));
        }
    }

    if (const auto* config = root.if_contains("studio_run")) {
        if (!config->is_object()) throw std::invalid_argument("studio_run must be an object");
        doc.runConfiguration = config->as_object();
        if (const auto* seconds = doc.runConfiguration.if_contains("runtime_seconds")) {
            if (!seconds->is_number())
                throw std::invalid_argument("studio_run.runtime_seconds must be a finite number in [0, 604800], or -1 for continuous execution");
            const double duration = seconds->to_number<double>();
            if (!std::isfinite(duration) || (duration < 0 && duration != -1) || duration > 604800)
                throw std::invalid_argument("studio_run.runtime_seconds must be a finite number in [0, 604800], or -1 for continuous execution");
        }
        if (const auto* clear = doc.runConfiguration.if_contains("clear_scope_traces"); clear && !clear->is_bool())
            throw std::invalid_argument("studio_run.clear_scope_traces must be boolean");
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
    root["description"] = description.toStdString();
    root["dt"] = dt;
    root["document_kind"] = "s7.diagram";
    root["diagram_version"] = kDiagramVersion;
    root["sheet"] = pageLayout.toJson();
    if (!runConfiguration.empty()) root["studio_run"] = runConfiguration;
    
    if (runtimeSchema.is_object()) {
        root["schema"] = runtimeSchema;
    }
    
    if (!diagram.empty()) {
        root["diagram"] = diagram;
    }
    if (!runtimeNodes.empty()) {
        root["runtime_nodes"] = runtimeNodes;
    }
    if (!streams.empty()) {
        root["streams"] = streams;
    }
    if (!probes.empty()) {
        boost::json::array probesJson;
        for (const auto& probe : probes) {
            probesJson.push_back(boost::json::object{
                {"element", probe.point.element},
                {"port", probe.point.port},
                {"name", probe.name.toStdString()},
                {"unit", probe.unit.toStdString()},
            });
        }
        root["probes"] = std::move(probesJson);
    }

    return root;
}

} // namespace s7studio::diagram
