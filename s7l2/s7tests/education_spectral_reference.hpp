// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "education_signals_reference.hpp"
#include <algorithm>
#include <array>
#include <numeric>

namespace s7tests::education {
inline double spectral_signal(const std::string& lab,const std::string& channel,int n,bool edited=false) {
    const long double t=static_cast<long double>(n)/128,pi=std::numbers::pi_v<long double>;
    const auto cosine=[&](long double a,long double f) {return a*std::cos(2*pi*f*t);};
    if (lab=="SP08") {
        if (channel=="dc") return .25;
        if (channel=="coherent") return cosine(1,16);
        const auto test=cosine(1,edited ? 18 : 16.5L);
        if (channel=="test") return test;
        if (channel=="offset") return test+.25;
    } else if (lab=="SP09") {
        const auto strong=cosine(2,20.5L),weak=cosine(edited ? .1L : .05L,24);
        if (channel=="calibration") return cosine(2,20);
        if (channel=="strong") return strong;
        if (channel=="weak") return weak;
        if (channel=="mixture") return strong+weak;
    }
    throw std::runtime_error("Unknown spectral lesson/channel");
}
inline std::vector<double> lesson_window(int window,std::size_t n) {
    if (window<0 || window>2) throw std::runtime_error("Unknown lesson window");
    std::vector<double> result(n,1);
    if (window) for (std::size_t i=0;i<n;++i) {
        const auto s=std::sin(std::numbers::pi_v<long double>*i/n),squared=s*s;
        result[i]=window==1 ? squared : .08L+.92L*squared;
    }
    return result;
}
struct LessonSpectrum {
    double mean=0,cg=0,energy=0,enbw=0,weightedPower=0;
    std::vector<std::complex<double>> coefficients; // DFT/N
    std::array<std::vector<double>,3> quantity; // peak amplitude, power/bin, PSD
};
inline LessonSpectrum lesson_spectrum(std::span<const double> input,int window,double rate,bool removeMean=false) {
    const auto n=input.size();const auto weights=lesson_window(window,n);
    LessonSpectrum result;
    long double sum=0,squares=0,mean=0,power=0;
    for (double x:input) mean+=static_cast<long double>(x)/n;
    result.mean=mean;std::vector<double> weighted(n);
    for (std::size_t i=0;i<n;++i) {
        const auto w=static_cast<long double>(weights[i]);sum+=w;squares+=w*w;
        const auto x=static_cast<long double>(input[i])-(removeMean ? mean : 0);
        weighted[i]=w*x;power+=w*w*x*x;
    }
    result.cg=sum/n;result.energy=squares/n;result.enbw=rate*squares/(sum*sum);
    result.weightedPower=power/squares;
    result.coefficients=signal_dft(weighted);
    for (std::size_t k=0;k<=n/2;++k) {
        const double fold=k==0 || (n%2==0 && k==n/2) ? 1 : 2;
        result.quantity[0].push_back(fold*std::abs(result.coefficients[k])/result.cg);
        const auto binPower=fold*std::norm(result.coefficients[k])/result.energy;
        result.quantity[1].push_back(binPower);result.quantity[2].push_back(binPower/(rate/n));
    }
    return result;
}
// Closed finite geometric sum, independent of the direct DFT.
inline std::complex<long double> geometric_sum(long double angle,std::size_t n) {
    const auto pi=std::numbers::pi_v<long double>;
    angle=std::remainder(angle,2*pi);
    if (std::abs(angle)<1e-17L) return {static_cast<long double>(n),0};
    return (std::sin(n*angle/2)/std::sin(angle/2))*std::polar(1.L,angle*(n-1)/2);
}
inline std::complex<double> cosine_coefficient(double frequency,std::size_t bin,std::size_t n,std::size_t first=0) {
    const auto theta=2*std::numbers::pi_v<long double>*frequency/128;
    const auto omega=2*std::numbers::pi_v<long double>*bin/n;
    const auto value=(std::polar(1.L,theta*first)*geometric_sum(theta-omega,n)+
                      std::polar(1.L,-theta*first)*geometric_sum(-theta-omega,n))/(2.L*n);
    return std::complex<double>(value);
}
} // namespace s7tests::education
