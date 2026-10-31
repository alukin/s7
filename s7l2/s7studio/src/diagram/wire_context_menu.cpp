// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-S7-plugin-permission

#include "diagram/wire_context_menu.hpp"

#include "diagram/diagram_geometry.hpp"
#include "diagram/diagram_scene.hpp"
#include "diagram/net_model.hpp"

#include <QGraphicsView>
#include <QInputDialog>
#include <QLineEdit>
#include <QLineF>
#include <QMenu>
#include <QMessageBox>

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
    QAction* renameProbeAction = nullptr;
    QAction* removeProbeAction = nullptr;
    QString probeName;
    if (driver) {
        const auto probe = std::ranges::find_if(scene.probes(), [&](const auto& candidate) { return candidate.point == *driver; });
        const bool hasProbe = probe != scene.probes().end();
        if (hasProbe) {
            probeName = probe->name;
        }
        addProbeAction = hasProbe ? nullptr : menu.addAction(QStringLiteral("Add Probe Here"));
        renameProbeAction = hasProbe ? menu.addAction(QStringLiteral("Rename Probe...")) : nullptr;
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
    } else if (renameProbeAction && chosen == renameProbeAction) {
        // Same dialog and rule as Recorded Signals > Rename...: an empty name is ignored.
        auto* parent = scene.views().isEmpty() ? nullptr : scene.views().front();
        bool accepted = false;
        const auto name = QInputDialog::getText(parent, QStringLiteral("Rename Probe"), QStringLiteral("Name"),
                                                QLineEdit::Normal, probeName, &accepted);
        if (accepted && !name.isEmpty() && !scene.renameProbe(*driver, name)) {
            QMessageBox::warning(parent, QStringLiteral("Rename Probe"),
                                 QStringLiteral("A probe named \u201C%1\u201D already exists.").arg(name));
        }
    } else if (removeProbeAction && chosen == removeProbeAction) {
        scene.removeProbe(*driver);
    } else if (chosen == erase) {
        scene.deleteWire(shared);
    }
}

} // namespace s7studio::diagram
