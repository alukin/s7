// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "s7/array_pool.hpp"

#include <algorithm>
#include <array>
#include <future>
#include <iostream>
#include <limits>
#include <string>
#include <type_traits>
#include <utility>

namespace {
using s7::ArrayDescriptor;
using s7::ArrayMemoryBudget;
using s7::ArrayPool;
using s7::ArrayPoolError;
using s7::SignalType;

static_assert(!std::is_copy_constructible_v<ArrayPool::WriteLease>);
static_assert(!std::is_copy_constructible_v<ArrayPool::Retention>);
static_assert(std::is_same_v<decltype(std::declval<const ArrayPool::ReadLease&>().values<SignalType::F64>()),
                             std::span<const double>>);

void require(const bool condition, const std::string_view message)
{
    if (!condition) throw std::runtime_error(std::string(message));
}

template <typename Exception, typename Operation>
void rejects(Operation operation, std::string_view reason)
{
    try { operation(); }
    catch (const Exception&) { return; }
    throw std::runtime_error(std::string(reason));
}

const ArrayDescriptor shape(SignalType::F64, std::array<std::int64_t, 2>{2, 3});

ArrayPool::ReadLease frame(ArrayPool& pool, const double value)
{
    auto writer = pool.try_acquire().value();
    std::ranges::fill(writer.values<SignalType::F64>(), value);
    return std::move(writer).seal();
}

void check_budget_and_lifetime()
{
    const auto bytes = shape.byte_size();
    const auto budget = std::make_shared<ArrayMemoryBudget>(bytes * 2);
    ArrayPool::ReadLease retained;
    {
        ArrayPool pool(shape, 1, 1, budget);
        require(budget->reserved_bytes() == bytes * 2, "charge full pool capacity at admission");
        rejects<std::length_error>([&] { ArrayPool second(shape, 1, 0, budget); }, "shared budget overrun");
        rejects<std::overflow_error>([&] { ArrayPool excessive(shape, 1, std::numeric_limits<std::size_t>::max(), budget); },
                                     "slot addition overflow");
        rejects<std::overflow_error>([&] { ArrayPool excessive(shape, std::numeric_limits<std::size_t>::max(), 0, budget); },
                                     "payload capacity overflow");
        rejects<std::invalid_argument>([&] { ArrayPool scalar(ArrayDescriptor(SignalType::F64), 1, 0, budget); },
                                       "scalar storage uses its native path");
        require(budget->reserved_bytes() == bytes * 2, "failed admissions leave the budget unchanged");

        auto writer = pool.try_acquire().value();
        rejects<std::invalid_argument>([&] { static_cast<void>(writer.values<SignalType::F32>()); }, "typed write checks dtype");
        auto values = writer.values<SignalType::F64>();
        require(values.size() == 6 && reinterpret_cast<std::uintptr_t>(values.data()) % alignof(double) == 0,
                "typed payload is aligned");
        std::ranges::fill(values, 42.0);
        const auto* address = values.data();
        auto graph = std::move(writer).seal();
        require(!writer && graph.values<SignalType::F64>().data() == address, "sealing consumes the writer without copying");
        rejects<std::logic_error>([&] { static_cast<void>(writer.bytes()); }, "consumed writer cannot access a slot");
        rejects<std::invalid_argument>([&] { static_cast<void>(graph.values<SignalType::F32>()); }, "typed read checks dtype");
        auto observer = pool.make_retention(1, bytes);
        retained = observer.try_retain(graph).value();
        require(retained.values<SignalType::F64>().data() == address, "retention shares the sealed payload");
        pool.close();
        require(pool.try_acquire().error() == ArrayPoolError::Closed &&
                observer.try_retain(graph).error() == ArrayPoolError::Closed, "close rejects new work");
    }
    require(retained.values<SignalType::F64>().back() == 42.0 && budget->reserved_bytes() == bytes * 2,
            "outstanding read keeps preallocated storage and budget alive");
    retained = {};
    require(budget->reserved_bytes() == 0, "last lease returns the reservation");
}

void check_admission()
{
    const auto bytes = shape.byte_size();
    const auto budget = std::make_shared<ArrayMemoryBudget>(bytes * 4);
    ArrayPool pool(shape, 1, 2, budget);
    ArrayPool other(shape, 1, 0, budget);
    auto current = frame(pool, 7);
    auto source_elsewhere = frame(other, 11);
    auto viewer = pool.make_retention(10, bytes);
    auto print = pool.make_retention(1, bytes);
    auto no_room = pool.make_retention(1, bytes - 1);
    rejects<std::length_error>([&] { static_cast<void>(pool.make_retention(1, bytes)); },
                               "per-pool optional limits cannot overbook headroom");
    require(viewer.try_retain(source_elsewhere).error() == ArrayPoolError::InvalidLease &&
            viewer.try_retain({}).error() == ArrayPoolError::InvalidLease, "foreign and empty leases cannot be retained");
    require(no_room.try_retain(current).error() == ArrayPoolError::Exhausted && no_room.skipped() == 1,
            "admission charges the full slot against the byte limit");
    auto retained = viewer.try_retain(current).value();
    auto copy = retained;
    auto moved = std::move(retained);
    require(!retained && moved.view().bytes().data() == copy.view().bytes().data(), "copies share one ticket");
    rejects<std::logic_error>([&] { static_cast<void>(retained.view()); }, "moved-from reader cannot access reused slots");
    moved = {};
    require(viewer.try_retain(current).error() == ArrayPoolError::Exhausted, "remaining copy keeps the admission charged");
    copy = {};
    require(viewer.try_retain(current).has_value(), "last copy recycles the ticket");

    auto snapshot = print.try_retain(current).value();
    print.close();
    require(snapshot.values<SignalType::F64>().front() == 7 &&
            print.try_retain(current).error() == ArrayPoolError::Closed, "cancelled admission preserves the active snapshot");
    { auto cancelled = std::move(print); }
    rejects<std::length_error>([&] { static_cast<void>(pool.make_retention(1, bytes)); },
                               "a live snapshot still reserves headroom after admission destruction");
    snapshot = {};
    require(pool.make_retention(1, bytes).skipped() == 0, "the last snapshot releases headroom");
    require(pool.try_acquire().error() == ArrayPoolError::Exhausted,
            "execution cannot borrow optional headroom to mask an underestimated reserve");
    auto alias = current;
    current = {};
    require(pool.try_acquire().error() == ArrayPoolError::Exhausted && alias.values<SignalType::F64>().front() == 7,
            "graph aliases keep the execution slot live");
    alias = {};
    { auto abandoned = pool.try_acquire().value(); }
    require(pool.try_acquire().has_value(), "abandoning an unpublished writer recycles its slot");
}

void check_storage_types()
{
    for (const auto type : s7::kAllSignalTypes) {
        const ArrayDescriptor descriptor(type, std::array<std::int64_t, 1>{3});
        auto budget = std::make_shared<ArrayMemoryBudget>(descriptor.byte_size() * 2);
        ArrayPool pool(descriptor, 2, 0, budget);
        auto first = pool.try_acquire().value();
        auto second = pool.try_acquire().value();
        s7::visit_signal_type(type, [&]<SignalType Type> {
            const auto storage = second.values<Type>();
            std::ranges::fill(storage, s7::signal_storage_t<Type>{});
            std::ranges::fill(first.values<Type>(), s7::signal_storage_t<Type>{});
            const auto read = std::move(second).seal();
            require(read.values<Type>().data() == storage.data() &&
                    reinterpret_cast<std::uintptr_t>(storage.data()) % alignof(s7::signal_storage_t<Type>) == 0,
                    "every slot satisfies native signal type alignment");
        });
    }
}

void check_slow_reader()
{
    const auto budget = std::make_shared<ArrayMemoryBudget>(shape.byte_size() * 3);
    ArrayPool pool(shape, 2, 1, budget);
    auto viewer = pool.make_retention(1, shape.byte_size());
    auto initial = frame(pool, -1);
    const auto held = viewer.try_retain(initial).value();
    initial = {};
    const auto* address = held.view().bytes().data();

    constexpr std::size_t iterations = 2'000;
    auto producer = std::async(std::launch::async, [&] {
        ArrayPool::ReadLease current;
        for (std::size_t index = 0; index < iterations; ++index) {
            auto next = frame(pool, static_cast<double>(index));
            require(next.view().bytes().data() != address, "retained storage must not be reused");
            require(viewer.try_retain(next).error() == ArrayPoolError::Exhausted, "slow reader causes a skip");
            current = std::move(next);
        }
    });
    for (std::size_t index = 0; index < iterations; ++index) {
        const auto copy = held;
        require(copy.values<SignalType::F64>().front() == -1, "concurrent readers see immutable bytes");
    }
    producer.get();
    require(viewer.skipped() == iterations, "optional exhaustion counts skipped admissions");
}

} // namespace

int main() try {
    check_budget_and_lifetime();
    check_admission();
    check_slow_reader();
    check_storage_types();
    return 0;
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
}
