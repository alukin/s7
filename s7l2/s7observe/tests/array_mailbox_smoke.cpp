// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "s7/array_mailbox.hpp"

#include <array>
#include <barrier>
#include <cassert>
#include <thread>

int main()
{
    using namespace s7;
    const ArrayDescriptor shape(SignalType::U8, std::array<std::int64_t, 1>{1});
    ArrayPool pool(shape, 1, 3, std::make_shared<ArrayMemoryBudget>(4 * shape.byte_size()));
    auto admission = pool.make_retention(3, 3 * shape.byte_size());
    const auto rate = Rate::from_hz(100);
    ArrayMailbox mailbox;
    const auto capture = [&](std::uint64_t step) -> std::expected<CommittedArraySample, ArrayPoolError> {
        auto writer = pool.try_acquire().value();
        writer.values<SignalType::U8>()[0] = static_cast<std::uint8_t>(step);
        const auto frame = std::move(writer).seal();
        auto lease = admission.try_retain(frame);
        if (!lease) return std::unexpected(lease.error());
        return CommittedArraySample{rate, step, {std::move(*lease), ArraySourceOccurrence{2, 0, step}}};
    };

    mailbox.offer(capture(1));
    std::barrier exchange(2);
    std::jthread viewer([&] {
        const auto held = mailbox.take();
        assert(held && held->domain_step == 1);
        exchange.arrive_and_wait();
        exchange.arrive_and_wait();
        assert(held->value.lease.values<SignalType::U8>()[0] == 1);
        assert(!mailbox.take());
    });
    exchange.arrive_and_wait();

    mailbox.set_paused(true);
    for (std::uint64_t step = 2; step <= 10; ++step) mailbox.offer(capture(step));
    assert(!mailbox.take());
    auto other_reader = capture(11).value();
    mailbox.offer(capture(12));
    const auto present = mailbox.counters();
    assert(present.replaced == 8 && present.admission_skips == 1);

    mailbox.set_paused(false);
    const auto newest = mailbox.take();
    assert(newest && newest->domain_step == 10 && newest->rate == rate &&
           newest->value.origin == ArraySourceOccurrence(2, 0, 10));
    other_reader.value = {};
    mailbox.offer(capture(13));
    mailbox.close();
    mailbox.offer(capture(14));
    exchange.arrive_and_wait();
    viewer.join();

    admission.close();
    assert(capture(15).error() == ArrayPoolError::Closed);
    assert(newest->value.lease.values<SignalType::U8>()[0] == 10);
}
