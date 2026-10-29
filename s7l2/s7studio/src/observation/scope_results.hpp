// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-S7-plugin-permission

#pragma once
#include "observation/observation_source.hpp"
#include "scope_recording.hpp"
#include <memory>

namespace s7studio::observation {
enum class ProjectionStep { More, Complete, Failed };

// Reads at most one bounded page per advance() call. Viewers can schedule the
// next page on a later UI turn; explicit export can run it to completion.
class ScopeProjectionCursor final
{
public:
    ScopeProjectionCursor(const ObservationSource& source, QString name, QVector<ChannelId> selection);
    [[nodiscard]] ProjectionStep advance(QString* error = nullptr);
    [[nodiscard]] std::unique_ptr<ui::ScopeRecording> takeResult();

private:
    const ObservationSource& source_;
    SourceStatus snapshot_;
    QVector<Channel> channels_;
    QVector<ChannelId> selection_;
    QHash<ChannelId, std::int64_t> firstSteps_, lastSteps_;
    QHash<ChannelId, std::uint64_t> counts_;
    std::unique_ptr<ui::ScopeRecording> result_;
    std::uint64_t cursor_ = 0;
    bool finished_ = false;
};

// Compatibility with the existing .s7scope result format, independent of a
// window. A projection is built only for explicit result operations.
std::unique_ptr<ui::ScopeRecording> scopeProjection(const ObservationSource& source, const QString& name,
                                                    const QVector<ChannelId>& channels, QString* error = nullptr);
} // namespace s7studio::observation
