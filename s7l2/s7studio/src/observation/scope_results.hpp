// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-S7-plugin-permission

#pragma once
#include "observation/observation_source.hpp"
#include "scope_recording.hpp"
#include <memory>

namespace s7studio::observation {
// Compatibility with the existing .s7scope result format, independent of a
// window. A projection is built only for explicit result operations.
std::unique_ptr<ui::ScopeRecording> scopeProjection(const ObservationSource& source,
    const QString& name, const QVector<ChannelId>& channels, QString* error = nullptr);
} // namespace s7studio::observation
