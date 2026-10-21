// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-S7-plugin-permission

#pragma once

#include <boost/json.hpp>

#include <QString>

#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace s7studio::diagram {

class DiagramScene;
class SignalWire;

struct PortRef {
    std::string element;
    int port = 0;

    bool operator==(const PortRef&) const = default;
};

struct NetConnection {
    PortRef from;
    PortRef to;
    SignalWire* wire = nullptr; // the wire ending at `to`
};

// A read-only tap on an existing element's output port (doc/S7_Numerical_Modeling_Test_Plan.md,
// S7_Architectural_Decisions.md ADR-011/012: observation points are not
// Functional Graph elements). Identity is `point` (stable across saves and
// re-routing); `name`/`unit` are display-only.
struct DiagramProbe {
    PortRef point;
    QString name;
    QString unit;

    bool operator==(const DiagramProbe&) const = default;
};

// The connections of a diagram, read from its wire trees: every wire ending
// at an input connects the output driving its tree to that input.
class NetModel
{
public:
    explicit NetModel(const DiagramScene& scene);

    // In element order (scene order of the elements, then ports).
    [[nodiscard]] const std::vector<NetConnection>& connections() const { return connections_; }
    // Observation-only links from functional outputs into Studio observer inputs.
    [[nodiscard]] const std::vector<NetConnection>& observationConnections() const { return observationConnections_; }
    // Wires that connect nothing valid, and why.
    [[nodiscard]] const std::vector<std::pair<SignalWire*, QString>>& problems() const { return problems_; }

    // The first wire of the wire's tree (its upstream end is the driving
    // output), or null when the chain of junctions is broken.
    [[nodiscard]] static SignalWire* rootOf(const SignalWire& wire);
    // The output driving the wire's net, if any.
    [[nodiscard]] static std::optional<PortRef> driverOf(const SignalWire& wire);
    // Junction hops from the wire to its root.
    [[nodiscard]] static int depthOf(const SignalWire& wire);

    // Runtime schema of the scene: elements (text frames left out) and connections.
    [[nodiscard]] auto schemaJson(const std::string& name) const -> boost::json::object;

private:
    const DiagramScene& scene_;
    std::vector<NetConnection> connections_;
    std::vector<NetConnection> observationConnections_;
    std::vector<std::pair<SignalWire*, QString>> problems_;
};

} // namespace s7studio::diagram
