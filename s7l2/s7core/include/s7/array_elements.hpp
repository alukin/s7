// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: LGPL-3.0-or-later

#ifndef S7CORE_INCLUDE_S7_ARRAY_ELEMENTS_HPP
#define S7CORE_INCLUDE_S7_ARRAY_ELEMENTS_HPP

#include "s7/array_port.hpp"

#include <algorithm>
#include <array>
#include <string>
#include <utility>
#include <vector>

namespace s7 {

namespace detail {
[[nodiscard]] ArrayDescriptor vector_adapter_shape(SignalType type, std::int64_t components);
} // namespace detail

/// One domain evaluation of causal memory for a whole array. The initializer
/// must be a sealed execution lease matching the port descriptor.
class VectorDelay final : public ArrayElement {
public:
    VectorDelay(ArrayDescriptor descriptor, ArrayPool::ReadLease initial);
    void step() override;

private:
    ArrayOutputSlot& output_;
};

class VectorPassThrough final : public ArrayElement {
public:
    explicit VectorPassThrough(ArrayDescriptor descriptor);
    void step() override;

private:
    ArrayOutputSlot& output_;
};

template <SignalType Type>
class VectorConstantT final : public ArrayElement {
public:
    VectorConstantT(std::span<const signal_storage_t<Type>> values, std::size_t optional_slots,
                    std::shared_ptr<ArrayMemoryBudget> budget)
        : ArrayElement("VectorConstant"), descriptor_(Type, std::array{static_cast<std::int64_t>(values.size())}),
          pool_(descriptor_, 1, optional_slots, std::move(budget)),
          output_(register_array_output(port_names::out, descriptor_))
    {
        auto writer = pool_.try_acquire().value();
        std::ranges::copy(values, writer.template values<Type>().begin());
        value_ = std::move(writer).seal();
        register_output(1, "components", SignalType::U64, &components_);
        components_ = values.size();
    }

    void step() override { static_cast<void>(output_.exchange({value_})); }

private:
    ArrayDescriptor descriptor_;
    ArrayPool pool_;
    ArrayOutputSlot& output_;
    ArrayPool::ReadLease value_;
    std::uint64_t components_{};
};

template <SignalType Type>
class VectorGainT final : public ArrayElement {
    static_assert(signal_type_info(Type).kind == SignalKind::Float ||
                  signal_type_info(Type).kind == SignalKind::ComplexFloat);
public:
    VectorGainT(ArrayDescriptor descriptor, signal_storage_t<Type> gain, std::size_t execution_slots,
                std::shared_ptr<ArrayMemoryBudget> budget)
        : ArrayElement("VectorGain"), pool_(descriptor, execution_slots, 0, std::move(budget)),
          output_(register_array_output(port_names::out, descriptor)), gain_(gain)
    {
        if (descriptor.rank() != 1 || descriptor.type() != Type || execution_slots < 2) {
            throw std::invalid_argument("VectorGain requires an exact rank-one descriptor and at least two execution slots.");
        }
        register_array_input(port_names::in, std::move(descriptor));
    }

    void step() override
    {
        auto writer = pool_.try_acquire();
        if (!writer) throw std::runtime_error("VectorGain execution pool cannot supply an output.");
        auto output = writer->template values<Type>();
        const auto input = array_input(0).template values<Type>();
        std::ranges::transform(input, output.begin(), [this](const auto& sample) { return sample * gain_; });
        static_cast<void>(output_.exchange({std::move(*writer).seal(), array_input(0).origin()}));
    }

private:
    ArrayPool pool_;
    ArrayOutputSlot& output_;
    signal_storage_t<Type> gain_;
};

/// Assemble one vector from scalar inputs in port order.
template <SignalType Type>
class PackVectorT final : public ArrayElement {
public:
    PackVectorT(std::int64_t components, std::size_t execution_slots, std::size_t optional_slots,
                std::shared_ptr<ArrayMemoryBudget> budget)
        : ArrayElement("PackVector"), descriptor_(detail::vector_adapter_shape(Type, components)),
          pool_(descriptor_, checked_reserve(execution_slots), optional_slots, std::move(budget)),
          output_(register_array_output(port_names::out, descriptor_))
    {
        for (std::size_t index = 0; index < descriptor_.element_count(); ++index) {
            register_input(index, "in" + std::to_string(index), Type, nullptr);
        }
    }

    void step() override
    {
        auto writer = pool_.try_acquire();
        if (!writer) throw std::runtime_error("PackVector execution pool cannot supply an output.");
        auto values = writer->template values<Type>();
        for (std::size_t index = 0; index < values.size(); ++index) {
            values[index] = read_input<signal_storage_t<Type>>(index);
        }
        static_cast<void>(output_.exchange({std::move(*writer).seal()}));
    }

    [[nodiscard]] ArrayPool::Retention make_retention(std::size_t max_slots, std::size_t max_bytes)
    {
        return pool_.make_retention(max_slots, max_bytes);
    }

private:
    static std::size_t checked_reserve(std::size_t slots)
    {
        if (slots < 2) throw std::invalid_argument("PackVector requires at least two execution slots.");
        return slots;
    }

    ArrayDescriptor descriptor_;
    ArrayPool pool_;
    ArrayOutputSlot& output_;
};

/// Expose one scalar output per vector component in port order.
template <SignalType Type>
class UnpackVectorT final : public ArrayElement {
public:
    explicit UnpackVectorT(std::int64_t components) : ArrayElement("UnpackVector")
    {
        auto descriptor = detail::vector_adapter_shape(Type, components);
        outputs_.resize(descriptor.element_count());
        register_array_input(port_names::in, std::move(descriptor));
        for (std::size_t index = 0; index < outputs_.size(); ++index) {
            register_output(index, "out" + std::to_string(index), Type, &outputs_[index]);
        }
    }

    void step() override
    {
        const auto values = array_input(0).template values<Type>();
        std::ranges::copy(values, outputs_.begin());
    }

private:
    std::vector<signal_storage_t<Type>> outputs_;
};

} // namespace s7

#endif // S7CORE_INCLUDE_S7_ARRAY_ELEMENTS_HPP
