// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-S7-plugin-permission
#pragma once
#include <functional>

class QWidget;
class QUrl;
class QString;

namespace s7studio {
// Receives a path relative to the bundled examples directory.
using OpenHelpExample = std::function<bool(const QString&)>;
// Resolve an existing diagram inside this directory; reject escapes/symlinks out.
QString resolveHelpExample(const QString& directory, const QString& relativePath);
void showHelp(QWidget* parent, const OpenHelpExample& openExample = {});
// Open a bundled article/example or an HTTP(S) reference from diagram notes.
void openHelpLink(QWidget* parent, const QUrl& link, const OpenHelpExample& openExample = {});
void showAbout(QWidget* parent);
}
