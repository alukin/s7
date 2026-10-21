#pragma once
#include "observation/observation_source.hpp"
#include "scope_sample_mailbox.hpp"

namespace s7studio::observation {
struct RetainedRun { QString label; std::shared_ptr<const ObservationSource> source; };

// Host-side lifetime boundary, usable without QApplication or any viewer.
// Standalone ingestion accepts delivered mailbox batches; worker-owned capture
// sources can also be retained here without copying their records.
class ObservationSession {
public:
    ObservationSession() { beginRun(); }
    void beginRun(std::uint64_t id = 0) {
        if (source_) source_->finish(true);
        source_ = std::make_shared<ObservationSource>(id, StorageOptions{20'000,
            qEnvironmentVariable("S7STUDIO_RECORDING_TEMP_DIR"), {}, {}});
    }
    bool append(const ScopeSampleBatch& batch) {
        for (const auto& sample : batch.samples)
            if (sample.runId != batch.runId) return false;
        if (source_->runId() != batch.runId) {
            if (source_->runId() != 0 || source_->status().samples != 0) return false;
            beginRun(batch.runId);
        }
        for (const auto& sample : batch.samples) {
            const auto channel = source_->channel(QString::fromStdString(sample.probeName));
            source_->append({channel, sample.step, sample.timeSeconds, sample.value});
        }
        source_->addCaptureLoss(batch.dropped);
        return true;
    }
    void retain(std::shared_ptr<const ObservationSource> source, const QString& label) {
        if (!source || source->status().samples == 0) return;
        for (const auto& run : retained_) if (run.source == source) return;
        retained_.push_back({label, std::move(source)});
    }
    [[nodiscard]] const QVector<RetainedRun>& retainedRuns() const { return retained_; }
    void clearHistory() { retained_.clear(); }
    [[nodiscard]] std::shared_ptr<const ObservationSource> source() const { return source_; }
private:
    std::shared_ptr<ObservationSource> source_;
    QVector<RetainedRun> retained_;
};
} // namespace s7studio::observation
