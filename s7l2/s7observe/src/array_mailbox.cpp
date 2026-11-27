// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "s7/array_mailbox.hpp"

#include <limits>

namespace s7 {
namespace {
void increment(std::uint64_t& counter)
{
    if (counter != std::numeric_limits<std::uint64_t>::max()) ++counter;
}
} // namespace

void ArrayMailbox::offer(std::expected<CommittedArraySample, ArrayPoolError> sample)
{
    // Release a replaced lease after leaving the mailbox lock.
    std::optional<CommittedArraySample> previous;
    {
        const std::lock_guard lock(mutex_);
        if (closed_) return;
        if (!sample) {
            increment(counters_.admission_skips);
            return;
        }
        previous = std::exchange(pending_, std::move(*sample));
        if (previous && !presented_) increment(counters_.replaced);
        presented_ = false;
    }
}

std::optional<CommittedArraySample> ArrayMailbox::take()
{
    const std::lock_guard lock(mutex_);
    if (paused_ || closed_) return std::nullopt;
    return std::exchange(pending_, std::nullopt);
}

std::optional<CommittedArraySample> ArrayMailbox::latest()
{
    const std::lock_guard lock(mutex_);
    if (paused_ || closed_ || !pending_) return std::nullopt;
    presented_ = true;
    return pending_;
}

bool ArrayMailbox::discard_pending()
{
    std::optional<CommittedArraySample> previous;
    {
        const std::lock_guard lock(mutex_);
        previous = std::exchange(pending_, std::nullopt);
        if (previous && !presented_) increment(counters_.replaced);
    }
    return previous.has_value();
}

void ArrayMailbox::set_paused(bool paused)
{
    const std::lock_guard lock(mutex_);
    paused_ = paused;
}

void ArrayMailbox::close()
{
    std::optional<CommittedArraySample> previous;
    {
        const std::lock_guard lock(mutex_);
        closed_ = true;
        previous = std::exchange(pending_, std::nullopt);
    }
}

ArrayMailboxCounters ArrayMailbox::counters() const
{
    const std::lock_guard lock(mutex_);
    return counters_;
}

} // namespace s7
