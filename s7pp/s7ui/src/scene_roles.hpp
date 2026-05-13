#pragma once

#include <Qt>

namespace s7ui::ui {

inline constexpr int kRoleTypeName = Qt::UserRole + 1;
inline constexpr int kRoleCategory = Qt::UserRole + 2;
inline constexpr int kRoleInputPorts = Qt::UserRole + 3;
inline constexpr int kRoleOutputPorts = Qt::UserRole + 4;
inline constexpr int kRoleVisualSink = Qt::UserRole + 5;
inline constexpr int kRoleNodeId = Qt::UserRole + 10;
inline constexpr int kRoleNodeType = Qt::UserRole + 11;
inline constexpr int kRoleNodeName = Qt::UserRole + 12;
inline constexpr int kRolePaperOverlay = Qt::UserRole + 200;

} // namespace s7ui::ui

