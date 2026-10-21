// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-S7-plugin-permission

#pragma once

#include "viewers/observation_viewer.hpp"

#include <QList>

namespace s7studio::viewers {

// The viewer types Studio can open. Built-in types are registered on first
// use of instance(); a new viewer type adds itself with add() and needs no
// change to the host (MainWindow, ViewerManager or the element picker).
class ViewerRegistry {
public:
    static auto instance() -> ViewerRegistry&;

    // Replaces a type registered under the same typeId.
    void add(ViewerType type);
    [[nodiscard]] auto find(const QString& typeId) const -> const ViewerType*;
    [[nodiscard]] auto types() const -> const QList<ViewerType>& { return types_; }

private:
    QList<ViewerType> types_;
};

// Scope, Phase Portrait and 3D Phase Trajectory.
void registerBuiltinViewers(ViewerRegistry& registry);

} // namespace s7studio::viewers
