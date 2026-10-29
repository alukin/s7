// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-S7-plugin-permission

#pragma once

#include "diagram/diagram_change.hpp"

namespace s7studio::diagram {

// Plans wire removal and reattachment from a snapshot of the wire trees.
// The caller provides any selected nodes or element edits in `change`.
class WireRemovalPlanner final
{
public:
    explicit WireRemovalPlanner(std::vector<std::shared_ptr<SignalWire>> wires);

    [[nodiscard]] auto plan(DiagramChange change, std::vector<std::shared_ptr<SignalWire>> requested) const -> DiagramChange;

private:
    std::vector<std::shared_ptr<SignalWire>> wires_;
};

} // namespace s7studio::diagram
