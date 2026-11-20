// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "education_loop_reference.hpp"
namespace s7tests::education {
inline std::complex<double> compensator_transfer(double frequency,int variant=0) {
    const long double k=variant==2 ? 1:16,a=variant==1 || variant==2 ? .2L:.8L,b=variant==2 ? .8L:.2L;
    const auto z=std::polar(1.L,2*std::numbers::pi_v<long double>*frequency/10);
    return std::complex<double>(k*(z-a)/(z-b)*.01L/((z-.9L)*(z-.9L)));
}
// 0 lead, 1 primary zero cancellation, 2 DC-matched lag, 3 wrong feedback sign.
inline ControlTrace compensator_reference(int variant=0) {
    const long double k=variant==2 ? 1:16,a=variant==1 || variant==2 ? .2L:.8L,b=variant==2 ? .8L:.2L;
    std::vector<long double> c(640),first(640),second(640);c[0]=k;
    for(int j=1;j<640;++j) {c[std::size_t(j)]=k*(b-a)*std::pow(b,j-1);first[std::size_t(j)]=.1L*std::pow(.9L,j-1);}
    for(int j=2;j<640;++j) second[std::size_t(j)]=.01L*(j-1)*std::pow(.9L,j-2);
    long double x=0,y=0,previousError=0,previousControl=0;ControlTrace trace;
    for(int n=0;n<640;++n) {
        ControlSample sample;const auto put=[&](const std::string& id,long double value,std::size_t port=0) {sample[control_key(id,port)]=double(value);};
        const long double ref=n>=10 ? .5L:0,err=ref+(variant==3 ? y:-y),u=b*previousControl+k*(err-a*previousError);
        const auto [bx,by]=loop_states(n,4,.5);const long double base=4*(ref-by);
        put("reference",ref);put("pulse",n==8 ? 1:0);put("base_gain",4);put("scale",k);put("zero",a);put("pole",b);put("tau",1);
        put("base_error",ref-by);put("base",base);put("base_x",bx);put("base_y",by);
        put("lead_error",err);put("lead",u);put("lead",previousError,1);put("lead",previousControl,2);put("lead_x",x);put("lead_y",y);
        long double ox=0,oy=0;const int m=n-8;
        if(m>=0) for(int j=0;j<=m;++j) {ox+=c[std::size_t(j)]*first[std::size_t(m-j)];oy+=c[std::size_t(j)]*second[std::size_t(m-j)];}
        put("open",m>=0 ? c[std::size_t(m)]:0);put("open",n==9 ? 1:0,1);put("open",m>=1 ? c[std::size_t(m-1)]:0,2);
        put("open_x",ox);put("open_y",oy);trace.push_back(std::move(sample));
        const auto nextX=x+.1L*(u-x);y+=.1L*(x-y);x=nextX;previousError=err;previousControl=u;
    }return trace;
}
} // namespace s7tests::education
