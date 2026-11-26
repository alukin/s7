// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "s7/array_port.hpp"

#include <utility>

namespace s7 {

ArrayOutputSlot::ArrayOutputSlot(ArrayDescriptor descriptor) : descriptor_(std::move(descriptor))
{
    if (descriptor_.rank() == 0) throw std::invalid_argument("An array output requires a positive rank.");
}

const ArrayPool::ReadLease& ArrayOutputSlot::graph_value() const noexcept
{
    return candidate_ ? *candidate_ : published_;
}

DenseCpuArrayView ArrayOutputSlot::view() const & { return graph_value().view(); }

ArrayPool::ReadLease ArrayOutputSlot::exchange(ArrayPool::ReadLease&& next)
{
    if (next.retention_) {
        throw std::invalid_argument("An array output requires an execution lease.");
    }
    if (next && next.view().descriptor() != descriptor_) {
        throw std::invalid_argument("Array lease does not match the output descriptor.");
    }
    if (!runtime_managed_) return std::exchange(published_, std::move(next));

    // Preserve the committed lease for both observation and rollback.
    auto previous = candidate_ ? std::move(*candidate_) : published_;
    candidate_.emplace(std::move(next));
    return previous;
}

void ArrayOutputSlot::commit(const std::uint64_t domain_step) noexcept
{
    if (candidate_) {
        published_ = std::move(*candidate_);
        candidate_.reset();
    }
    committed_step_ = domain_step;
}

std::expected<ArrayPool::ReadLease, ArrayPoolError> ArrayOutputSlot::try_retain(
    const ArrayPool::Retention& retention) const
{
    if (runtime_managed_ && committed_step_ == 0) return std::unexpected(ArrayPoolError::InvalidLease);
    return retention.try_retain(published_);
}

ArrayInputBinding::ArrayInputBinding(ArrayDescriptor descriptor) : descriptor_(std::move(descriptor))
{
    if (descriptor_.rank() == 0) throw std::invalid_argument("An array input requires a positive rank.");
}

void ArrayInputBinding::bind(const ArrayOutputSlot& producer)
{
    if (producer.descriptor() != descriptor_) {
        throw std::invalid_argument("Array output descriptor does not match the input descriptor.");
    }
    source_ = &producer;
}

const ArrayOutputSlot& ArrayInputBinding::source() const
{
    if (!source_) throw std::logic_error("Array input is unbound.");
    return *source_;
}

DenseCpuArrayView ArrayInputBinding::view() const & { return source().view(); }

ArrayPool::ReadLease ArrayInputBinding::share() const
{
    const auto& current = source().graph_value();
    if (!current) throw std::logic_error("Array input has no value.");
    return current;
}

void ArrayElement::register_array_input(const std::string_view name, ArrayDescriptor descriptor)
{
    const auto index = input_count();
    auto binding = std::make_unique<ArrayInputBinding>(std::move(descriptor));
    array_inputs_.resize(index + 1);
    register_input(index, name, binding->descriptor().type(), nullptr, &binding->descriptor());
    array_inputs_[index] = std::move(binding);
}

ArrayOutputSlot& ArrayElement::register_array_output(const std::string_view name, ArrayDescriptor descriptor)
{
    const auto index = output_count();
    auto slot = std::make_unique<ArrayOutputSlot>(std::move(descriptor));
    array_outputs_.resize(index + 1);
    register_output(index, name, slot->descriptor().type(), nullptr, &slot->descriptor());
    array_outputs_[index] = std::move(slot);
    return *array_outputs_[index];
}

void ArrayElement::bind_array_input(const std::size_t index, const ArrayOutputSlot& source)
{
    if (index >= array_inputs_.size() || !array_inputs_[index]) {
        throw std::invalid_argument("Port is not an array input.");
    }
    array_inputs_[index]->bind(source);
}

const ArrayInputBinding& ArrayElement::array_input(const std::size_t index) const &
{
    if (index >= array_inputs_.size() || !array_inputs_[index]) {
        throw std::invalid_argument("Port is not an array input.");
    }
    return *array_inputs_[index];
}

const ArrayOutputSlot& ArrayElement::array_output(const std::size_t index) const &
{
    if (index >= array_outputs_.size() || !array_outputs_[index]) {
        throw std::invalid_argument("Port is not an array output.");
    }
    return *array_outputs_[index];
}

} // namespace s7
