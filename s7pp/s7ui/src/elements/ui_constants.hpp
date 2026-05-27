#pragma once

#include <QtGlobal>

namespace s7ui::elements::style {

// Global visual scale for node item rendering (body, pins, text via item transform).
inline constexpr qreal kNodeVisualScale = 0.5;

// Basic node layout and typography.
// Values are intentionally chosen so that after kNodeVisualScale is applied,
// connector scene positions still land on the scene grid.
inline constexpr qreal kNodeMinWidth = 30.0;
inline constexpr qreal kNodeMinHeight = 20.0;
inline constexpr qreal kPortPitch = 10.0;
inline constexpr qreal kPortMargin = 10.0;
inline constexpr qreal kNodeTitleTopPadding = 1.0;
inline constexpr qreal kNodeTitleHorizontalPadding = 0.8;
inline constexpr qreal kNodeTitleFontPointSize = 4.4;
inline constexpr qreal kNodeBorderPenWidth = 0.4;

// Connector (pin) vertical distribution using slot model.
// Slot 0 and slot (count+1) are forbidden (top/bottom margins).
// Connectors occupy interior slots [1..count].
inline constexpr int kConnectorSlotModel_ForbiddenEndpoints = 1;  // Number of forbidden top/bottom slots

// Wire rendering tweaks to keep line weight visually consistent with scaled nodes.
inline constexpr qreal kWirePenWidth = 0.35;
inline constexpr qreal kWireJunctionRadius = 1.0;
inline constexpr qreal kWireHandleSize = 1.2;
inline constexpr qreal kBrokenWireMarkerSize = 1.2;

} // namespace s7ui::elements::style

