// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-S7-plugin-permission

#include "diagram/wire_removal_planner.hpp"

#include "diagram/net_model.hpp"

#include <algorithm>
#include <cmath>
#include <unordered_set>
#include <utility>

namespace s7studio::diagram {

WireRemovalPlanner::WireRemovalPlanner(std::vector<std::shared_ptr<SignalWire>> wires)
    : wires_(std::move(wires))
{
}

auto WireRemovalPlanner::plan(DiagramChange change, std::vector<std::shared_ptr<SignalWire>> requested) const -> DiagramChange
{
    std::unordered_set<const QSchematic::Items::Item*> removedNodes;
    for (const auto& node : change.removedNodes) {
        removedNodes.insert(node.get());
    }
    std::unordered_set<const SignalWire*> removed;
    for (const auto& wire : requested) {
        removed.insert(wire.get());
    }
    // Wires into removed elements, and every wire of a net whose driver is removed.
    for (const auto& wire : wires_) {
        const auto target = wire->downstream.node.lock();
        const auto* root = NetModel::rootOf(*wire);
        const auto driver = root ? root->upstream.node.lock() : nullptr;
        if ((target && removedNodes.contains(target.get())) || (driver && removedNodes.contains(driver.get()))) {
            removed.insert(wire.get());
        }
    }
    for (const auto& wire : wires_) {
        if (removed.contains(wire.get())) {
            change.removedWires.push_back(wire);
        }
    }

    // Wires hanging on removed wires move up to the nearest remaining wire,
    // or become the first wire of their output.
    std::vector<std::shared_ptr<SignalWire>> survivors;
    for (const auto& wire : wires_) {
        if (!removed.contains(wire.get()) && wire->upstream.isJunction()) {
            const auto trunk = wire->upstream.wire.lock();
            if (trunk && removed.contains(trunk.get())) {
                survivors.push_back(wire);
            }
        }
    }
    std::stable_sort(survivors.begin(), survivors.end(),
                     [](const auto& a, const auto& b) { return NetModel::depthOf(*a) < NetModel::depthOf(*b); });
    std::vector<std::pair<WireEnd, std::shared_ptr<SignalWire>>> newTrunks;
    for (const auto& wire : survivors) {
        WireEnd end = wire->upstream;
        while (end.isJunction()) {
            const auto trunk = end.wire.lock();
            if (!trunk || !removed.contains(trunk.get())) {
                break;
            }
            end = trunk->upstream;
        }
        if (end.isOutput()) {
            std::shared_ptr<SignalWire> trunk;
            for (const auto& candidate : wires_) {
                if (!removed.contains(candidate.get()) && candidate != wire && candidate->upstream.sameAs(end)) {
                    trunk = candidate;
                }
            }
            for (const auto& [port, taken] : newTrunks) {
                if (port.sameAs(end)) {
                    trunk = taken;
                }
            }
            if (trunk) {
                const auto port = end.port();
                const QPointF at = port ? port->scenePos() : QPointF();
                end = WireEnd::onWire(trunk, {
                    std::round(at.x() / kGridStep) * kGridStep,
                    std::round(at.y() / kGridStep) * kGridStep});
            } else {
                newTrunks.emplace_back(end, wire);
            }
        }
        change.rehung.push_back({wire, wire->upstream, end});
    }
    return change;
}

} // namespace s7studio::diagram
