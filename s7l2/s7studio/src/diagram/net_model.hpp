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

// The connections of a diagram, read from its wire trees: every wire ending
// at an input connects the output driving its tree to that input.
class NetModel
{
public:
    explicit NetModel(const DiagramScene& scene);

    // In element order (scene order of the elements, then ports).
    [[nodiscard]] const std::vector<NetConnection>& connections() const { return connections_; }
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
    std::vector<std::pair<SignalWire*, QString>> problems_;
};

} // namespace s7studio::diagram
