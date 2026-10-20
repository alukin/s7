#include "viewers/observation_viewer.hpp"

namespace s7studio::viewers {
void ObservationViewer::attachSource(std::shared_ptr<const observation::ObservationSource> source,
                                    QVector<InputBinding> bindings, bool clear)
{
    source_ = std::move(source);
    bindings_ = std::move(bindings);
    readError_.clear();
    if (clear) clearSamples();
    cursor_ = 0;
}
void ObservationViewer::discardPresentationHistory()
{
    if (source_) cursor_ = source_->status().samples;
}
QString ObservationViewer::sourceStatusText() const
{
    if (!readError_.isEmpty()) return readError_;
    if (!source_) return {};
    const auto status = source_->status();
    return status.storageError.isEmpty() ? QString() : QStringLiteral("INCOMPLETE: ") + status.storageError;
}
bool ObservationViewer::refreshSource()
{
    if (!source_) return false;
    const auto page = source_->read(cursor_);
    readError_ = page.error;
    const auto channels = source_->channels();
    for (const auto& sample : page.samples) {
        for (const auto& binding : bindings_) if (binding.channel == sample.channel)
            appendObservedSample({channels[sample.channel].name, static_cast<long>(sample.step),
                sample.timeSeconds, sample.value, source_->runId(), sample.channel, binding.role});
    }
    cursor_ = page.next;
    setDroppedSampleCount(source_->status().deliveryDropped);
    return page.error.isEmpty() && cursor_ < source_->status().samples;
}
} // namespace s7studio::viewers
