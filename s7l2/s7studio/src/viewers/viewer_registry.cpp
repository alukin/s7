// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-S7-plugin-permission

#include "viewers/viewer_registry.hpp"

#include "phase_portrait_view_window.hpp"
#include "phase_trajectory_3d_view_window.hpp"
#include "scope_view_window.hpp"
#include "live_oscilloscope_window.hpp"

#include <utility>
#include <s7/metadata_json.hpp>
#include "observer_metadata.hpp"

namespace s7studio::viewers {

auto ViewerRegistry::instance() -> ViewerRegistry&
{
    static ViewerRegistry registry = [] {
        ViewerRegistry builtins;
        registerBuiltinViewers(builtins);
        return builtins;
    }();
    return registry;
}

void ViewerRegistry::add(ViewerType type)
{
    for (auto& existing : types_) {
        if (existing.typeId == type.typeId) {
            existing = std::move(type);
            return;
        }
    }
    types_.push_back(std::move(type));
}

auto ViewerRegistry::find(const QString& typeId) const -> const ViewerType*
{
    for (const auto& type : types_) {
        if (type.typeId == typeId) return &type;
    }
    return nullptr;
}

void registerBuiltinViewers(ViewerRegistry& registry)
{
    static const s7::ElementMetadataLibrary metadata(s7::resources::observer_metadata);
    const auto add = [&registry](ViewerType type) {
        type.documentation = metadata.at(type.typeId.toStdString());
        type.displayName = QString::fromStdString(type.documentation->display_name);
        type.description = QString::fromStdString(type.documentation->description);
        registry.add(std::move(type));
    };
    add(ui::ScopeViewWindow::viewerType());
    add(ui::LiveOscilloscopeWindow::viewerType());
    add(ui::PhasePortraitViewWindow::viewerType());
    add(ui::PhaseTrajectory3DViewWindow::viewerType());
}

} // namespace s7studio::viewers
