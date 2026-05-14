#pragma once

#include <QtGlobal>

namespace s7ui::elements::style {

// Global visual scale for node item rendering (body, pins, text via item transform).
inline constexpr qreal kNodeVisualScale = 0.5;

// Basic node layout and typography.
inline constexpr qreal kNodeMinWidth = 15.0;
inline constexpr qreal kNodeMinHeight = 7.5;
inline constexpr qreal kPortPitch = 2.5;
inline constexpr qreal kPortMargin = 2.5;
inline constexpr qreal kNodeTitleTopPadding = 0.5;
inline constexpr qreal kNodeTitleHorizontalPadding = 0.4;
inline constexpr qreal kNodeTitleFontPointSize = 2.2;
inline constexpr qreal kNodeBorderPenWidth = 0.2;

// Connector (pin) vertical distribution using slot model.
// Slot 0 and slot (count+1) are forbidden (top/bottom margins).
// Connectors occupy interior slots [1..count].
inline constexpr int kConnectorSlotModel_ForbiddenEndpoints = 1;  // Number of forbidden top/bottom slots

// Wire rendering tweaks to keep line weight visually consistent with scaled nodes.
inline constexpr qreal kWirePenWidth = 0.6;
inline constexpr qreal kWireJunctionRadius = 2.0;
inline constexpr qreal kWireHandleSize = 2.0;
inline constexpr qreal kBrokenWireMarkerSize = 2.0;

} // namespace s7ui::elements::style

