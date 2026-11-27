// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: MIT-0
#pragma once

#include "s7spectral/analysis.hpp"

#include <s7/array_port.hpp>

#include <array>
#include <limits>

namespace s7spectral {

class VectorPSD final : public s7::ArrayElement {
public:
    VectorPSD(std::size_t length, s7::SpectrumUnits units, std::optional<s7::Rate> configured_rate,
              std::size_t execution_slots)
        : ArrayElement("VectorPSD"), transform_(length), units_(units),
          configured_rate_(configured_rate ? std::optional{s7::Rate::reduced(configured_rate->numerator,
                                                                              configured_rate->denominator)}
                                           : std::nullopt),
          descriptor_(s7::SignalType::F32, std::array{static_cast<std::int64_t>(length)}),
          pool_(descriptor_, execution_slots, 1, reserve(length, execution_slots)),
          output_(register_array_output(s7::port_names::out, descriptor_)),
          window_(length), scratch_(length)
    {
        register_array_input(s7::port_names::in,
                             s7::ArrayDescriptor(s7::SignalType::C32, std::array{static_cast<std::int64_t>(length)}));
        for (std::size_t index = 0; index < length; ++index) {
            const auto angle = 2.0 * std::numbers::pi * static_cast<double>(index) / static_cast<double>(length);
            window_[index] = static_cast<float>(0.5 - 0.5 * s7::math::cos(angle));
            const double weight = window_[index];
            energy_ += weight * weight;
        }
    }

    [[nodiscard]] s7::ArrayPool::Retention make_retention()
    {
        return pool_.make_retention(1, descriptor_.byte_size());
    }

    void step() override
    {
        const auto& input = array_input(0);
        const auto samples = input.values<s7::SignalType::C32>();
        const auto provenance = input.sample_window();
        auto physical_rate = configured_rate_;
        if (provenance) {
            if (provenance->missing_samples ||
                (provenance->valid_sample_count && *provenance->valid_sample_count != samples.size())) {
                throw std::invalid_argument("VectorPSD requires a complete sample block.");
            }
            if (provenance->sample_rate) {
                const auto rate = s7::Rate::reduced(provenance->sample_rate->numerator,
                                                  provenance->sample_rate->denominator);
                if (physical_rate && *physical_rate != rate) {
                    throw std::invalid_argument("VectorPSD configured rate differs from the source rate.");
                }
                physical_rate = rate;
            }
        }
        if (units_ == s7::SpectrumUnits::PowerPerHz && !physical_rate) {
            throw std::invalid_argument("VectorPSD per-Hz units require a physical sample rate.");
        }
        for (std::size_t index = 0; index < samples.size(); ++index) {
            if (!finite(Complex(samples[index]))) {
                throw std::invalid_argument("VectorPSD requires finite input samples.");
            }
            scratch_[index] = samples[index] * window_[index];
        }
        transform_.apply(scratch_);
        const auto scale = energy_ * (units_ == s7::SpectrumUnits::PowerPerHz
            ? physical_rate->to_hz() : static_cast<double>(samples.size()));
        auto writer = pool_.try_acquire();
        if (!writer) throw std::runtime_error("VectorPSD execution pool cannot supply an output.");
        auto output = writer->values<s7::SignalType::F32>();
        for (std::size_t bin = 0; bin < output.size(); ++bin) {
            const double real = scratch_[bin].real();
            const double imaginary = scratch_[bin].imag();
            const auto power = (real * real + imaginary * imaginary) / scale;
            if (!(power <= std::numeric_limits<float>::max())) {
                throw std::overflow_error("VectorPSD power exceeds the F32 range.");
            }
            output[bin] = static_cast<float>(power);
        }
        static_cast<void>(output_.exchange({std::move(*writer).seal(), input.origin(), provenance,
                                            s7::ArraySpectrum{units_, s7::SpectrumWindow::PeriodicHann, physical_rate}}));
    }

private:
    static std::shared_ptr<s7::ArrayMemoryBudget> reserve(std::size_t length, std::size_t slots)
    {
        if (slots < 2 || slots >= std::numeric_limits<std::size_t>::max() / (length * sizeof(float))) {
            throw std::invalid_argument("VectorPSD requires a valid reservation for at least two execution slots.");
        }
        return std::make_shared<s7::ArrayMemoryBudget>(length * sizeof(float) * (slots + 1));
    }

    Radix2Transform<float> transform_;
    s7::SpectrumUnits units_;
    std::optional<s7::Rate> configured_rate_;
    s7::ArrayDescriptor descriptor_;
    s7::ArrayPool pool_;
    s7::ArrayOutputSlot& output_;
    std::vector<float> window_;
    std::vector<std::complex<float>> scratch_;
    double energy_{};
};

} // namespace s7spectral
