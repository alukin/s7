// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-S7-plugin-permission

#pragma once

#include <boost/json.hpp>

#include <QString>
#include <s7/array_descriptor.hpp>

#include <string_view>
#include <vector>

namespace s7studio::diagram {

// Upper bound on one element's ports in Studio. Diagram files and parameters
// are untrusted: a hostile count must not build millions of port items.
inline constexpr int kMaxElementPorts = 4096;

enum class ArrayObserverKind { None, Waterfall, VideoPlayer };

[[nodiscard]] inline std::string_view arrayObserverType(ArrayObserverKind kind)
{
    switch (kind) {
    case ArrayObserverKind::Waterfall: return "Waterfall";
    case ArrayObserverKind::VideoPlayer: return "VideoPlayer";
    case ArrayObserverKind::None: return {};
    }
    return {};
}

[[nodiscard]] inline ArrayObserverKind arrayObserverKind(const std::string_view type)
{
    if (type == "Waterfall") return ArrayObserverKind::Waterfall;
    if (type == "VideoPlayer") return ArrayObserverKind::VideoPlayer;
    return ArrayObserverKind::None;
}

[[nodiscard]] inline QString arrayObserverPortProblem(ArrayObserverKind kind, const s7::ArrayDescriptor* shape)
{
    switch (kind) {
    case ArrayObserverKind::Waterfall:
        if (shape && shape->type() == s7::SignalType::F32 && shape->rank() == 1) return {};
        return QStringLiteral("Waterfall requires an F32 spectrum vector.");
    case ArrayObserverKind::VideoPlayer:
        if (shape && shape->type() == s7::SignalType::U8 &&
            (shape->rank() == 2 || (shape->rank() == 3 && shape->extents()[2] == 3))) return {};
        return QStringLiteral("Video Player requires a U8 grayscale [H,W] or RGB [H,W,3] frame.");
    case ArrayObserverKind::None:
        return {};
    }
    return {};
}

struct ElementPorts {
    std::vector<QString> inputs;  // port names, by index
    std::vector<QString> outputs;
};

// The ports an element shows: from its nested schema's exposed ports, the
// element registry's metadata, and the params that size variadic elements
// (Adder `weights`, Multiply `factors`, Scope `channel_count`).
[[nodiscard]] auto elementPorts(std::string_view type, const boost::json::object& params, const boost::json::value& nestedSchema)
    -> ElementPorts;

// Observer element types (Scope, PhasePortrait, PhaseTrajectory3D): their
// inputs are observation taps excluded from the exported runtime schema.
[[nodiscard]] bool isObserverElementType(std::string_view type);

// Observer types whose inputs are fixed axes (x/y[/z]) rather than Scope's
// numbered signal channels.
[[nodiscard]] bool isAxisObserverElementType(std::string_view type);

} // namespace s7studio::diagram
