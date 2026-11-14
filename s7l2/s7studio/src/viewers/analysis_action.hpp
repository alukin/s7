// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-S7-plugin-permission
#pragma once
#include <QPushButton>

namespace s7studio::viewers {
inline void emphasizeAnalysisAction(QPushButton &button) {
    button.setStyleSheet(
        "QPushButton { background: #1565c0; color: white; border: 2px solid #1565c0;"
        " border-radius: 4px; padding: 6px 12px; font-weight: bold; }"
        "QPushButton:hover { background: #1976d2; border-color: #1976d2; }"
        "QPushButton:pressed { background: #0d47a1; border-color: #0d47a1; }"
        "QPushButton:focus { border-color: palette(highlighted-text); }"
        "QPushButton:disabled { background: palette(button); color: palette(mid); border-color: palette(mid); }");
}
} // namespace s7studio::viewers
