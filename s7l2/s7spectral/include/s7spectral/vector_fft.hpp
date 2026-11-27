// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: MIT-0
#pragma once

#include "s7spectral/analysis.hpp"

#include <s7/array_port.hpp>

#include <array>
#include <limits>

namespace s7spectral {

class VectorFFT final : public s7::ArrayElement {
public:
    VectorFFT(std::size_t length, bool inverse, std::size_t execution_slots)
        : ArrayElement("VectorFFT"), transform_(length), inverse_(inverse),
          descriptor_(s7::SignalType::C32, std::array{static_cast<std::int64_t>(length)}),
          pool_(descriptor_, execution_slots, 0, reserve(length, execution_slots)),
          output_(register_array_output(s7::port_names::out, descriptor_))
    {
        register_array_input(s7::port_names::in, output_.descriptor());
    }

    void step() override
    {
        const auto& input = array_input(0);
        auto writer = pool_.try_acquire();
        if (!writer) throw std::runtime_error("VectorFFT execution pool cannot supply an output.");
        auto output = writer->values<s7::SignalType::C32>();
        std::ranges::copy(input.values<s7::SignalType::C32>(), output.begin());
        transform_.apply(output, inverse_);
        static_cast<void>(output_.exchange({std::move(*writer).seal(), input.origin(), input.sample_window()}));
    }

private:
    static std::shared_ptr<s7::ArrayMemoryBudget> reserve(std::size_t length, std::size_t slots)
    {
        constexpr auto sample_bytes = sizeof(s7::signal_storage_t<s7::SignalType::C32>);
        if (slots < 2 || slots > std::numeric_limits<std::size_t>::max() / (length * sample_bytes)) {
            throw std::invalid_argument("VectorFFT requires a valid reservation for at least two execution slots.");
        }
        return std::make_shared<s7::ArrayMemoryBudget>(sample_bytes * length * slots);
    }

    Radix2Transform<float> transform_;
    bool inverse_;
    s7::ArrayDescriptor descriptor_;
    s7::ArrayPool pool_;
    s7::ArrayOutputSlot& output_;
};

} // namespace s7spectral
