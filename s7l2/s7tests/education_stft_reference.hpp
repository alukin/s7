// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cmath>
#include <complex>
#include <numbers>
#include <span>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace s7tests::education {
inline double stft_signal(std::string_view channel,int n,bool edited=false) {
    constexpr long double pi=std::numbers::pi_v<long double>;
    const long double index=n;
    const double gate=n>=128 && n<(edited ? 192 : 256) ? 1 : 0;
    const double tone=std::cos(2*pi*32*index/128);
    // Sum of the source's left-endpoint frequency increments, not a
    // continuous chirp integral. This freezes the exact discrete source.
    const double chirp=.5L*std::cos(2*pi*(8*index/128+32*index*(index-1)/(2*512*128)));
    if(channel=="gate") return gate;
    if(channel=="burst") return gate*tone;
    if(channel=="chirp") return chirp;
    if(channel=="mixture") return gate*tone+chirp;
    throw std::invalid_argument("Unknown SP11 signal");
}
struct LessonStft {
    std::vector<double> times,frequencies,power;
    // Frequency-major, time-fast, unlike the production processor.
    std::vector<std::complex<double>> coefficients;
};
inline LessonStft stft_reference(std::span<const double> input,std::size_t size,std::size_t hop,
                                double origin=1./128) {
    if(size<2 || !hop || hop>size || input.size()<size) throw std::invalid_argument("Invalid reference STFT");
    LessonStft result;
    const auto columns=1+(input.size()-size)/hop,bins=size/2+1;
    result.coefficients.resize(columns*bins);result.power.resize(columns);
    std::vector<long double> weights(size);long double gain=0,energy=0;
    for(std::size_t i=0;i<size;++i) {
        const auto s=std::sin(std::numbers::pi_v<long double>*i/size);
        weights[i]=s*s;gain+=weights[i];energy+=weights[i]*weights[i];
    }
    for(std::size_t k=0;k<bins;++k) result.frequencies.push_back(k*128./size);
    for(std::size_t t=0;t<columns;++t) {
        const auto start=t*hop;result.times.push_back(origin+(start+(size-1)/2.)/128);
        long double weighted=0;
        for(std::size_t i=0;i<size;++i) weighted+=input[start+i]*input[start+i]*weights[i]*weights[i];
        result.power[t]=double(weighted/energy);
        for(std::size_t k=0;k<bins;++k) {
            std::complex<long double> sum{};
            for(std::size_t i=0;i<size;++i)
                sum+=static_cast<long double>(input[start+i])*weights[i]*
                    std::polar(1.L,-2*std::numbers::pi_v<long double>*k*i/size);
            result.coefficients[k*columns+t]=std::complex<double>(sum*((k==0 || k==size/2) ? 1.L : 2.L)/gain);
        }
    }
    return result;
}
inline double stft_quantity(std::complex<double> coefficient,std::size_t bin,std::size_t size,int quantity) {
    if(quantity==0) return std::abs(coefficient);
    if(quantity==1) return std::norm(coefficient);
    if(quantity==2) return std::arg(coefficient);
    const double fold=bin==0 || bin==size/2 ? 1 : 2;
    const double power=std::norm(coefficient)*2/(3*fold);
    return quantity==4 ? power*size/128 : power;
}
} // namespace s7tests::education
