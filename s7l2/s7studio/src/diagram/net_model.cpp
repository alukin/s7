#include "diagram/net_model.hpp"

#include "diagram/diagram_scene.hpp"
#include "diagram/items.hpp"

#include <algorithm>
#include <map>
#include <unordered_map>

namespace s7studio::diagram {

namespace {

constexpr int kMaxDepth = 256; // junction chains longer than this are broken (a cycle)

auto portName(const WireEnd& end) -> QString
{
    const auto node = end.node.lock();
    const auto port = end.port();
    return QStringLiteral("%1.%2").arg(node ? node->title() : QStringLiteral("?"), port ? port->name() : QString::number(end.index));
}

} // namespace

SignalWire* NetModel::rootOf(const SignalWire& wire)
{
    const SignalWire* current = &wire;
    for (int depth = 0; depth < kMaxDepth; ++depth) {
        if (current->upstream.isPort()) {
            return const_cast<SignalWire*>(current);
        }
        if (!current->upstream.isJunction()) {
            return nullptr;
        }
        const auto next = current->upstream.wire.lock();
        if (!next || next.get() == &wire) {
            return nullptr;
        }
        current = next.get();
    }
    return nullptr;
}

std::optional<PortRef> NetModel::driverOf(const SignalWire& wire)
{
    const auto* root = rootOf(wire);
    if (!root || !root->upstream.isOutput()) {
        return std::nullopt;
    }
    const auto node = root->upstream.node.lock();
    if (!node || !root->upstream.port()) {
        return std::nullopt;
    }
    return PortRef{node->id(), root->upstream.index};
}

int NetModel::depthOf(const SignalWire& wire)
{
    int depth = 0;
    const SignalWire* current = &wire;
    while (current->upstream.isJunction() && depth < kMaxDepth) {
        const auto next = current->upstream.wire.lock();
        if (!next) {
            break;
        }
        current = next.get();
        ++depth;
    }
    return depth;
}

NetModel::NetModel(const DiagramScene& scene)
    : scene_(scene)
{
    std::unordered_map<std::string, std::size_t> order;
    for (const auto& node : scene.elementNodes()) {
        order.emplace(node->id(), order.size());
    }
    std::map<std::pair<std::string, int>, SignalWire*> fedInputs;
    for (const auto& wire : scene.signalWires()) {
        const auto driver = driverOf(*wire);
        if (!driver) {
            problems_.emplace_back(wire.get(), QStringLiteral("The wire is not connected to an output."));
            continue;
        }
        if (!wire->downstream.isInput() || !wire->downstream.port()) {
            problems_.emplace_back(wire.get(), QStringLiteral("The wire does not end at an input."));
            continue;
        }
        const auto target = wire->downstream.node.lock();
        const PortRef to{target->id(), wire->downstream.index};
        if (const auto [it, inserted] = fedInputs.emplace(std::make_pair(to.element, to.port), wire.get()); !inserted) {
            problems_.emplace_back(wire.get(), QStringLiteral("Input %1 is fed by two wires.").arg(portName(wire->downstream)));
            continue;
        }
        connections_.push_back(NetConnection{*driver, to, wire.get()});
    }
    auto rank = [&](const PortRef& port) { return std::make_pair(order.contains(port.element) ? order.at(port.element) : order.size(), port.port); };
    std::stable_sort(connections_.begin(), connections_.end(), [&](const NetConnection& a, const NetConnection& b) {
        return std::make_pair(rank(a.from), rank(a.to)) < std::make_pair(rank(b.from), rank(b.to));
    });
}

auto NetModel::schemaJson(const std::string& name) const -> boost::json::object
{
    boost::json::array elements;
    for (const auto& node : scene_.elementNodes()) {
        const auto& element = node->element();
        boost::json::object object{{"id", element.id}, {"type", element.type}, {"params", element.params}};
        if (!element.placement.empty()) {
            object["node"] = element.placement;
        }
        if (element.schema.is_object()) {
            object["schema"] = element.schema;
        }
        elements.push_back(std::move(object));
    }
    boost::json::array connections;
    for (const auto& connection : connections_) {
        connections.push_back(boost::json::object{{"from_element", connection.from.element},
                                                  {"from_port", connection.from.port},
                                                  {"to_element", connection.to.element},
                                                  {"to_port", connection.to.port}});
    }
    return boost::json::object{{"name", name}, {"elements", std::move(elements)}, {"connections", std::move(connections)}};
}

} // namespace s7studio::diagram
