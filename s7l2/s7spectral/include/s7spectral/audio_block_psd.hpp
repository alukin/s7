// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: MIT-0
#pragma once

#include "s7spectral/analysis.hpp"

#include <s7/array_port.hpp>

#include <array>
#include <limits>

namespace s7spectral {

class AudioBlockPSD final : public s7::ArrayElement {
public:
    AudioBlockPSD(std::size_t channels, std::size_t length)
        : ArrayElement("AudioBlockPSD"), transform_(length), length_(length), channels_(channels),
          descriptor_(shape(channels, length / 2 + 1)),
          pool_(descriptor_, 2, 1, std::make_shared<s7::ArrayMemoryBudget>(3 * descriptor_.byte_size())),
          output_(register_array_output(s7::port_names::out, descriptor_)), window_(length), scratch_(length)
    {
        register_array_input(s7::port_names::in, shape(channels, length));
        for (std::size_t i = 0; i < length_; ++i) {
            const auto angle = 2 * std::numbers::pi * static_cast<double>(i) / static_cast<double>(length_);
            window_[i] = static_cast<float>(0.5 - 0.5 * s7::math::cos(angle));
            energy_ += static_cast<double>(window_[i]) * window_[i];
        }
    }

    void step() override
    {
        const auto& input = array_input(0);
        const auto provenance = input.sample_window();
        if (!provenance || !provenance->sample_rate || provenance->missing_samples ||
            provenance->valid_sample_count != length_) {
            throw std::invalid_argument("AudioBlockPSD requires aligned complete blocks with a sample rate");
        }
        const auto rate = *provenance->sample_rate;
        const auto scale = energy_ * s7::Rate::reduced(rate.numerator, rate.denominator).to_hz();
        auto writer = pool_.try_acquire();
        if (!writer) throw std::runtime_error("AudioBlockPSD execution pool exhausted");

        const auto samples = input.values<s7::SignalType::F32>();
        const auto bins = length_ / 2 + 1;
        auto powers = writer->values<s7::SignalType::F32>();
        for (std::size_t channel = 0; channel < channels_; ++channel) {
            const auto row = samples.subspan(channel * length_, length_);
            for (std::size_t i = 0; i < length_; ++i) {
                if (!std::isfinite(row[i])) throw std::invalid_argument("AudioBlockPSD requires finite samples");
                scratch_[i] = row[i] * window_[i];
            }
            transform_.apply(scratch_);
            for (std::size_t bin = 0; bin < bins; ++bin) {
                const double real = scratch_[bin].real();
                const double imaginary = scratch_[bin].imag();
                const auto fold = bin == 0 || bin + 1 == bins ? 1 : 2;
                const auto power = fold * (real * real + imaginary * imaginary) / scale;
                if (!(power <= std::numeric_limits<float>::max())) throw std::overflow_error("Audio PSD exceeds F32");
                powers[channel * bins + bin] = static_cast<float>(power);
            }
        }
        static_cast<void>(output_.exchange({std::move(*writer).seal(), input.origin(), provenance,
            s7::ArraySpectrum{s7::SpectrumUnits::PowerPerHz, s7::SpectrumWindow::PeriodicHann, rate}}));
    }

private:
    static s7::ArrayDescriptor shape(std::size_t channels, std::size_t width)
    {
        if (channels < 1 || channels > 2) throw std::invalid_argument("AudioBlockPSD requires mono or stereo");
        return s7::ArrayDescriptor(s7::SignalType::F32,
            std::array{static_cast<std::int64_t>(channels), static_cast<std::int64_t>(width)});
    }

    Radix2Transform<float> transform_;
    std::size_t length_;
    std::size_t channels_;
    s7::ArrayDescriptor descriptor_;
    s7::ArrayPool pool_;
    s7::ArrayOutputSlot& output_;
    std::vector<float> window_;
    std::vector<std::complex<float>> scratch_;
    double energy_{};
};

} // namespace s7spectral
