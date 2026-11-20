// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <algorithm>
#include <cmath>
#include <complex>
#include <numbers>
#include <stdexcept>
#include <string>

namespace s7tests::education {
inline double lti_reference(const std::string& lab,const std::string& channel,int n,double rate,bool edited=false,bool nonzero=false) {
    if (lab=="SP05") {
        const double b=edited ? .75 : -.5;
        const auto kernel=[](int k) {return k==0 ? 1. : k==1 ? .5 : k==2 ? -.25 : 0.;};
        const double first=kernel(n-4),second=b*kernel(n-6),sum=first+second;
        const double natural=nonzero && n==0 ? -.25 : 0;
        if (channel=="first") return n==4 ? 1 : 0;
        if (channel=="weight") return n==6 ? b : 0;
        if (channel=="input") return (n==4 ? 1 : 0)+(n==6 ? b : 0);
        if (channel=="first_response") return first;
        if (channel=="second_response") return second;
        if (channel=="sum") return sum;
        if (channel=="output") return sum+natural;
        if (channel=="residual") return natural;
        throw std::runtime_error("Unknown convolution channel");
    }
    if (lab!="SP07") throw std::runtime_error("Unknown LTI lab");
    const double h=1/rate,amplitude=rate/64,b=edited ? 3.2 : 1.6;
    const int k=n-int(std::llround(.125*rate));
    if (channel=="input") return k==0 ? amplitude : 0;
    if (k<1) return 0;
    if (channel=="low") return amplitude*2*h*std::pow(1-2*h,k-1);
    const double w=std::sqrt(16-b*b/4);
    const auto power=std::pow(std::complex<double>(1-b*h/2,h*w),k-1);
    if (channel=="resonant") return amplitude*16*h/w*power.imag();
    if (channel=="velocity") return amplitude*16*h*(power.real()-b/(2*w)*power.imag());
    throw std::runtime_error("Unknown filter channel");
}
inline std::complex<double> lti_transfer(const std::string& channel,double f,double rate,bool edited=false) {
    const double h=1/rate,b=edited ? 3.2 : 1.6;
    const auto z=std::polar(1.,2*std::numbers::pi*f*h);
    if (channel=="low") return 2*h/(z-(1-2*h));
    if (channel=="resonant") return 16*h*h/((z-1.)*(z-1.+b*h)+16*h*h);
    throw std::runtime_error("Unknown transfer channel");
}
inline double lti_phase(const std::string& channel,double f,double rate,bool edited=false) {
    const double h=1/rate,b=edited ? 3.2 : 1.6;
    // The plotted log-frequency coordinate can round back just above Nyquist.
    const double angle=2*std::numbers::pi*std::clamp(f,0.,rate/2)*h;
    if (channel=="low") return -std::atan2(std::sin(angle),std::cos(angle)-(1-2*h))*180/std::numbers::pi;
    const double alpha=1-b*h/2,beta=h*std::sqrt(16-b*b/4);
    const double x=std::cos(angle)-alpha,y=std::sin(angle)-beta;
    double upper=std::atan2(y,x);
    if (y<0 && x<0) upper+=2*std::numbers::pi;
    return -(upper+std::atan2(std::sin(angle)+beta,x))*180/std::numbers::pi;
}
} // namespace s7tests::education
