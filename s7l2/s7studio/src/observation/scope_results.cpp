// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-S7-plugin-permission

#include "observation/scope_results.hpp"
#include "observation/run_metadata.hpp"

#include <QJsonArray>

#include <algorithm>
#include <utility>

namespace s7studio::observation {

ScopeProjectionCursor::ScopeProjectionCursor(const ObservationSource& source, QString name, QVector<ChannelId> selection)
    : source_(source), snapshot_(source.status()), channels_(source.channels()), selection_(std::move(selection)),
      result_(std::make_unique<ui::ScopeRecording>(std::move(name)))
{
}

ProjectionStep ScopeProjectionCursor::advance(QString* error)
{
    if (finished_)
        return ProjectionStep::Complete;
    if (cursor_ < snapshot_.samples) {
        const auto page = source_.read(cursor_, static_cast<qsizetype>(std::min<std::uint64_t>(4096, snapshot_.samples - cursor_)));
        if (!page.error.isEmpty() || page.next <= cursor_) {
            if (error)
                *error = page.error.isEmpty() ? QStringLiteral("Recording did not advance.") : page.error;
            return ProjectionStep::Failed;
        }
        for (const auto& sample : page.samples) {
            if (!selection_.contains(sample.channel))
                continue;
            if (sample.channel >= static_cast<ChannelId>(channels_.size())) {
                if (error)
                    *error = QStringLiteral("Recording refers to an unknown channel.");
                return ProjectionStep::Failed;
            }
            if (!firstSteps_.contains(sample.channel))
                firstSteps_.insert(sample.channel, sample.step);
            lastSteps_.insert(sample.channel, sample.step);
            ++counts_[sample.channel];
            const auto& channel = channels_[sample.channel];
            const auto separator = channel.name.indexOf(QLatin1Char('/'));
            result_->appendSample(separator < 0 ? channel.name : channel.name.mid(separator + 1), sample.timeSeconds, sample.value);
        }
        cursor_ = page.next;
        if (cursor_ < snapshot_.samples)
            return ProjectionStep::More;
    }
    if (result_->exportMayBeIncomplete()) {
        if (error)
            *error = QStringLiteral("Cannot build result projection: ") + result_->statusText();
        return ProjectionStep::Failed;
    }
    const auto provenance = source_.provenance();
    QJsonArray mapping;
    for (const auto& channel : channels_)
        if (counts_.contains(channel.id)) {
            const auto separator = channel.name.indexOf(QLatin1Char('/'));
            result_->setChannelUnit(separator < 0 ? channel.name : channel.name.mid(separator + 1), probeUnit(provenance, channel.name));
            const auto count = counts_.value(channel.id);
            const auto first = firstSteps_.value(channel.id), last = lastSteps_.value(channel.id);
            mapping.append(QJsonObject{{"name", channel.name},
                                       {"id", QString::number(channel.id)},
                                       {"source_type", channel.sourceType},
                                       {"stored_type", "f64"},
                                       {"first_step", QString::number(first)},
                                       {"last_step", QString::number(last)},
                                       {"consecutive_steps", count == static_cast<std::uint64_t>(last - first + 1)}});
        }
    result_->setRunMetadata({{"run_id", QString::number(source_.runId())},
                             {"finished", snapshot_.finished},
                             {"stopped", snapshot_.stopped},
                             {"run_error", snapshot_.runError},
                             {"channels", mapping},
                             {"provenance", provenance}});
    result_->setDroppedSampleCount(snapshot_.captureDropped);
    if (!snapshot_.storageError.isEmpty() || !snapshot_.runError.isEmpty())
        result_->setCaptureError(snapshot_.storageError + QStringLiteral(" ") + snapshot_.runError);
    finished_ = true;
    return ProjectionStep::Complete;
}

std::unique_ptr<ui::ScopeRecording> ScopeProjectionCursor::takeResult() { return finished_ ? std::move(result_) : nullptr; }

std::unique_ptr<ui::ScopeRecording> scopeProjection(const ObservationSource& source, const QString& name,
                                                    const QVector<ChannelId>& selection, QString* error)
{
    ScopeProjectionCursor cursor(source, name, selection);
    for (;;) {
        switch (cursor.advance(error)) {
        case ProjectionStep::More:
            continue;
        case ProjectionStep::Complete:
            return cursor.takeResult();
        case ProjectionStep::Failed:
            return {};
        }
    }
}

} // namespace s7studio::observation
