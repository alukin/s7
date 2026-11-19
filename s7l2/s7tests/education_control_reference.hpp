// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <algorithm>
#include <cmath>
#include <map>
#include <string>
#include <vector>

namespace s7tests::education {
using ControlSample=std::map<std::string,double>;
using ControlTrace=std::vector<ControlSample>;
inline std::string control_key(const std::string& element,std::size_t port=0) {
    return element+":"+std::to_string(port);
}
// Independent scalar equations. No S7 controller, integrator or graph helper.
// State at index n is the old state; all updates use that same state.
inline ControlTrace control_reference(bool windup,bool edited=false,double rate=100) {
    struct State {long double y=0,integral=0,filter=0;};
    std::map<std::string,State> states;
    const std::vector<std::string> modes=windup ? std::vector<std::string>{"aw","free"} : std::vector<std::string>{"p","pi","pid"};
    const long double h=1.L/rate,kp=windup ? 1.5L:2.L,ki=windup ? 2.L:1.5L,kd=edited ? 0.L:.15L;
    ControlTrace trace;
    for(int n=0;n<int((windup ? 12:16)*rate);++n) {
        const long double ref=n<std::llround(rate) ? 0.L:windup ? (n<std::llround(4*rate) ? 1.5L:.4L):.5L;
        const long double load=!windup && n>=std::llround(8*rate) ? .2L:0.L;
        ControlSample sample;
        const auto put=[&](const std::string& id,long double value,std::size_t port=0) {sample[control_key(id,port)]=double(value);};
        put("reference_step",n<std::llround(rate) ? 0.L:windup ? 1.5L:.5L);
        put("reference",ref);put("release",n<std::llround(4*rate) ? 0.L:-1.1L);
        put("load",load);put("kp",kp);put("ki",ki);put("kd",kd);put("filter_tau",.1L);
        for(const auto& mode:modes) {
            auto& s=states[mode];const auto error=ref-s.y;
            const bool p=mode=="p",pid=mode=="pid";
            const long double derivative=pid ? (s.y-s.filter)/.1L:0.L;
            const long double raw=kp*error+(p ? 0.L:s.integral)-(pid ? kd*derivative:0.L);
            const auto applied=std::clamp(raw,-1.L,1.L);
            put(mode+"_y",s.y);put(mode+"_error",error);
            put(mode,p ? raw:applied);put(mode,raw,1);put(mode,s.integral,2);
            if(pid) {put(mode,s.filter,3);put(mode,kd*derivative,4);}
            if(p) put("p_limit",applied);
            const auto plantRate=(applied-s.y-load)/(windup ? .8L:1.L);put(mode+"_rate",plantRate);
            const long double tracking=mode=="aw" && !edited ? 6.L:0.L;
            s.y+=h*plantRate;
            if(!p) s.integral+=h*(ki*error+tracking*(applied-raw));
            if(pid) s.filter+=h*derivative;
        }
        trace.push_back(std::move(sample));
    }
    return trace;
}
} // namespace s7tests::education
