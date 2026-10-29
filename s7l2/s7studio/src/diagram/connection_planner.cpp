// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-S7-plugin-permission

#include "diagram/connection_planner.hpp"

#include "diagram/diagram_geometry.hpp"
#include "diagram/element_ports.hpp"
#include "local_runtime_v2.hpp"

#include <s7/type_inference.hpp>

#include <algorithm>
#include <cmath>
#include <exception>
#include <utility>

namespace s7studio::diagram {

namespace {

auto withoutRepeats(const QVector<QPointF>& points) -> QVector<QPointF>
{
    QVector<QPointF> result;
    for (const auto& point : points) {
        if (result.isEmpty() || std::abs(result.back().x() - point.x()) >= 0.5 ||
            std::abs(result.back().y() - point.y()) >= 0.5) {
            result.append(point);
        }
    }
    return result;
}

// Unknown element types (plugins not loaded) count as unresolved, so adding
// a connection cannot make an already unresolved diagram appear worse.
auto resolveError(const boost::json::object& schema) -> std::optional<QString>
{
    try {
        const auto built = runtime::buildSchema(schema);
        const auto resolved = s7::TypeResolver::resolve(built);
        if (resolved.ok()) {
            return std::nullopt;
        }
        return QString::fromStdString(resolved.errors.front());
    } catch (const std::exception& ex) {
        return QString::fromUtf8(ex.what());
    }
}

} // namespace

ConnectionPlanner::ConnectionPlanner(std::vector<std::shared_ptr<SignalWire>> wires,
                                     std::function<boost::json::object()> schema,
                                     std::optional<bool> typesResolve)
    : wires_(std::move(wires)), schema_(std::move(schema)), typesResolve_(typesResolve)
{
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
            if (const auto* root = NetModel::rootOf(*wire); root && root->upstream.isOutput()) {
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
        return {}; // observation inputs accept any signal type
    }
    auto schema = schema_();
    if (!typesResolve_) {
        typesResolve_ = !resolveError(schema).has_value();
    }
    if (!*typesResolve_) {
        return {};
    }
    schema["connections"].as_array().push_back(boost::json::object{
        {"from_element", from->id()}, {"from_port", driver.index}, {"to_element", to->id()}, {"to_port", input.index}});
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
    if (const auto target = downstream.node.lock(); target && (target->element().type == "Scope" || target->element().type == "LiveOscilloscope") &&
        downstream.index == static_cast<int>(target->inputs().size()) - 1) {
        auto element = target->element();
        element.params["channel_count"] = static_cast<int>(target->inputs().size());
        result.change.elements.push_back({target, target->element(), std::move(element)});
    }
    return result;
}

} // namespace s7studio::diagram
