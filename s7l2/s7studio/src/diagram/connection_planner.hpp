// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-S7-plugin-permission

#pragma once

#include "diagram/diagram_change.hpp"

#include <functional>
#include <optional>

namespace s7studio::diagram {

// Checks a prospective connection and prepares its complete undoable edit.
// The caller supplies the current wire snapshot and a lazy schema factory;
// planning never changes the scene or its elements. The cached type status
// belongs to that snapshot and must be discarded after an edit.
class ConnectionPlanner final
{
public:
    struct Result {
        QString problem;
        DiagramChange change;
    };

    ConnectionPlanner(std::vector<std::shared_ptr<SignalWire>> wires,
                      std::function<boost::json::object()> schema,
                      std::optional<bool> typesResolve);

    [[nodiscard]] auto check(const WireEnd& a, const WireEnd& b) const -> QString;
    [[nodiscard]] auto plan(const WireEnd& a, const WireEnd& b, QVector<QPointF> bends = {}) const -> Result;
    [[nodiscard]] auto typesResolve() const -> std::optional<bool> { return typesResolve_; }

private:
    [[nodiscard]] auto driverEnd(const WireEnd& end) const -> std::optional<WireEnd>;
    [[nodiscard]] auto typeProblem(const WireEnd& driver, const WireEnd& input) const -> QString;
    [[nodiscard]] auto trunkOf(const WireEnd& output) const -> std::shared_ptr<SignalWire>;

    std::vector<std::shared_ptr<SignalWire>> wires_;
    std::function<boost::json::object()> schema_;
    mutable std::optional<bool> typesResolve_;
};

} // namespace s7studio::diagram
