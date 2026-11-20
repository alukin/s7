// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cmath>
#include <complex>
#include <numbers>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace s7tests::education {
inline double signal_reference(const std::string& lab, const std::string& channel, int n, double rate, bool edited=false) {
    constexpr long double pi=std::numbers::pi_v<long double>;
    const long double t=static_cast<long double>(n)/rate;
    if (lab=="SP01") {
        if (channel=="target") return std::fmod(2*t,1.L)<.5L ? 1 : -1;
        int count=channel=="weight1" ? 1 : channel=="three" ? 3 : channel=="five" ? 5 : 0;
        if (!count) throw std::runtime_error("Unknown synthesis channel");
        long double sum=0;
        for (int m=1;m<2*count;m+=2)
            if (!(edited && m==9)) sum+=4/(pi*m)*std::sin(2*pi*m*2*t);
        return static_cast<double>(sum);
    }
    if (lab!="SP03") throw std::runtime_error("Unknown signal lab");
    const auto low=std::sin(2*pi*10*t+.375L);
    const auto high=std::sin(2*pi*(edited ? 70 : 74)*t+.375L);
    if (channel=="low") return low;
    if (channel=="shifted") return high;
    if (channel=="reflected") return std::sin(2*pi*54*t+pi-.375L);
    if (channel=="residual") return high-low;
    throw std::runtime_error("Unknown alias channel");
}
// Normalized complex DFT, independent of the production FFT/analysis code.
inline std::vector<std::complex<double>> signal_dft(std::span<const double> samples) {
    std::vector<std::complex<double>> out(samples.size());
    for (std::size_t k=0;k<samples.size();++k) {
        std::complex<long double> sum{};
        for (std::size_t n=0;n<samples.size();++n) {
            const auto angle=-2*std::numbers::pi_v<long double>*k*n/samples.size();
            sum+=static_cast<long double>(samples[n])*std::complex<long double>(std::cos(angle),std::sin(angle));
        }
        out[k]=std::complex<double>(sum/static_cast<long double>(samples.size()));
    }
    return out;
}
} // namespace s7tests::education
