// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: MIT-0
#pragma once
#include "analysis.hpp"
#include <s7/sources.hpp>
namespace s7spectral {
class FourierCoefficient final : public s7::Element {
  public:
    FourierCoefficient(double frequency = 10, double window_time = 1, double hop_time = .5)
        : Element("FourierCoefficient", true), frequency_(frequency), windowTime_(window_time),
          hopTime_(hop_time) {
        if (!std::isfinite(frequency) || frequency < 0 || !std::isfinite(window_time) || window_time <= 0 ||
            !std::isfinite(hop_time) || hop_time <= 0)
            throw std::invalid_argument(
                "FourierCoefficient frequency/durations must be finite and nonnegative/positive");
        register_input(0, "in", s7::SignalType::F64, nullptr);
        register_output(0, "coefficient", s7::SignalType::C64, &value_);
        register_output(1, "updated", s7::SignalType::F64, &updated_);
    }
    void adopt_rate(const s7::Rate &rate) override {
        rate_ = rate.to_hz();
        const auto n = s7::detail::duration_samples(rate, windowTime_, "window_time");
        const auto hop = s7::detail::duration_samples(rate, hopTime_, "hop_time");
        if (n < 2 || n > 65536 || hop < 1 || hop > n || frequency_ > rate_ / 2)
            throw std::invalid_argument("Invalid Fourier window/hop/frequency for adopted model rate");
        ring_.assign(std::size_t(n), 0);
        work_.resize(std::size_t(n));
        hop_ = std::size_t(hop);
        count_ = 0;
        value_ = {};
        updated_ = 0;
    }
    void step() override {
        if (ring_.empty())
            throw std::logic_error("FourierCoefficient has not adopted a rate");
        const auto x = read_input<double>(0);
        if (!std::isfinite(x))
            throw std::invalid_argument("Fourier input must be finite");
        ring_[count_ % ring_.size()] = x;
        ++count_;
        updated_ = 0;
        if (count_ >= ring_.size() && (count_ - ring_.size()) % hop_ == 0) {
            for (std::size_t i = 0; i < ring_.size(); ++i)
                work_[i] = ring_[(count_ + i) % ring_.size()];
            value_ = fourierCoefficient(work_, rate_, frequency_);
            updated_ = 1;
        }
    }

  private:
    double frequency_, windowTime_, hopTime_, rate_ = 0, updated_ = 0;
    std::vector<double> ring_, work_;
    std::size_t count_ = 0, hop_ = 0;
    Complex value_{};
};
} // namespace s7spectral
