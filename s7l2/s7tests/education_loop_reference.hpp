// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "education_control_reference.hpp"
#include <complex>
#include <numbers>

namespace s7tests::education {
inline std::pair<double,double> loop_states(int n,double gain,double amplitude=1) {
    if(n<10 || gain==0) return {0,0};
    const long double k=gain,q=std::sqrt(k),steady=amplitude*k/(1+k);
    const auto rotation=std::pow(std::complex<long double>(.9L,.1L*q),n-10);
    return {double(steady*(1-rotation.real()+q*rotation.imag())),double(steady*(1-rotation.real()-rotation.imag()/q))};
}
inline std::complex<double> loop_transfer(double frequency,double gain) {
    const auto z=std::polar(1.L,2*std::numbers::pi_v<long double>*frequency/10);
    const auto d=z-.9L;return std::complex<double>(.01L*gain/(d*d));
}
struct LoopMargins {double gainMargin,phaseMargin,gainCrossover,phaseCrossover;};
inline LoopMargins loop_margins(double gain) {
    if(gain<=1 || gain>=361) throw std::invalid_argument("No interior gain crossover in this lesson");
    const auto theta=std::acos((1.81-.01*gain)/1.8);
    const auto arg=std::atan2(std::sin(theta),std::cos(theta)-.9);
    return {19/gain,180-2*arg*180/std::numbers::pi,theta*10/(2*std::numbers::pi),std::acos(.9)*10/(2*std::numbers::pi)};
}
inline ControlTrace loop_reference(bool margins,double gain=4) {
    ControlTrace trace;
    for(int n=0;n<(margins ? 640:320);++n) {
        ControlSample sample;const auto put=[&](const std::string& id,double value) {sample[control_key(id)]=value;};
        const auto [x,y]=loop_states(n,gain,margins ? .5:1);
        const double ref=n>=10 ? (margins ? .5:1):0,error=ref-y,command=gain*error;
        const std::string p=margins ? "closed_":"";
        put("gain",gain);put("reference",ref);put("error",error);put("command",command);
        put(p+"x",x);put(p+"y",y);put(p+"x_rate",command-x);put(p+"y_rate",x-y);
        if(margins) {
            const double ox=n>=9 ? double(.1L*gain*std::pow(.9L,n-9)):0;
            const double oy=n>=10 ? double(.01L*gain*(n-9)*std::pow(.9L,n-10)):0;
            const double pulse=n==8 ? 1:0,oc=gain*pulse;
            put("pulse",pulse);put("open_command",oc);put("open_x",ox);put("open_y",oy);
            put("open_x_rate",oc-ox);put("open_y_rate",ox-oy);
        }
        trace.push_back(std::move(sample));
    }return trace;
}
} // namespace s7tests::education
