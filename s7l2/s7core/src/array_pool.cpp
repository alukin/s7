// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "s7/array_pool.hpp"

#include <algorithm>
#include <cassert>
#include <limits>
#include <mutex>
#include <utility>
#include <vector>

namespace s7 {
namespace {

constexpr auto max_size = std::numeric_limits<std::size_t>::max();

std::size_t checked_slot_count(const ArrayDescriptor& descriptor, std::size_t execution, std::size_t optional)
{
    if (descriptor.rank() == 0 || execution == 0) {
        throw std::invalid_argument("An array pool requires an array descriptor and execution slots.");
    }
    if (optional > max_size - execution || execution + optional > max_size / descriptor.byte_size()) {
        throw std::overflow_error("Array pool capacity overflows size_t.");
    }
    return execution + optional;
}

std::vector<std::size_t> free_indices(const std::size_t count)
{
    std::vector<std::size_t> indices(count);
    for (std::size_t index = 0; index < count; ++index) indices[index] = count - index - 1;
    return indices;
}

} // namespace

void ArrayMemoryBudget::reserve(const std::size_t bytes)
{
    auto used = reserved_.load();
    do {
        if (bytes > limit_bytes_ - used) throw std::length_error("Array pool memory budget exhausted.");
    } while (!reserved_.compare_exchange_weak(used, used + bytes));
}

void ArrayMemoryBudget::release(const std::size_t bytes) noexcept
{
    reserved_.fetch_sub(bytes);
}

struct ArrayPool::State {
    struct Reservation {
        Reservation(std::shared_ptr<ArrayMemoryBudget> budget, const std::size_t bytes)
            : budget(std::move(budget)), bytes(bytes)
        {
            if (!this->budget) throw std::invalid_argument("An array pool requires a memory budget.");
            this->budget->reserve(bytes);
        }
        ~Reservation() { budget->release(bytes); }

        const std::shared_ptr<ArrayMemoryBudget> budget;
        const std::size_t bytes;
    };

    struct Slot {
        std::size_t graph_refs{};
        std::size_t retention_tickets{};
    };

    State(ArrayDescriptor shape, const std::size_t execution, const std::size_t optional,
          std::shared_ptr<ArrayMemoryBudget> budget)
        : descriptor(std::move(shape)), slot_bytes(descriptor.byte_size()),
          execution_limit(execution), optional_limit(optional),
          slot_count(checked_slot_count(descriptor, execution, optional)),
          reservation(std::move(budget), slot_count * slot_bytes),
          slots(slot_count), free_slots(free_indices(slot_count))
    {
        // Construct native signal objects at admission. Lease access may then
        // legally expose typed spans over the same objects on every reuse.
        visit_signal_type(descriptor.type(), [&]<SignalType Type> {
            using Storage = signal_storage_t<Type>;
            const auto count = slot_count * descriptor.element_count();
            auto owner = std::make_unique_for_overwrite<Storage[]>(count);
            backing = std::as_writable_bytes(std::span(owner.get(), count));
            allocation = {owner.release(), [](void* data) { delete[] static_cast<Storage*>(data); }};
        });
    }

    [[nodiscard]] std::span<std::byte> bytes(const std::size_t slot) const noexcept
    {
        return backing.subspan(slot * slot_bytes, slot_bytes);
    }

    // Called under the pool mutex after dropping a graph reference or ticket.
    void recycle(const std::size_t slot)
    {
        if (slots[slot].graph_refs == 0 && slots[slot].retention_tickets == 0) free_slots.push_back(slot);
    }

    // Called under the pool mutex. A writer counts as one graph reference.
    void release_graph(const std::size_t slot)
    {
        if (--slots[slot].graph_refs == 0) {
            --active_graph_slots;
            recycle(slot);
        }
    }

    const ArrayDescriptor descriptor;
    const std::size_t slot_bytes;
    const std::size_t execution_limit;
    const std::size_t optional_limit;
    const std::size_t slot_count;
    const Reservation reservation;
    std::unique_ptr<void, void (*)(void*)> allocation{nullptr, nullptr};
    std::span<std::byte> backing;
    std::mutex mutex;
    std::vector<Slot> slots;
    std::vector<std::size_t> free_slots;
    std::size_t active_graph_slots{};
    std::size_t admitted_optional_slots{};
    bool closed{};
};

struct ArrayPool::RetentionState {
    RetentionState(std::shared_ptr<State> pool, const std::size_t capacity)
        : pool(std::move(pool)), capacity(capacity), references(capacity), free_tickets(free_indices(capacity))
    {
        std::lock_guard lock(this->pool->mutex);
        if (this->pool->closed) throw std::logic_error("Array pool is closed.");
        if (capacity > this->pool->optional_limit - this->pool->admitted_optional_slots) {
            throw std::length_error("Array pool has insufficient optional retention headroom.");
        }
        this->pool->admitted_optional_slots += capacity;
    }

    ~RetentionState()
    {
        std::lock_guard lock(pool->mutex);
        pool->admitted_optional_slots -= capacity;
    }

    const std::shared_ptr<State> pool;
    const std::size_t capacity;
    std::vector<std::size_t> references;
    std::vector<std::size_t> free_tickets;
    std::size_t skipped{};
    bool closed{};
};

ArrayPool::ArrayPool(ArrayDescriptor descriptor, const std::size_t execution_slots,
                     const std::size_t optional_slots, std::shared_ptr<ArrayMemoryBudget> budget)
    : state_(std::make_shared<State>(std::move(descriptor), execution_slots, optional_slots, std::move(budget))) {}

ArrayPool::~ArrayPool() { close(); }

std::expected<ArrayPool::WriteLease, ArrayPoolError> ArrayPool::try_acquire()
{
    std::lock_guard lock(state_->mutex);
    if (state_->closed) return std::unexpected(ArrayPoolError::Closed);
    if (state_->active_graph_slots == state_->execution_limit) return std::unexpected(ArrayPoolError::Exhausted);
    assert(!state_->free_slots.empty()); // optional tickets cannot consume the execution reserve
    const auto slot = state_->free_slots.back();
    state_->free_slots.pop_back();
    state_->slots[slot].graph_refs = 1;
    ++state_->active_graph_slots;
    return WriteLease(state_, slot);
}

ArrayPool::Retention ArrayPool::make_retention(const std::size_t max_slots, const std::size_t max_bytes)
{
    const auto capacity = std::min(max_slots, max_bytes / state_->slot_bytes);
    if (capacity > state_->optional_limit) {
        throw std::length_error("Array pool has insufficient optional retention headroom.");
    }
    return Retention(std::make_shared<RetentionState>(state_, capacity));
}

void ArrayPool::close() noexcept
{
    std::lock_guard lock(state_->mutex);
    state_->closed = true;
}

ArrayPool::WriteLease::WriteLease(std::shared_ptr<State> state, const std::size_t slot) noexcept
    : state_(std::move(state)), slot_(slot) {}

ArrayPool::WriteLease& ArrayPool::WriteLease::operator=(WriteLease other) noexcept
{
    state_.swap(other.state_);
    std::swap(slot_, other.slot_);
    return *this;
}

ArrayPool::WriteLease::~WriteLease()
{
    if (!state_) return;
    std::lock_guard lock(state_->mutex);
    state_->release_graph(slot_);
}

const ArrayDescriptor& ArrayPool::WriteLease::descriptor() const &
{
    if (!state_) throw std::logic_error("Empty array write lease.");
    return state_->descriptor;
}

std::span<std::byte> ArrayPool::WriteLease::bytes() &
{
    if (!state_) throw std::logic_error("Empty array write lease.");
    return state_->bytes(slot_);
}

ArrayPool::ReadLease ArrayPool::WriteLease::seal() &&
{
    if (!state_) throw std::logic_error("Empty array write lease.");
    return ReadLease(std::move(state_), slot_);
}

ArrayPool::ReadLease::ReadLease(std::shared_ptr<State> state, const std::size_t slot,
                              std::shared_ptr<RetentionState> retention, const std::size_t ticket) noexcept
    : state_(std::move(state)), retention_(std::move(retention)), slot_(slot), ticket_(ticket) {}

ArrayPool::ReadLease::ReadLease(const ReadLease& other)
    : slot_(other.slot_), ticket_(other.ticket_)
{
    if (!other.state_) return;
    std::lock_guard lock(other.state_->mutex);
    state_ = other.state_;
    retention_ = other.retention_;
    if (retention_) ++retention_->references[ticket_];
    else ++state_->slots[slot_].graph_refs;
}

ArrayPool::ReadLease& ArrayPool::ReadLease::operator=(ReadLease other) noexcept
{
    state_.swap(other.state_);
    retention_.swap(other.retention_);
    std::swap(slot_, other.slot_);
    std::swap(ticket_, other.ticket_);
    return *this;
}

ArrayPool::ReadLease::~ReadLease()
{
    if (!state_) return;
    std::lock_guard lock(state_->mutex);
    if (!retention_) {
        state_->release_graph(slot_);
    } else if (--retention_->references[ticket_] == 0) {
        retention_->free_tickets.push_back(ticket_);
        --state_->slots[slot_].retention_tickets;
        state_->recycle(slot_);
    }
}

DenseCpuArrayView ArrayPool::ReadLease::view() const &
{
    if (!state_) throw std::logic_error("Empty array read lease.");
    return DenseCpuArrayView(state_->descriptor, state_->bytes(slot_));
}

ArrayPool::Retention::Retention(std::shared_ptr<RetentionState> state) noexcept
    : state_(std::move(state)) {}

ArrayPool::Retention& ArrayPool::Retention::operator=(Retention other) noexcept
{
    state_.swap(other.state_);
    return *this;
}

ArrayPool::Retention::~Retention() { close(); }

std::expected<ArrayPool::ReadLease, ArrayPoolError> ArrayPool::Retention::try_retain(const ReadLease& source) const
{
    if (!state_) return std::unexpected(ArrayPoolError::Closed);
    auto& pool = *state_->pool;
    if (source.state_.get() != &pool) return std::unexpected(ArrayPoolError::InvalidLease);
    std::lock_guard lock(pool.mutex);
    if (state_->closed || pool.closed) return std::unexpected(ArrayPoolError::Closed);
    if (state_->free_tickets.empty()) {
        if (state_->skipped != max_size) ++state_->skipped;
        return std::unexpected(ArrayPoolError::Exhausted);
    }
    const auto ticket = state_->free_tickets.back();
    state_->free_tickets.pop_back();
    state_->references[ticket] = 1;
    ++pool.slots[source.slot_].retention_tickets;
    return ReadLease(state_->pool, source.slot_, state_, ticket);
}

std::size_t ArrayPool::Retention::skipped() const
{
    if (!state_) return 0;
    std::lock_guard lock(state_->pool->mutex);
    return state_->skipped;
}

void ArrayPool::Retention::close() noexcept
{
    if (!state_) return;
    std::lock_guard lock(state_->pool->mutex);
    state_->closed = true;
}

} // namespace s7
