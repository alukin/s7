// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-S7-plugin-permission

#include "viewers/observation_viewer.hpp"
#include "observation/run_metadata.hpp"

#include <QElapsedTimer>

namespace s7studio::viewers {
void ObservationViewer::attachSource(std::shared_ptr<const observation::ObservationSource> source,
                                    QVector<InputBinding> bindings, bool clear)
{
    source_ = std::move(source);
    bindings_ = std::move(bindings);
    readError_.clear();
    if (clear) clearSamples();
    cursor_ = 0;
    units_.clear();
    if (source_) {
        const auto provenance = source_->provenance();
        for (const auto& channel : source_->channels()) units_.insert(channel.id, observation::probeUnit(provenance, channel.name));
    }
}
void ObservationViewer::discardPresentationHistory()
{
    if (source_) cursor_ = source_->status().samples;
}
QString ObservationViewer::sourceStatusText() const
{
    if (!readError_.isEmpty()) return QStringLiteral("INCOMPLETE display read: ") + readError_;
    if (!source_) return {};
    const auto status = source_->status();
    QStringList parts;
    const auto state = status.incomplete() ? QStringLiteral("INCOMPLETE") : !status.finished ? QStringLiteral("capturing")
        : status.stopped ? QStringLiteral("stopped") : QStringLiteral("completed");
    parts << QStringLiteral("Run #%1 — %2").arg(source_->runId()).arg(state);
    if (status.captureDropped) parts << QStringLiteral("capture lost %1 records").arg(status.captureDropped);
    if (cursor_ < status.samples) parts << QStringLiteral("loading history %1/%2 records").arg(cursor_).arg(status.samples);
    if (!status.storageError.isEmpty()) parts << QStringLiteral("INCOMPLETE: ") + status.storageError;
    if (!status.runError.isEmpty()) parts << QStringLiteral("run failed: ") + status.runError;
    return parts.join(QStringLiteral(" | "));
}
bool ObservationViewer::refreshSource()
{
    if (!source_) return false;
    // Several pages per UI tick, within a small time budget: one page per
    // tick capped intake at a page every 16 ms, so a fast run left the view
    // further behind for its whole duration.
    constexpr qint64 kRefreshBudgetMs = 4;
    QElapsedTimer budget;
    budget.start();
    const auto channels = source_->channels();
    bool more = false;
    do {
        const auto page = source_->read(cursor_);
        readError_ = page.error;
        for (const auto& sample : page.samples) {
            for (const auto& binding : bindings_) if (binding.channel == sample.channel)
                appendObservedSample({channels[sample.channel].name, sample.step,
                    sample.timeSeconds, sample.value, source_->runId(), sample.channel, binding.role, units_.value(sample.channel)});
        }
        cursor_ = page.next;
        more = page.error.isEmpty() && cursor_ < source_->status().samples;
    } while (more && budget.elapsed() < kRefreshBudgetMs);
    setDroppedSampleCount(source_->status().captureDropped);
    return more;
}
} // namespace s7studio::viewers
