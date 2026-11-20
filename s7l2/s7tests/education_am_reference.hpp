// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "education_control_reference.hpp"
#include <complex>
#include <numbers>
#include <span>
namespace s7tests::education {
inline std::complex<double> am_filter(double frequency,double rate=256) {
    const long double beta=50.L/rate,a=1-beta;
    const auto z=std::polar(1.L,2*std::numbers::pi_v<long double>*frequency/rate);
    return std::complex<double>(beta*beta/((z-a)*(z-a)));
}
inline ControlTrace am_reference(double phase=0,double rate=256) {
    const int count=int(4*rate);const long double beta=50.L/rate,a=1-beta,pi=std::numbers::pi_v<long double>;
    std::vector<long double> mixed(std::size_t(count),0),first(std::size_t(count),0),second(std::size_t(count),0);
    for(int k=1;k<count;++k) first[std::size_t(k)]=beta*std::pow(a,k-1);
    for(int k=2;k<count;++k) second[std::size_t(k)]=beta*beta*(k-1)*std::pow(a,k-2);
    ControlTrace trace;
    for(int n=0;n<count;++n) {
        const long double message=.6L*std::cos(2*pi*4*n/rate),envelope=1+message,carrier=std::sin(2*pi*32*n/rate),local=2*std::sin(2*pi*32*n/rate+phase);
        const auto rf=envelope*carrier;mixed[std::size_t(n)]=rf*local;long double low=0,recovered=0;
        for(int k=1;k<=n;++k) {low+=first[std::size_t(k)]*mixed[std::size_t(n-k)];recovered+=second[std::size_t(k)]*mixed[std::size_t(n-k)];}
        ControlSample sample;const auto put=[&](const std::string& id,long double value) {sample[control_key(id)]=double(value);};
        put("message",message);put("bias",1);put("envelope",envelope);put("carrier",carrier);put("local",local);put("transmit",rf);put("mixed",mixed[std::size_t(n)]);put("tau",.02L);put("low1",low);put("recovered",recovered);trace.push_back(std::move(sample));
    }return trace;
}
inline std::complex<double> am_bin(std::span<const double> values,std::size_t bin) {
    std::complex<long double> sum{};const auto size=values.size();
    for(std::size_t n=0;n<size;++n) sum+=static_cast<long double>(values[n])*std::polar(1.L,-2*std::numbers::pi_v<long double>*bin*n/size);
    return std::complex<double>(sum/static_cast<long double>(size));
}
inline std::complex<double> am_steady(const std::string& channel,double frequency,double phase=0,double rate=256) {
    if(channel=="transmit") return frequency==32 ? std::complex<double>(0,-1):(frequency==28 || frequency==36) ? std::complex<double>(0,-.3):std::complex<double>{};
    if(channel=="envelope") return frequency==0 ? 1.:frequency==4 ? .6:0.;
    const auto image=-std::polar(1.,phase);
    std::complex<double> coefficient=frequency==0 ? std::cos(phase):frequency==4 ? .6*std::cos(phase):frequency==64 ? image:(frequency==60 || frequency==68) ? .3*image:std::complex<double>{};
    return channel=="recovered" ? coefficient*am_filter(frequency,rate):coefficient;
}
} // namespace s7tests::education
