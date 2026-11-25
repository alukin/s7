// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-S7-plugin-permission

#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace s7studio::observation {

// Probe values of consecutive steps, filled on the stepping thread.
struct ProbeBlock {
    std::vector<std::int64_t> steps;
    std::vector<double> times;
    std::vector<double> values; // values[frame * width + probe]
    std::size_t frames = 0;

    void reserve(std::size_t capacity, std::size_t width)
    {
        steps.resize(capacity);
        times.resize(capacity);
        values.resize(capacity * width);
        frames = 0;
    }
};

// Hands probe blocks from the stepping thread (single producer) to one
// distributor thread (single consumer) (performance plan WP3, D1, D3).
// Every block is allocated up front. The producer fills its own block and
// publishes it by swapping it with a free slot, so publication copies nothing
// and frames never take a lock. The mutex and condition variables are
// touched once per block, only to wake a waiting thread.
class ProbeBlockRing {
public:
    ProbeBlockRing(std::size_t slotCount, std::size_t frames, std::size_t width)
        : slots_(slotCount), capacity_(frames), width_(width)
    {
        for (auto& slot : slots_) slot.reserve(frames, width);
        current_.reserve(frames, width);
    }

    [[nodiscard]] std::size_t capacity() const { return capacity_; }
    [[nodiscard]] std::size_t width() const { return width_; }

    // ---- producer ------------------------------------------------------------
    [[nodiscard]] ProbeBlock& current() { return current_; }

    // Publishes the current block if a slot is free. Returns false when the
    // ring is full; the block then stays current.
    bool tryPublish()
    {
        const auto write = write_.load(std::memory_order_relaxed);
        if (write - read_.load(std::memory_order_acquire) == slots_.size()) return false;
        std::swap(current_, slots_[write % slots_.size()]);
        current_.frames = 0;
        write_.store(write + 1, std::memory_order_release);
        { std::lock_guard lock(mutex_); }
        readable_.notify_one();
        return true;
    }
    // Waits for a free slot and publishes (lossless). Returns the time spent
    // waiting; `published` is false when the consumer failed instead.
    std::chrono::nanoseconds publishWaiting(bool& published)
    {
        const auto start = std::chrono::steady_clock::now();
        published = false;
        while (!(published = tryPublish())) {
            std::unique_lock lock(mutex_);
            writable_.wait(lock, [this] {
                return failed_.load() || write_.load() - read_.load() < slots_.size();
            });
            if (failed_.load()) break;
        }
        return std::chrono::steady_clock::now() - start;
    }
    // Waits until the consumer has taken every published block (or failed):
    // after it returns, all published frames have reached their destinations.
    void waitDrained()
    {
        std::unique_lock lock(mutex_);
        writable_.wait(lock, [this] { return failed_.load() || read_.load() == write_.load(); });
    }
    // No more blocks will be published.
    void close()
    {
        { std::lock_guard lock(mutex_); closed_ = true; }
        readable_.notify_all();
    }

    // ---- consumer ------------------------------------------------------------
    // The next published block, or nullptr once closed and drained.
    ProbeBlock* waitReadable()
    {
        std::unique_lock lock(mutex_);
        readable_.wait(lock, [this] { return read_.load() < write_.load() || closed_; });
        if (read_.load() == write_.load()) return nullptr;
        return &slots_[read_.load() % slots_.size()];
    }
    void release()
    {
        read_.store(read_.load(std::memory_order_relaxed) + 1, std::memory_order_release);
        { std::lock_guard lock(mutex_); }
        writable_.notify_one();
    }
    // The consumer can take no more data (e.g. a storage error); a waiting
    // producer wakes and stops publishing.
    void fail(std::string error)
    {
        {
            std::lock_guard lock(mutex_);
            if (!failed_.load()) error_ = std::move(error);
            failed_.store(true);
        }
        writable_.notify_all();
    }
    [[nodiscard]] bool failed() const { return failed_.load(); }
    [[nodiscard]] std::string error() const
    {
        std::lock_guard lock(mutex_);
        return error_;
    }

private:
    std::vector<ProbeBlock> slots_;
    ProbeBlock current_;
    const std::size_t capacity_, width_;
    std::atomic<std::uint64_t> write_{0}, read_{0};
    std::atomic<bool> failed_{false};
    mutable std::mutex mutex_;
    std::condition_variable readable_, writable_;
    bool closed_ = false;
    std::string error_;
};

} // namespace s7studio::observation
