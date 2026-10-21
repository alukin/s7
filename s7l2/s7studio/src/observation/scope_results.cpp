#include "observation/scope_results.hpp"
#include <QJsonArray>

namespace s7studio::observation {
std::unique_ptr<ui::ScopeRecording> scopeProjection(const ObservationSource& source,
    const QString& name, const QVector<ChannelId>& selection, QString* error)
{
    auto result = std::make_unique<ui::ScopeRecording>(name);
    const auto snapshot = source.status();
    const auto channels = source.channels();
    QHash<ChannelId, std::int64_t> firstSteps, lastSteps;
    QHash<ChannelId, std::uint64_t> counts;
    std::uint64_t cursor = 0;
    while (cursor < snapshot.samples) {
        const auto page = source.read(cursor, static_cast<qsizetype>(std::min<std::uint64_t>(4096, snapshot.samples - cursor)));
        if (!page.error.isEmpty() || page.next == cursor) { if (error) *error = page.error; return {}; }
        for (const auto& sample : page.samples) {
            if (!selection.contains(sample.channel)) continue;
            if (!firstSteps.contains(sample.channel)) firstSteps.insert(sample.channel, sample.step);
            lastSteps.insert(sample.channel, sample.step); ++counts[sample.channel];
            const auto& channel = channels[sample.channel];
            const auto separator = channel.name.indexOf(QLatin1Char('/'));
            result->appendSample(separator < 0 ? channel.name : channel.name.mid(separator + 1), sample.timeSeconds, sample.value);
        }
        cursor = page.next;
    }
    if (result->exportMayBeIncomplete()) {
        if (error) *error = QStringLiteral("Cannot build result projection: ") + result->statusText();
        return {};
    }
    QJsonArray mapping;
    for (const auto& channel : channels) if (counts.contains(channel.id)) {
        const auto count = counts.value(channel.id);
        const auto first = firstSteps.value(channel.id), last = lastSteps.value(channel.id);
        mapping.append(QJsonObject{{"name", channel.name}, {"id", QString::number(channel.id)},
            {"source_type", channel.sourceType}, {"stored_type", "f64"},
            {"first_step", QString::number(first)}, {"last_step", QString::number(last)},
            {"consecutive_steps", count == static_cast<std::uint64_t>(last - first + 1)}});
    }
    result->setRunMetadata({{"run_id", QString::number(source.runId())}, {"finished", snapshot.finished},
        {"stopped", snapshot.stopped}, {"run_error", snapshot.runError}, {"channels", mapping}, {"provenance", source.provenance()}});
    result->setDroppedSampleCount(snapshot.captureDropped);
    if (!snapshot.storageError.isEmpty() || !snapshot.runError.isEmpty())
        result->setCaptureError(snapshot.storageError + QStringLiteral(" ") + snapshot.runError);
    return result;
}
} // namespace s7studio::observation
