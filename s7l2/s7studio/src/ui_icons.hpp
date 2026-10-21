// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-S7-plugin-permission

#pragma once

#include <QColor>
#include <QIcon>
#include <QString>

namespace s7studio::ui {

// A monochrome SVG icon drawn in `color` (normal, active, selected, and a
// faded variant for disabled).
[[nodiscard]] auto themedIcon(const QString& resourcePath, const QColor& color) -> QIcon;

// A Tabler icon (resources/icons/tabler/<name>.svg) in the palette's button
// text color, or in `color`.
[[nodiscard]] auto icon(const QString& name) -> QIcon;
[[nodiscard]] auto icon(const QString& name, const QColor& color) -> QIcon;

} // namespace s7studio::ui
