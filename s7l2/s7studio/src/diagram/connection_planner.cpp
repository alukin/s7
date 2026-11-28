// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-S7-plugin-permission

#include "diagram/connection_planner.hpp"

#include "diagram/diagram_geometry.hpp"
#include "diagram/element_ports.hpp"
#include "local_runtime_v2.hpp"

#include <s7/type_inference.hpp>
#include <s7/validation.hpp>
#include <s7/schema_library.hpp>

#include <algorithm>
#include <exception>
#include <utility>

namespace s7studio::diagram {

namespace {

// Unknown element types (plugins not loaded) count as unresolved, so adding
// a connection cannot make an already unresolved diagram appear worse.
auto resolveError(const boost::json::object& schema) -> std::optional<QString>
{
    try {
        const auto built = runtime::buildSchema(schema);
        const auto flattened = s7::detail::flatten_schema(built, true);
        if (!flattened.errors.empty()) return QString::fromStdString(flattened.errors.front());
        const auto resolved = s7::TypeResolver::resolve(*flattened.schema);
        if (resolved.ok()) {
            return std::nullopt;
        }
        return QString::fromStdString(resolved.errors.front());
    } catch (const std::exception& ex) {
        return QString::fromUtf8(ex.what());
    }
}

bool declaresArrayPort(const ElementNode& node, int index, bool output)
{
    if (index < 0) return false;
    const auto port = static_cast<std::size_t>(index);
    const auto& element = node.element();
    if (element.schema.is_object()) {
        const auto* bindings = element.schema.as_object().if_contains(output ? "exposed_outputs" : "exposed_inputs");
        return bindings && bindings->is_array() && port < bindings->as_array().size() &&
               bindings->as_array()[port].is_object() && bindings->as_array()[port].as_object().contains("shape");
    }
    const auto* descriptor = s7::ElementRegistry::instance().find(element.type);
    if (!descriptor) return false;
    const auto& metadata = descriptor->metadata();
    const auto& ports = output ? metadata.output_ports : metadata.input_ports;
    return port < ports.size() && ports[port].array.has_value();
}

} // namespace

ConnectionPlanner::ConnectionPlanner(std::vector<std::shared_ptr<ElementNode>> nodes,
                                     std::vector<std::shared_ptr<SignalWire>> wires,
                                     std::function<boost::json::object()> schema,
                                     std::optional<bool> typesResolve)
    : nodes_(std::move(nodes)), wires_(std::move(wires)), schema_(std::move(schema)), typesResolve_(typesResolve)
{
}

bool ConnectionPlanner::contains(const WireEnd& end) const
{
    if (end.isPort()) {
        const auto node = end.node.lock();
        return node && end.port() && std::ranges::any_of(nodes_, [&](const auto& current) { return current == node; });
    }
    if (end.isJunction()) {
        const auto wire = end.wire.lock();
        return wire && std::ranges::any_of(wires_, [&](const auto& current) { return current == wire; });
    }
    return false;
}

auto ConnectionPlanner::trunkOf(const WireEnd& output) const -> std::shared_ptr<SignalWire>
{
    for (const auto& wire : wires_) {
        if (wire->upstream.sameAs(output)) {
            return wire;
        }
    }
    return nullptr;
}

auto ConnectionPlanner::driverEnd(const WireEnd& end) const -> std::optional<WireEnd>
{
    if (end.isOutput()) {
        return end;
    }
    if (end.isJunction()) {
        if (const auto wire = end.wire.lock()) {
            if (const auto* root = NetModel::rootOf(*wire);
                root && root->upstream.isOutput() &&
                std::ranges::any_of(wires_, [&](const auto& current) { return current.get() == root; })) {
                return root->upstream;
            }
        }
    }
    return std::nullopt;
}

auto ConnectionPlanner::typeProblem(const WireEnd& driver, const WireEnd& input) const -> QString
{
    const auto from = driver.node.lock();
    const auto to = input.node.lock();
    if (!from || !to) {
        return {};
    }
    if (isObserverElementType(to->element().type)) {
        const auto kind = arrayObserverKind(to->element().type);
        if (kind != ArrayObserverKind::None) {
            if (!declaresArrayPort(*from, driver.index, true)) return arrayObserverPortProblem(kind, nullptr);
            try {
                const auto& source = from->element();
                const auto port = static_cast<std::size_t>(driver.index);
                if (source.schema.is_object()) {
                    const auto exposed = s7::parse_exposed_ports(source.schema.as_object(), "exposed_outputs");
                    const auto& shape = exposed.at(port).array;
                    return arrayObserverPortProblem(kind, shape ? &*shape : nullptr);
                }
                s7::Schema schema("observer_source");
                runtime::detail::addElementFromRegistry(schema, source.id, source.type, source.params);
                auto resolved = s7::TypeResolver::resolve(schema);
                if (!resolved.ok()) return QString::fromStdString(resolved.errors.front());
                const auto& spec = schema.elements().front();
                if (spec.array_metadata) {
                    const auto& shape = spec.array_metadata->output_ports.at(port).array;
                    return arrayObserverPortProblem(kind, shape ? &*shape : nullptr);
                }
                const auto element = resolved.instances.front() ? std::move(resolved.instances.front())
                                                                 : resolved.factories.front()();
                const auto ports = element->output_ports();
                return arrayObserverPortProblem(kind, port < ports.size() ? ports[port].array_descriptor : nullptr);
            } catch (const std::exception& ex) {
                return QString::fromUtf8(ex.what());
            }
        }
        if (declaresArrayPort(*from, driver.index, true)) {
            return QStringLiteral("This observer requires a scalar output.");
        }
        return {};
    }
    auto schema = schema_();
    boost::json::object connection{
        {"from_element", from->id()}, {"from_port", driver.index}, {"to_element", to->id()}, {"to_port", input.index}};
    if (declaresArrayPort(*from, driver.index, true) || declaresArrayPort(*to, input.index, false)) {
        boost::json::array endpoints;
        for (const auto& element : schema.at("elements").as_array()) {
            const std::string_view id = element.as_object().at("id").as_string();
            if (id == from->id() || id == to->id()) endpoints.push_back(element);
        }
        // Fixed array descriptors can be checked even when another element in
        // the diagram has an unresolved type or an unavailable plugin.
        const boost::json::object trial{{"elements", std::move(endpoints)},
                                        {"connections", boost::json::array{connection}}};
        if (const auto error = resolveError(trial)) {
            return QStringLiteral("Types do not match: %1").arg(*error);
        }
        return {};
    }
    if (!typesResolve_) {
        typesResolve_ = !resolveError(schema).has_value();
    }
    if (!*typesResolve_) {
        return {};
    }
    schema["connections"].as_array().push_back(std::move(connection));
    if (const auto error = resolveError(schema)) {
        return QStringLiteral("Types do not match: %1").arg(*error);
    }
    return {};
}

auto ConnectionPlanner::check(const WireEnd& a, const WireEnd& b) const -> QString
{
    auto kind = [](const WireEnd& end) { return end.isOutput() ? 0 : (end.isInput() ? 1 : (end.isJunction() ? 2 : 3)); };
    const int ka = kind(a);
    const int kb = kind(b);
    if (ka == 3 || kb == 3) {
        return QStringLiteral("A wire connects ports and wires.");
    }
    if (!contains(a) || !contains(b)) {
        return (a.isJunction() || b.isJunction()) ? QStringLiteral("The wire is no longer in the diagram.")
                                                  : QStringLiteral("The port is no longer in the diagram.");
    }
    if (a.sameAs(b)) {
        return QStringLiteral("Pick another port.");
    }
    if (ka == 0 && kb == 0) {
        return QStringLiteral("Two outputs cannot be connected.");
    }
    if (ka == 1 && kb == 1) {
        return QStringLiteral("An input connects to an output or to a wire carrying a signal.");
    }
    if (ka == 2 && kb == 2) {
        return QStringLiteral("A wire joins another wire only to feed an input.");
    }
    const WireEnd& input = ka == 1 ? a : b;
    const WireEnd& upstream = ka == 1 ? b : a;
    if (ka + kb == 2) {
        const WireEnd& output = ka == 0 ? a : b;
        const WireEnd& onWire = ka == 0 ? b : a;
        const auto driver = driverEnd(onWire);
        const auto node = driver ? driver->node.lock() : nullptr;
        if (driver && driver->sameAs(output)) {
            return QStringLiteral("The wire already carries this output.");
        }
        return QStringLiteral("The wire already carries a signal (%1); a net has one output.")
            .arg(node ? QStringLiteral("%1.%2").arg(node->title(), driver->port() ? driver->port()->name() : QString()) : QStringLiteral("?"));
    }
    const auto inputPort = input.port();
    if (!inputPort) {
        return QStringLiteral("The input is gone.");
    }
    for (const auto& wire : wires_) {
        if (wire->downstream.sameAs(input)) {
            return QStringLiteral("Input %1.%2 is already connected.").arg(input.node.lock()->title(), inputPort->name());
        }
    }
    const auto driver = driverEnd(upstream);
    if (!driver || !driver->port()) {
        return QStringLiteral("The wire is not connected to an output.");
    }
    return typeProblem(*driver, input);
}

auto ConnectionPlanner::plan(const WireEnd& a, const WireEnd& b, QVector<QPointF> bends) const -> Result
{
    if (auto problem = check(a, b); !problem.isEmpty()) {
        return {.problem = std::move(problem), .change = {}};
    }
    WireEnd upstream = a;
    WireEnd downstream = b;
    if (a.isInput()) {
        std::swap(upstream, downstream);
        std::reverse(bends.begin(), bends.end());
    }
    if (upstream.isOutput()) {
        // An output has one first wire; further wires branch off it.
        if (const auto trunk = trunkOf(upstream)) {
            const auto port = upstream.port();
            const QPointF at = port ? port->scenePos() : QPointF();
            upstream = WireEnd::onWire(trunk, snap(at));
        }
    }
    auto wire = std::make_shared<SignalWire>();
    wire->upstream = upstream;
    wire->downstream = downstream;
    wire->userBends = withoutRepeats(bends);
    Result result;
    result.change.addedWires.push_back(std::move(wire));
    if (const auto target = downstream.node.lock(); target && (target->element().type == "Scope" || target->element().type == "LiveOscilloscope" || target->element().type == "LiveSpectrum") &&
        downstream.index == static_cast<int>(target->inputs().size()) - 1) {
        auto element = target->element();
        element.params["channel_count"] = static_cast<int>(target->inputs().size());
        const auto ports = elementPorts(element.type, element.params, element.schema);
        const int count = std::max({static_cast<int>(ports.inputs.size()), static_cast<int>(ports.outputs.size()),
                                    target->minimumInputs(), target->minimumOutputs()});
        const QSize beforeSize = target->gridSize();
        const QSize afterSize(beforeSize.width(), elementRowsForPorts(beforeSize.height(), count));
        result.change.elements.push_back({target, target->element(), std::move(element), beforeSize, afterSize});
    }
    return result;
}

} // namespace s7studio::diagram
