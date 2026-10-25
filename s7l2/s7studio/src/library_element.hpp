// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-S7-plugin-permission
#pragma once

#include <QString>
class QMimeData;

namespace s7studio::ui {
inline constexpr char libraryElementMimeType[] = "application/x-s7studio-library-element";
inline constexpr int maximumLibraryElementPayload = 4096;

// Identifiers are registry type names or StudioObserver:<viewer type>.
bool libraryElementAvailable(const QString& identifier);
QMimeData* libraryElementMimeData(const QString& identifier);
// Empty for malformed, unsupported or no-longer-available entries.
QString libraryElementFromMimeData(const QMimeData* data);
}
