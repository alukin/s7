// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-S7-plugin-permission
#pragma once

class QWidget;
class QUrl;

namespace s7studio {
void showHelp(QWidget* parent);
// Open a bundled help article or an HTTP(S) reference from diagram notes.
void openHelpLink(QWidget* parent, const QUrl& link);
void showAbout(QWidget* parent);
}
