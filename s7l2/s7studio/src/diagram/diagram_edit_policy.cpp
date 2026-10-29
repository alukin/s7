// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-S7-plugin-permission

#include "diagram/diagram_edit_policy.hpp"

#include "diagram/element_ports.hpp"

#include <algorithm>
#include <string>
#include <utility>

namespace s7studio::diagram {

auto completeDiagramChange(DiagramChange change, const std::vector<DiagramProbe>& probes,
                           const boost::json::array& observersPanel) -> DiagramChange
{
    if (change.removedNodes.empty() && change.elements.empty()) {
        return change;
    }
    if (!change.observersPanel) {
        auto remaining = observersPanel;
        remaining.erase(std::remove_if(remaining.begin(), remaining.end(), [&](const boost::json::value& value) {
            const auto& entry = value.as_object();
            const auto id = std::string(entry.at("element_id").as_string());
            for (const auto& node : change.removedNodes) {
                if (const auto element = std::dynamic_pointer_cast<ElementNode>(node); element && element->id() == id) {
                    return true;
                }
            }
            for (const auto& edit : change.elements) {
                if (edit.before.id == id && (edit.after.id != id || edit.after.type != std::string(entry.at("type").as_string()))) {
                    return true;
                }
            }
            return false;
        }), remaining.end());
        if (remaining != observersPanel) {
            change.observersPanel = std::pair{observersPanel, std::move(remaining)};
        }
    }
    if (!change.probes) {
        auto remaining = probes;
        std::erase_if(remaining, [&](const DiagramProbe& probe) {
            for (const auto& node : change.removedNodes) {
                if (const auto element = std::dynamic_pointer_cast<ElementNode>(node); element && element->id() == probe.point.element) {
                    return true;
                }
            }
            for (const auto& edit : change.elements) {
                if (edit.before.id == probe.point.element) {
                    const auto ports = elementPorts(edit.after.type, edit.after.params, edit.after.schema);
                    if (edit.after.id != edit.before.id ||
                        probe.point.port >= std::max(static_cast<int>(ports.outputs.size()), edit.node->minimumOutputs())) {
                        return true;
                    }
                }
            }
            return false;
        });
        if (remaining != probes) {
            change.probes = std::pair{probes, std::move(remaining)};
        }
    }
    return change;
}

} // namespace s7studio::diagram
