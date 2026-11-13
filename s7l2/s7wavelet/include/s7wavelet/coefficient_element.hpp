// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: MIT-0
#pragma once
#include "analysis.hpp"
#include <s7/element.hpp>
namespace s7wavelet {
class WaveletCoefficient final : public s7::Element {
  public:
    explicit WaveletCoefficient(double frequency = 25, double cycles = 6)
        : Element("WaveletCoefficient", true), frequency_(frequency), cycles_(cycles) {
        // Full rate-dependent validation happens on adoption, before execution.
        if (!std::isfinite(frequency) || frequency <= 0 || !std::isfinite(cycles) || cycles < 5 ||
            cycles > 16)
            throw std::invalid_argument("Invalid wavelet frequency/cycles");
        register_input(0, "in", s7::SignalType::F64, nullptr);
        register_output(0, "coefficient", s7::SignalType::C64, &value_);
        register_output(1, "valid", s7::SignalType::F64, &valid_);
        register_output(2, "delay_samples", s7::SignalType::F64, &delay_);
    }
    void adopt_rate(const s7::Rate &rate) override {
        kernel_ = morlet(rate.to_hz(), frequency_, cycles_);
        history_.assign(kernel_.taps.size(), 0);
        count_ = 0;
        value_ = {};
        valid_ = 0;
        delay_ = double(kernel_.halfSupport);
    }
    void step() override {
        if (history_.empty())
            throw std::logic_error("WaveletCoefficient has not adopted a rate");
        const double x = read_input<double>(0);
        if (!std::isfinite(x))
            throw std::invalid_argument("Wavelet input must be finite");
        history_[count_ % history_.size()] = x;
        ++count_;
        value_ = {};
        for (std::size_t j = 0; j < kernel_.taps.size(); ++j)
            value_ += kernel_.taps[j] * history_[(count_ - 1 + history_.size() - j) % history_.size()];
        if (!s7spectral::finite(value_))
            throw std::invalid_argument("Wavelet coefficient overflow");
        valid_ = count_ >= history_.size() ? 1 : 0;
    }

  private:
    double frequency_, cycles_, valid_ = 0, delay_ = 0;
    Kernel kernel_;
    std::vector<double> history_;
    std::size_t count_ = 0;
    s7spectral::Complex value_{};
};
} // namespace s7wavelet
