// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: LGPL-3.0-or-later

#ifndef S7CORE_INCLUDE_S7_ARRAY_POOL_HPP
#define S7CORE_INCLUDE_S7_ARRAY_POOL_HPP

#include "s7/array_descriptor.hpp"

#include <atomic>
#include <expected>
#include <memory>
#include <stdexcept>

namespace s7 {

/// Shared admission budget for the full payload capacity of admitted pools.
class ArrayMemoryBudget {
public:
    explicit ArrayMemoryBudget(std::size_t limit_bytes) : limit_bytes_(limit_bytes) {}
    [[nodiscard]] std::size_t reserved_bytes() const noexcept { return reserved_.load(); }

private:
    friend class ArrayPool;
    void reserve(std::size_t bytes);
    void release(std::size_t bytes) noexcept;

    const std::size_t limit_bytes_;
    std::atomic<std::size_t> reserved_{0};
};

enum class ArrayPoolError { Exhausted, Closed, InvalidLease };

/// Preallocated CPU slots for one fixed array descriptor. Distinct handles
/// can be used on different threads. Borrowed spans and views are valid only
/// while a corresponding lease is live and the access mode still applies.
class ArrayPool {
    struct State;
    struct RetentionState;

public:
    class WriteLease;
    class Retention;

    class ReadLease {
    public:
        ReadLease() = default;
        ReadLease(const ReadLease& other);
        ReadLease(ReadLease&&) noexcept = default;
        ReadLease& operator=(ReadLease other) noexcept;
        ~ReadLease();

        explicit operator bool() const noexcept { return static_cast<bool>(state_); }
        [[nodiscard]] DenseCpuArrayView view() const &;
        DenseCpuArrayView view() const && = delete;

        template <SignalType Type>
        [[nodiscard]] std::span<const signal_storage_t<Type>> values() const &
        {
            const auto data = view();
            if (data.descriptor().type() != Type) throw std::invalid_argument("Array lease type mismatch.");
            return {reinterpret_cast<const signal_storage_t<Type>*>(data.bytes().data()),
                    data.descriptor().element_count()};
        }
        template <SignalType Type>
        std::span<const signal_storage_t<Type>> values() const && = delete;

    private:
        friend class ArrayOutputSlot;
        friend class WriteLease;
        friend class Retention;
        ReadLease(std::shared_ptr<State> state, std::size_t slot,
                  std::shared_ptr<RetentionState> retention = {}, std::size_t ticket = 0) noexcept;

        std::shared_ptr<State> state_;
        std::shared_ptr<RetentionState> retention_;
        std::size_t slot_{};
        std::size_t ticket_{};
    };

    class WriteLease {
    public:
        WriteLease() = default;
        WriteLease(WriteLease&&) noexcept = default;
        WriteLease& operator=(WriteLease other) noexcept;
        ~WriteLease();

        explicit operator bool() const noexcept { return static_cast<bool>(state_); }
        [[nodiscard]] const ArrayDescriptor& descriptor() const &;
        const ArrayDescriptor& descriptor() const && = delete;
        [[nodiscard]] std::span<std::byte> bytes() &;

        template <SignalType Type>
        [[nodiscard]] std::span<signal_storage_t<Type>> values() &
        {
            const auto& shape = descriptor();
            if (shape.type() != Type) throw std::invalid_argument("Array lease type mismatch.");
            return {reinterpret_cast<signal_storage_t<Type>*>(bytes().data()), shape.element_count()};
        }

        /// The writer must initialize the full payload on each acquisition.
        /// Sealing consumes writable ownership and expires all writable spans.
        [[nodiscard]] ReadLease seal() &&;

    private:
        friend class ArrayPool;
        WriteLease(std::shared_ptr<State> state, std::size_t slot) noexcept;
        std::shared_ptr<State> state_;
        std::size_t slot_{};
    };

    /// Per-consumer optional retention. Copies of an admitted read lease share
    /// its ticket. Closing an admission leaves outstanding reads valid.
    class Retention {
    public:
        Retention(Retention&&) noexcept = default;
        Retention& operator=(Retention other) noexcept;
        ~Retention();
        [[nodiscard]] std::expected<ReadLease, ArrayPoolError> try_retain(const ReadLease& source) const;
        [[nodiscard]] std::size_t skipped() const;
        void close() noexcept;

    private:
        friend class ArrayPool;
        explicit Retention(std::shared_ptr<RetentionState> state) noexcept;
        std::shared_ptr<RetentionState> state_;
    };

    ArrayPool(ArrayDescriptor descriptor, std::size_t execution_slots, std::size_t optional_slots,
              std::shared_ptr<ArrayMemoryBudget> budget);
    ~ArrayPool();
    ArrayPool(const ArrayPool&) = delete;
    ArrayPool& operator=(const ArrayPool&) = delete;

    /// Acquisitions never wait for capacity to become free. Execution callers
    /// must respect the simultaneous-slot bound supplied at construction.
    [[nodiscard]] std::expected<WriteLease, ArrayPoolError> try_acquire();
    /// Reserves optional headroom until the admission and its leases are released.
    [[nodiscard]] Retention make_retention(std::size_t max_slots, std::size_t max_bytes);
    /// Stops acquisition and optional retention. Existing leases stay valid.
    void close() noexcept;

private:
    std::shared_ptr<State> state_;
};

} // namespace s7

#endif // S7CORE_INCLUDE_S7_ARRAY_POOL_HPP
