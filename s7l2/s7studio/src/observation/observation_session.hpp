#pragma once
#include "observation/observation_source.hpp"
#include "scope_sample_mailbox.hpp"

namespace s7studio::observation {
// Host-side lifetime boundary, usable without QApplication or any viewer.
// This increment accepts only already-delivered mailbox batches.
class ObservationSession {
public:
    ObservationSession() { beginRun(); }
    void beginRun(std::uint64_t id = 0) {
        source_ = std::make_shared<ObservationSource>(id, 20'000,
            qEnvironmentVariable("S7STUDIO_RECORDING_TEMP_DIR"));
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
        source_->addDeliveryLoss(batch.dropped);
        return true;
    }
    [[nodiscard]] std::shared_ptr<const ObservationSource> source() const { return source_; }
private:
    std::shared_ptr<ObservationSource> source_;
};
} // namespace s7studio::observation
