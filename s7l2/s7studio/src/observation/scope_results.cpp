#include "observation/scope_results.hpp"

namespace s7studio::observation {
std::unique_ptr<ui::ScopeRecording> scopeProjection(const ObservationSource& source,
    const QString& name, const QVector<ChannelId>& selection, QString* error)
{
    auto result = std::make_unique<ui::ScopeRecording>(name);
    const auto snapshot = source.status();
    const auto channels = source.channels();
    std::uint64_t cursor = 0;
    while (cursor < snapshot.samples) {
        const auto page = source.read(cursor, static_cast<qsizetype>(std::min<std::uint64_t>(4096, snapshot.samples - cursor)));
        if (!page.error.isEmpty() || page.next == cursor) { if (error) *error = page.error; return {}; }
        for (const auto& sample : page.samples) {
            if (!selection.contains(sample.channel)) continue;
            const auto& channel = channels[sample.channel];
            const auto separator = channel.name.indexOf(QLatin1Char('/'));
            result->appendSample(separator < 0 ? channel.name : channel.name.mid(separator + 1), sample.timeSeconds, sample.value);
        }
        cursor = page.next;
    }
    result->setDroppedSampleCount(snapshot.deliveryDropped);
    if (!snapshot.storageError.isEmpty()) result->setCaptureError(snapshot.storageError);
    return result;
}
} // namespace s7studio::observation
