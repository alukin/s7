// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-S7-plugin-permission

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
    std::uint64_t runId = 0;
};

struct ScopeSampleBatch {
    std::vector<ScopeSample> samples; // in production order
    std::uint64_t dropped = 0;        // discarded since the previous batch
    std::uint64_t runId = 0;
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
            if (sample.runId != runId_) return false;
            if (samples_.size() >= capacity_) {
                samples_.pop_front();
                ++dropped_;
            }
            samples_.push_back(std::move(sample));
        }
        return !notifyPending_.exchange(true);
    }

    // Worker thread: a whole block under one lock, oldest-first. Keeps at
    // most `capacity` samples, dropping (and counting) the oldest.
    bool push(std::vector<ScopeSample>& batch)
    {
        if (batch.empty()) return false;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (batch.front().runId != runId_) return false;
            auto first = batch.begin();
            if (batch.size() > capacity_) { // only the newest fit
                dropped_ += samples_.size() + (batch.size() - capacity_);
                samples_.clear();
                first += static_cast<std::ptrdiff_t>(batch.size() - capacity_);
            } else if (samples_.size() + batch.size() > capacity_) {
                const auto excess = samples_.size() + batch.size() - capacity_;
                samples_.erase(samples_.begin(), samples_.begin() + static_cast<std::ptrdiff_t>(excess));
                dropped_ += excess;
            }
            samples_.insert(samples_.end(), std::make_move_iterator(first), std::make_move_iterator(batch.end()));
        }
        return !notifyPending_.exchange(true);
    }

    // UI thread. Takes everything queued so far and re-arms notification.
    auto take() -> ScopeSampleBatch
    {
        notifyPending_.store(false);
        ScopeSampleBatch batch;
        std::deque<ScopeSample> taken;
        {
            // O(1) under the lock, so the producer never waits for the copy.
            std::lock_guard<std::mutex> lock(mutex_);
            taken.swap(samples_);
            batch.dropped = std::exchange(dropped_, 0);
            batch.runId = runId_;
        }
        batch.samples.assign(std::make_move_iterator(taken.begin()), std::make_move_iterator(taken.end()));
        return batch;
    }

    void clear(std::uint64_t runId = 0)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        samples_.clear();
        runId_ = runId;
        dropped_ = 0;
        notifyPending_.store(false);
    }

private:
    std::size_t capacity_;
    std::mutex mutex_;
    std::deque<ScopeSample> samples_;
    std::uint64_t dropped_ = 0;
    std::uint64_t runId_ = 0;
    std::atomic<bool> notifyPending_{false};
};

} // namespace s7studio
