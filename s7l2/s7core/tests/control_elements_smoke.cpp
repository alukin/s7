// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: LGPL-3.0-or-later

#include <s7/core.hpp>
#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
void require(bool ok,const char* message) { if(!ok) throw std::runtime_error(message); }
template <typename F> void rejects(F f) {
    bool caught=false;try {f();} catch(const std::exception&) {caught=true;}
    require(caught,"Invalid control configuration/input must be rejected");
}
template <typename E> auto sample(E& element) {
    element.step();return element.output_ports()[0].template as<typename E::V>();
}
template <s7::SignalType T> void primitives() {
    using V=s7::signal_storage_t<T>;
    V a=V(-2),b=V(3),control=V(0);
    const s7::InputBinding switches[]={&a,&b,&control};
    s7::SwitchT<T> sw;sw.bind_inputs(switches);
    require(sample(sw)==a,"Switch zero selects off");control=V(-1);
    require(sample(sw)==b,"Switch nonzero selects on");
    control=std::numeric_limits<V>::quiet_NaN();rejects([&] {sw.step();});
    V value=V(0);const s7::InputBinding binding[]={&value};
    s7::HysteresisRelayT<T> relay(V(-1),V(1),V(-2),V(3),true);relay.bind_inputs(binding);
    require(sample(relay)==V(3),"Hysteresis initial state in band");
    value=V(-1);require(sample(relay)==V(-2),"Hysteresis lower inclusive boundary");
    for(double x : {-0.9,0.9,0.0}) {value=V(x);require(sample(relay)==V(-2),"Relay stays off inside band");}
    value=V(1);require(sample(relay)==V(3),"Hysteresis upper inclusive boundary");
    value=V(0);require(sample(relay)==V(3),"Relay stays on inside band");
    value=std::numeric_limits<V>::infinity();rejects([&] {relay.step();});
    s7::PhaseWrapT<T> wrap(V(4),V(-2));wrap.bind_inputs(binding);
    for(double x : {-10,-6,-2,2,6,10}) {value=V(x);require(sample(wrap)==V(-2),"PhaseWrap half-open boundaries and negative cycles");}
    value=V(-2.5);require(sample(wrap)==V(1.5),"PhaseWrap negative remainder");
    value=V(1.5);require(sample(wrap)==V(1.5),"PhaseWrap preserves values in range");
    value=std::numeric_limits<V>::quiet_NaN();rejects([&] {wrap.step();});
    s7::FrequencyMeterT<T> meter(8);meter.bind_inputs(binding);
    const double sequence[]={-1,0,1,0};
    for(int n=0;n<20;++n) {
        value=V(sequence[n%4]);const V hz=sample(meter);
        require(hz==V(n<5 ? 0 : 2),"Frequency measurement and first-valid timing");
        require(meter.output_ports()[1].template as<V>()==V(n>=5),"Frequency estimate validity");
    }
    for(int n=0;n<8;++n) {value=V(1);require(sample(meter)==V(2),"Frequency holds its last estimate without crossings");}
    value=std::numeric_limits<V>::infinity();rejects([&] {meter.step();});
    s7::FrequencyMeterT<T> interpolated(8);interpolated.bind_inputs(binding);
    const double asymmetric[]={-3,1,3,-1};
    for(int n=0;n<20;++n) {value=V(asymmetric[n%4]);const V hz=sample(interpolated);if(n>=5) require(hz==V(2),"Subsample crossing interpolation");}

    V derivative=V(4),enable=V(1),reset=V(0),reset_value=V(-0.75);
    const s7::InputBinding inputs[]={&derivative,&enable,&reset,&reset_value};
    s7::ControlledIntegratorT<T> integral(V(0.125),V(0),V(-1),V(1));integral.bind_inputs(inputs);
    require(sample(integral)==V(0.5)&&sample(integral)==V(1)&&sample(integral)==V(1),"Integrator accumulation and state saturation");
    derivative=V(-4);require(sample(integral)==V(0.5),"Limited integrator has no hidden windup");
    enable=V(0);derivative=std::numeric_limits<V>::quiet_NaN();
    require(sample(integral)==V(0.5),"Disabled integrator holds without using derivative");
    reset=V(1);require(sample(integral)==V(-0.75),"Reset overrides hold");
    reset_value=V(0.5);require(sample(integral)==V(-0.75),"Rising reset does not repeat while high");
    reset=V(0);(void)sample(integral);reset=V(1);reset_value=V(2);
    require(sample(integral)==V(1),"Reset value is limited");
    s7::ControlledIntegratorT<T> level(V(0.125),V(0),V(-1),V(1),s7::IntegratorReset::Level);level.bind_inputs(inputs);
    require(sample(level)==V(1),"Level reset clamps");reset_value=V(-0.5);
    require(sample(level)==V(-0.5),"Level reset repeats");
}

template <s7::SignalType T> void causal_publication() {
    using V=s7::signal_storage_t<T>;
    s7::Schema schema("causal_controls");
    schema.add_element("one",[] {return std::make_unique<s7::ConstantT<T>>(V(1));});
    schema.add_element("zero",[] {return std::make_unique<s7::ConstantT<T>>(V(0));});
    for(const char* id : {"a","b"}) {
        schema.add_element(id,[] {return std::make_unique<s7::ControlledIntegratorT<T>>(V(0.125));});
        schema.connect("one",0,id,1);schema.connect("zero",0,id,2);schema.connect("zero",0,id,3);
    }
    schema.connect("one",0,"a",0);schema.connect("a",0,"b",0);
    s7::NodeRuntime runtime(s7::Compiler::compile(schema));
    for(int n=0;n<8;++n) {
        runtime.step();
        require(runtime.output_of<T>("a")==V(n*0.125),"Controlled integrator publishes initial then previous update");
        require(runtime.output_of<T>("b")==V(n*(n-1)*0.125*0.125/2),"Controlled states do not leak same-step updates");
    }
    // A genuine feedback loop closes through the controlled integrator itself.
    s7::Schema feedback("feedback");
    feedback.add_element("state",[] {return std::make_unique<s7::ControlledIntegratorT<T>>(V(0.125),V(1));});
    feedback.add_element("gain",[] {return std::make_unique<s7::GainT<T>>(V(-1));});
    feedback.add_element("one",[] {return std::make_unique<s7::ConstantT<T>>(V(1));});
    feedback.add_element("zero",[] {return std::make_unique<s7::ConstantT<T>>(V(0));});
    feedback.connect("state",0,"gain",0);feedback.connect("gain",0,"state",0);
    feedback.connect("one",0,"state",1);feedback.connect("zero",0,"state",2);feedback.connect("zero",0,"state",3);
    require(s7::Validator::validate(feedback).ok(),"ControlledIntegrator supplies causal memory in feedback");
    s7::NodeRuntime decay(s7::Compiler::compile(feedback));decay.step();decay.step();
    require(decay.output_of<T>("state")==V(0.875),"Feedback follows local rectangle transition");
}

template <s7::SignalType T> void reset_publication() {
    using V=s7::signal_storage_t<T>;
    s7::Schema schema("causal_reset");
    schema.add_element("derivative",[] {return std::make_unique<s7::ConstantT<T>>(V(1));});
    schema.add_element("enable",[] {return std::make_unique<s7::ConstantT<T>>(V(1));});
    schema.add_element("reset_value",[] {return std::make_unique<s7::ConstantT<T>>(V(-0.5));});
    schema.add_element("reset",[] {return std::make_unique<s7::StepGeneratorT<T>>(V(0),V(1),8,2);});
    schema.add_element("state",[] {return std::make_unique<s7::ControlledIntegratorT<T>>(V(0.125));});
    schema.connect("derivative",0,"state",0);schema.connect("enable",0,"state",1);
    schema.connect("reset",0,"state",2);schema.connect("reset_value",0,"state",3);
    s7::NodeRuntime runtime(s7::Compiler::compile(schema));
    const double expected[]={0,.125,.25,-.5,-.375,-.25};
    for(const double value : expected) {runtime.step();require(runtime.output_of<T>("state")==V(value),"Reset is published next step and a held rising reset does not retrigger");}
}

void add(s7::Schema& schema,const char* id,const char* type,boost::json::object params={}) {
    const auto* descriptor=s7::ElementRegistry::instance().find(type);
    require(descriptor!=nullptr,"Control element registered");
    schema.add_element(s7::make_element_spec(id,*descriptor,std::move(params)));
}
void registry() {
    for(const char* type : {"F16","F32","F64","F128"}) for(const char* name : {"Switch","HysteresisRelay","ControlledIntegrator","PhaseWrap","FrequencyMeter"}) {
        s7::Schema schema("metadata");schema.set_rate(s7::Rate::from_hz(8));
        add(schema,"element",name,{{"type",type}});
        const auto* d=s7::ElementRegistry::instance().find(name);
        for(std::size_t i=0;i<d->metadata().input_ports.size();++i) {
            const auto id="input"+std::to_string(i);
            add(schema,id.c_str(),"Constant",{{"value",0}});
            schema.connect(id,0,"element",i);
        }
        require(s7::Validator::validate(schema).ok(),"Registry defaults, port types and inference");
        s7::NodeRuntime runtime(s7::Compiler::compile(schema));runtime.step();
    }
    s7::Schema integer("integer_switch");integer.set_rate(s7::Rate::from_hz(1));
    add(integer,"switch","Switch",{{"type","I16"}});
    for(int i=0;i<3;++i) {const auto id=std::to_string(i);add(integer,id.c_str(),"Constant",{{"value",i}});integer.connect(id,0,"switch",i);}
    s7::NodeRuntime runtime(s7::Compiler::compile(integer));runtime.step();
    require(runtime.output_of<s7::SignalType::I16>("switch")==1,"Integer switch");
    rejects([] {s7::HysteresisRelay invalid(1,1);});
    rejects([] {s7::ControlledIntegrator invalid(0);});
    rejects([] {s7::ControlledIntegrator invalid(.1,2,-1,1);});
    rejects([] {s7::PhaseWrap invalid(0);});
    rejects([] {s7::FrequencyMeter invalid(0);});
}
}
int main() {
    try {
        s7::register_builtin_elements(s7::ElementRegistry::instance());
        primitives<s7::SignalType::F16>();primitives<s7::SignalType::F32>();
        primitives<s7::SignalType::F64>();primitives<s7::SignalType::F128>();
        causal_publication<s7::SignalType::F16>();causal_publication<s7::SignalType::F32>();
        causal_publication<s7::SignalType::F64>();causal_publication<s7::SignalType::F128>();
        reset_publication<s7::SignalType::F16>();reset_publication<s7::SignalType::F32>();
        reset_publication<s7::SignalType::F64>();reset_publication<s7::SignalType::F128>();
        registry();std::cout<<"Local control elements: transitions, boundaries, types and causal publication passed\n";
    } catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
