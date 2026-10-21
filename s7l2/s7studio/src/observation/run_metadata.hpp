// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-S7-plugin-permission

#pragma once
#include <QJsonObject>
#include <QString>
namespace s7studio::observation {
// Empty provenance is explicitly unavailable (standalone/imported data).
[[nodiscard]] bool validateRunProvenance(const QJsonObject& provenance, QString* error = nullptr);
[[nodiscard]] QString probeUnit(const QJsonObject& provenance, const QString& name);
[[nodiscard]] QString runMetadataSummary(const QJsonObject& observation);
}
