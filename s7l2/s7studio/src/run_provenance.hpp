// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-S7-plugin-permission

#pragma once
#include "is_schema_runner.hpp"
#include <QJsonObject>

namespace s7studio {
struct PreparedRun {
    boost::json::value effectiveSchema;
    QJsonObject provenance;
};
// Fill registry defaults and freeze nested definitions without changing dt.
// Resolve the S7 noise seed=0 convention before any factory is called.
[[nodiscard]] PreparedRun prepareRunProvenance(const RunConfiguration& configuration);
// Reconstruct the original execution request from provenance, never from an editor.
// This only validates/returns configuration; it does not execute imported models.
[[nodiscard]] RunConfiguration configurationFromProvenance(const QJsonObject& provenance);
} // namespace s7studio
