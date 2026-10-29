// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-S7-plugin-permission

#include "diagram/wire_context_menu.hpp"

#include "diagram/diagram_geometry.hpp"
#include "diagram/diagram_scene.hpp"
#include "diagram/net_model.hpp"

#include <QLineF>
#include <QMenu>

#include <algorithm>

namespace s7studio::diagram {

void showWireContextMenu(DiagramScene& scene, SignalWire& wire, const QPointF scenePos, const QPoint screenPos)
{
    const auto shared = scene.sharedWire(&wire);
    if (!shared) {
        return;
    }
    const auto route = wire.pointsAbsolute();
    QMenu menu;
    auto* add = menu.addAction(QStringLiteral("Add Bend Here"));
    QAction* remove = nullptr;
    int nearBend = -1;
    for (int index = 0; index < wire.userBends.size(); ++index) {
        if (QLineF(wire.userBends[index], scenePos).length() <= kWireHandleHit + 2.0) {
            nearBend = index;
        }
    }
    if (nearBend >= 0) {
        remove = menu.addAction(QStringLiteral("Remove Bend"));
    }
    QAction* reroute = nullptr;
    if (!wire.userBends.isEmpty()) {
        reroute = menu.addAction(QStringLiteral("Re-route (Remove Hand-Placed Bends)"));
    }
    menu.addSeparator();
    // A probe taps the net's driving output (the same output every wire of
    // this net shares), not this particular wire segment (doc/
    // S7_Numerical_Modeling_Test_Plan.md section 1.1: a probe reads an
    // element's output port, never a graph element of its own).
    const auto driver = NetModel::driverOf(wire);
    QAction* addProbeAction = nullptr;
    QAction* removeProbeAction = nullptr;
    if (driver) {
        const bool hasProbe = std::ranges::any_of(scene.probes(), [&](const auto& probe) { return probe.point == *driver; });
        addProbeAction = hasProbe ? nullptr : menu.addAction(QStringLiteral("Add Probe Here"));
        removeProbeAction = hasProbe ? menu.addAction(QStringLiteral("Remove Probe")) : nullptr;
    }
    menu.addSeparator();
    auto* erase = menu.addAction(QStringLiteral("Delete Wire"));
    const auto* chosen = menu.exec(screenPos);
    if (chosen == add) {
        const QPointF point = nearestOnRoute(scenePos, route);
        int segment = 0;
        for (int index = 0; index + 1 < route.size(); ++index) {
            if (onSegment(point, route[index], route[index + 1])) {
                segment = index;
                break;
            }
        }
        auto bends = wire.userBends;
        bends.insert(bendsBefore(route, bends, segment + 1), point);
        scene.setUserBends(shared, bends);
    } else if (remove && chosen == remove) {
        auto bends = wire.userBends;
        bends.removeAt(nearBend);
        scene.setUserBends(shared, bends);
    } else if (reroute && chosen == reroute) {
        scene.setUserBends(shared, {});
    } else if (addProbeAction && chosen == addProbeAction) {
        scene.addProbe(*driver, QStringLiteral("%1_%2").arg(QString::fromStdString(driver->element)).arg(driver->port));
    } else if (removeProbeAction && chosen == removeProbeAction) {
        scene.removeProbe(*driver);
    } else if (chosen == erase) {
        scene.deleteWire(shared);
    }
}

} // namespace s7studio::diagram
