// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-S7-plugin-permission

#pragma once

#include "diagram/diagram_change.hpp"

namespace s7studio::diagram {

// Completes a document edit with the probe and observer-preview changes that
// follow from removed or changed elements. The scene applies the result as one
// undo command.
[[nodiscard]] auto completeDiagramChange(DiagramChange change,
                                         const std::vector<DiagramProbe>& probes,
                                         const boost::json::array& observersPanel) -> DiagramChange;

} // namespace s7studio::diagram
