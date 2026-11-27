// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "s7/runtime.hpp"

#include <mutex>

namespace s7 {

struct ArrayMailboxCounters {
    std::uint64_t replaced{};
    std::uint64_t admission_skips{};
};

// One subscription's latest admitted result. The stepping thread offers
// committed samples. The viewer can keep a taken sample after close().
class ArrayMailbox {
public:
    void offer(std::expected<CommittedArraySample, ArrayPoolError> sample);
    [[nodiscard]] std::optional<CommittedArraySample> take();
    // Marks the pending value presented and keeps it available for reopening.
    [[nodiscard]] std::optional<CommittedArraySample> latest();
    // Release the queued value before retrying admission in a one-slot pool.
    [[nodiscard]] bool discard_pending();
    void set_paused(bool paused);
    void close();
    [[nodiscard]] ArrayMailboxCounters counters() const;

private:
    mutable std::mutex mutex_;
    std::optional<CommittedArraySample> pending_;
    ArrayMailboxCounters counters_;
    bool paused_{};
    bool closed_{};
    bool presented_{};
};

} // namespace s7
