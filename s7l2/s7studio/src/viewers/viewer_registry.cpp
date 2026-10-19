#include "viewers/viewer_registry.hpp"

#include "phase_portrait_view_window.hpp"
#include "phase_trajectory_3d_view_window.hpp"
#include "scope_view_window.hpp"

#include <utility>

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
    registry.add(ui::ScopeViewWindow::viewerType());
    registry.add(ui::PhasePortraitViewWindow::viewerType());
    registry.add(ui::PhaseTrajectory3DViewWindow::viewerType());
}

} // namespace s7studio::viewers
