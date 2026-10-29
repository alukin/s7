// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-S7-plugin-permission

#pragma once

#include <QPoint>
#include <QPointF>

namespace s7studio::diagram {

class DiagramScene;
class SignalWire;

// Presents actions for an existing wire and applies the selected scene edit.
void showWireContextMenu(DiagramScene& scene, SignalWire& wire, QPointF scenePos, QPoint screenPos);

} // namespace s7studio::diagram
