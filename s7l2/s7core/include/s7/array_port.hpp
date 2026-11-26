// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: LGPL-3.0-or-later

#ifndef S7CORE_INCLUDE_S7_ARRAY_PORT_HPP
#define S7CORE_INCLUDE_S7_ARRAY_PORT_HPP

#include "s7/array_pool.hpp"
#include "s7/element.hpp"

namespace s7 {

/// Stable storage for one immutable array lease on the graph evaluation thread.
/// A view borrows the current lease and expires when the slot is replaced.
class ArrayOutputSlot {
public:
    explicit ArrayOutputSlot(ArrayDescriptor descriptor);
    ArrayOutputSlot(const ArrayOutputSlot&) = delete;
    ArrayOutputSlot& operator=(const ArrayOutputSlot&) = delete;

    [[nodiscard]] const ArrayDescriptor& descriptor() const & noexcept { return descriptor_; }
    const ArrayDescriptor& descriptor() const && = delete;
    [[nodiscard]] DenseCpuArrayView view() const &;
    DenseCpuArrayView view() const && = delete;

    /// An empty lease clears the slot. On shape mismatch both leases are unchanged.
    [[nodiscard]] ArrayPool::ReadLease exchange(ArrayPool::ReadLease&& next);
    /// The caller selects which values are eligible for asynchronous retention.
    [[nodiscard]] std::expected<ArrayPool::ReadLease, ArrayPoolError> try_retain(
        const ArrayPool::Retention& retention) const;

private:
    friend class ArrayInputBinding;
    const ArrayDescriptor descriptor_;
    ArrayPool::ReadLease current_;
};

/// A synchronous input bound to a stable producer slot. The producer must
/// outlive the binding, and replacement and reads run on the graph thread.
class ArrayInputBinding {
public:
    explicit ArrayInputBinding(ArrayDescriptor descriptor);
    [[nodiscard]] const ArrayDescriptor& descriptor() const & noexcept { return descriptor_; }
    const ArrayDescriptor& descriptor() const && = delete;
    void bind(const ArrayOutputSlot& source);
    void bind(const ArrayOutputSlot&&) = delete;
    [[nodiscard]] DenseCpuArrayView view() const &;
    DenseCpuArrayView view() const && = delete;

    template <SignalType Type>
    [[nodiscard]] std::span<const signal_storage_t<Type>> values() const & {
        return source().current_.values<Type>();
    }
    template <SignalType Type>
    std::span<const signal_storage_t<Type>> values() const && = delete;

private:
    [[nodiscard]] const ArrayOutputSlot& source() const;
    const ArrayDescriptor descriptor_;
    const ArrayOutputSlot* source_{};
};

/// Base for elements with array ports. Scalar ports keep the ordinary
/// Element binding path. Array slots and descriptors are owned by the element.
class ArrayElement : public Element {
public:
    ArrayElement(const ArrayElement&) = delete;
    ArrayElement& operator=(const ArrayElement&) = delete;

    void bind_array_input(std::size_t index, const ArrayOutputSlot& source);
    void bind_array_input(std::size_t, const ArrayOutputSlot&&) = delete;
    [[nodiscard]] const ArrayOutputSlot& array_output(std::size_t index) const &;
    const ArrayOutputSlot& array_output(std::size_t) const && = delete;

protected:
    using Element::Element;
    void register_array_input(std::string_view name, ArrayDescriptor descriptor);
    ArrayOutputSlot& register_array_output(std::string_view name, ArrayDescriptor descriptor);
    [[nodiscard]] const ArrayInputBinding& array_input(std::size_t index) const &;
    const ArrayInputBinding& array_input(std::size_t) const && = delete;

private:
    std::vector<std::unique_ptr<ArrayInputBinding>> array_inputs_;
    std::vector<std::unique_ptr<ArrayOutputSlot>> array_outputs_;
};

} // namespace s7

#endif // S7CORE_INCLUDE_S7_ARRAY_PORT_HPP
