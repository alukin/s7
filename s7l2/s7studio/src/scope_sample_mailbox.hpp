#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace s7studio {

struct ScopeSample {
    std::string probeName;
    long step = 0;
    double timeSeconds = 0.0;
    double value = 0.0;
};

struct ScopeSampleBatch {
    std::vector<ScopeSample> samples; // in production order
    std::uint64_t dropped = 0;        // discarded since the previous batch
};

// Hands Scope samples from the runtime worker to the UI thread with bounded
// memory. The worker pushes; when the mailbox is full the oldest samples are
// dropped and counted, so a UI that cannot keep up sees the newest data and
// the drop count instead of an ever-growing event backlog. At most one UI
// notification is outstanding at a time.
class ScopeSampleMailbox {
public:
    explicit ScopeSampleMailbox(std::size_t capacity = 200'000) : capacity_(capacity) {}

    // Worker thread. Returns true if the caller should notify the UI.
    bool push(ScopeSample sample)
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (samples_.size() >= capacity_) {
                samples_.pop_front();
                ++dropped_;
            }
            samples_.push_back(std::move(sample));
        }
        return !notifyPending_.exchange(true);
    }

    // UI thread. Takes everything queued so far and re-arms notification.
    auto take() -> ScopeSampleBatch
    {
        notifyPending_.store(false);
        ScopeSampleBatch batch;
        std::lock_guard<std::mutex> lock(mutex_);
        batch.samples.assign(std::make_move_iterator(samples_.begin()), std::make_move_iterator(samples_.end()));
        samples_.clear();
        batch.dropped = std::exchange(dropped_, 0);
        return batch;
    }

    void clear()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        samples_.clear();
        dropped_ = 0;
        notifyPending_.store(false);
    }

private:
    std::size_t capacity_;
    std::mutex mutex_;
    std::deque<ScopeSample> samples_;
    std::uint64_t dropped_ = 0;
    std::atomic<bool> notifyPending_{false};
};

} // namespace s7studio
