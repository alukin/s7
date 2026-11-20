// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <array>
#include <complex>
#include <numbers>
#include <span>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace s7tests::education {
using ConvolutionSample = std::complex<double>;
inline constexpr std::array<double, 4> convolution_word{1, 2, -1, 1};

// Deposit each pair's product at its sum index. Independent of production's
// output-centred FIR loop and FFT; no conjugation (this is not correlation).
inline std::vector<ConvolutionSample> convolution_sum(std::span<const ConvolutionSample> x,
                                                     std::span<const ConvolutionSample> h,
                                                     std::size_t period = 0) {
    if (x.empty() || h.empty()) throw std::invalid_argument("Nonempty reference operands required");
    std::vector<std::complex<long double>> sum(period ? period : x.size()+h.size()-1);
    for (std::size_t i=0;i<x.size();++i) for (std::size_t j=0;j<h.size();++j)
        sum[period ? (i+j)%period : i+j] += std::complex<long double>(x[i])*std::complex<long double>(h[j]);
    std::vector<ConvolutionSample> result;for (auto z:sum) result.emplace_back(z);return result;
}

inline std::vector<ConvolutionSample> convolution_dft_product(std::span<const ConvolutionSample> x,
                                                            std::span<const ConvolutionSample> h,
                                                            std::size_t period) {
    // Raw forward DFT, inverse / N. This intentionally computes a circular
    // product, including insufficient padding; never calls a production FFT.
    using C=std::complex<long double>;
    std::vector<C> product(period);std::vector<ConvolutionSample> result(period);
    for (std::size_t k=0;k<period;++k) {
        C a{},b{};
        for (std::size_t n=0;n<x.size();++n) a+=C(x[n])*std::polar(1.L,-2*std::numbers::pi_v<long double>*k*n/period);
        for (std::size_t n=0;n<h.size();++n) b+=C(h[n])*std::polar(1.L,-2*std::numbers::pi_v<long double>*k*n/period);
        product[k]=a*b;
    }
    for (std::size_t n=0;n<period;++n) {
        C sum{};for (std::size_t k=0;k<period;++k) sum+=product[k]*std::polar(1.L,2*std::numbers::pi_v<long double>*k*n/period);
        result[n]=ConvolutionSample(sum/static_cast<long double>(period));
    }
    return result;
}

inline double convolution_input(std::string_view channel,int n) {
    if (n<0) return 0;
    if (channel=="linear_input") return n>=8 && n<12 ? convolution_word[std::size_t(n-8)] : 0;
    if (channel=="cyclic4_input") return convolution_word[std::size_t(n%4)];
    if (channel=="padded8_input") return n%8<4 ? convolution_word[std::size_t(n%8)] : 0;
    throw std::invalid_argument("Unknown SP10 input");
}
inline double convolution_signal(std::string_view channel,int n,bool edited=false) {
    if (channel.ends_with("_input")) return convolution_input(channel,n);
    const std::array<double,3> h{1,.5,edited ? .5 : -.25};
    const auto filter=[&](std::string_view input) {
        double result=0;for(int k=0;k<3;++k) result+=h[std::size_t(k)]*convolution_input(input,n-k);return result;
    };
    if (channel=="linear") return filter("linear_input");
    if (channel=="cyclic4") return filter("cyclic4_input");
    if (channel=="padded8") return filter("padded8_input");
    if (channel=="difference") return filter("padded8_input")-filter("linear_input");
    throw std::invalid_argument("Unknown SP10 output");
}
} // namespace s7tests::education
