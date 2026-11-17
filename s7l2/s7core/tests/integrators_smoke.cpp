// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include <s7/core.hpp>
#include <array>
#include <cmath>
#include <complex>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
template<class F> void rejects(F f) {
    bool threw=false;
    try { f(); } catch (const std::exception&) { threw=true; }
    require(threw,"Invalid integrator configuration/transition must fail explicitly");
}
template<s7::SignalType T> void local_quadrature() {
    using V=s7::signal_storage_t<T>; using R=s7::component_storage_t<T>;
    const V unit=[] { if constexpr(s7::is_complex_signal_type(T)) return V(R(1),R(.5)); else return V(1); }();
    V input{};
    s7::EulerIntegratorT<T> forward(R(.125));
    s7::BackwardEulerIntegratorT<T> backward(R(.125));
    s7::TrapezoidIntegratorT<T> trapezoid(R(.125),{},s7::TrapezoidMode::Sampled);
    const std::array<s7::InputBinding,1> bindings{&input};
    forward.bind_inputs(bindings); backward.bind_inputs(bindings); trapezoid.bind_inputs(bindings);
    // For forward Euler the state read before its transition is y[n]. The
    // runtime stages that transition for publication at the next step.
    for(int n=0;n<9;++n) {
        input=unit*R(n);
        require(forward.output_ports()[0].template as<V>()==unit*R(n*(n-1)/2)*R(.125),"Forward Euler left endpoint");
        backward.step(); trapezoid.step();
        require(backward.output_ports()[0].template as<V>()==unit*R(n*(n+1)/2)*R(.125),"Backward Euler right endpoint");
        require(trapezoid.output_ports()[0].template as<V>()==unit*R(n*n)*R(.0625),"Trapezoid endpoint average");
        forward.step();
    }
    require(forward.has_causal_memory() && !backward.has_causal_memory(),"Euler feedthrough distinction");
    for(const R h:{R(0),R(-1),std::numeric_limits<R>::infinity(),std::numeric_limits<R>::quiet_NaN()}) {
        rejects([&]{s7::EulerIntegratorT<T> x(h);});
        rejects([&]{s7::BackwardEulerIntegratorT<T> x(h);});
        rejects([&]{s7::TrapezoidIntegratorT<T> x(h);});
        rejects([&]{s7::SymplecticIntegratorT<T> x(h);});
    }
    s7::BackwardEulerIntegratorT<T> startup(R(.5),unit*R(3));
    startup.bind_inputs(bindings); input=unit*R(7); startup.step();
    require(startup.output_ports()[0].template as<V>()==unit*R(3),"Backward Euler preserves prescribed y[0], even with nonzero x[0]");
    input=unit*R(2); startup.step();
    require(startup.output_ports()[0].template as<V>()==unit*R(4),"Backward Euler integrates x[1] on first interval");
}

enum class Method {Forward,Backward,Trapezoid};
s7::Schema oscillator(Method method,double h,bool reverse=false) {
    s7::Schema schema("explicit_oscillator"); schema.set_rate(s7::Rate::from_seconds(h));
    auto x=[] {return std::make_unique<s7::Delay>(1.);};
    auto v=[] {return std::make_unique<s7::Delay>(0.);};
    if(reverse) {schema.add_element("v",v);schema.add_element("x",x);}
    else {schema.add_element("x",x);schema.add_element("v",v);}
    double a=1,b=h;
    if(method==Method::Backward) {a=1/(1+h*h);b=h/(1+h*h);}
    if(method==Method::Trapezoid) {const double q=h/2;a=(1-q*q)/(1+q*q);b=h/(1+q*q);}
    schema.add_element("x_next",[=]{return std::make_unique<s7::Adder>(std::vector<double>{a,b});});
    schema.add_element("v_next",[=]{return std::make_unique<s7::Adder>(std::vector<double>{-b,a});});
    schema.connect("x",0,"x_next",0);schema.connect("v",0,"x_next",1);
    schema.connect("x",0,"v_next",0);schema.connect("v",0,"v_next",1);
    schema.connect("x_next",0,"x",0);schema.connect("v_next",0,"v",0);
    return schema;
}
void oscillator_poles() {
    for(const auto method:{Method::Forward,Method::Backward,Method::Trapezoid})
        for(const double h:{.125,.5,3.}) for(const bool reverse:{false,true}) {
            s7::NodeRuntime runtime(s7::Compiler::compile(oscillator(method,h,reverse)));
            std::complex<double> pole;
            if(method==Method::Forward) pole={1,-h};
            else if(method==Method::Backward) pole=1./std::complex<double>(1,h);
            else pole=std::polar(1.,-2*std::atan(h/2));
            std::complex<double> expected{1,0};
            const int steps=h>1?40:400;
            for(int n=0;n<steps;++n) {
                runtime.step();
                const std::complex<double> actual{runtime.real_output_of("x"),runtime.real_output_of("v")};
                require(std::abs(actual-expected)<2e-12*std::max(1.,std::abs(expected)),"Explicit delay oscillator must match independent modal solution");
                expected*=pole;
            }
            const double energy=std::norm(std::complex<double>{runtime.real_output_of("x"),runtime.real_output_of("v")});
            if(method==Method::Trapezoid) require(std::abs(energy-1)<2e-12,"Trapezoid oscillator quadratic invariant");
            else if(method==Method::Forward) require(energy>1,"Forward Euler grows an undamped oscillator");
            else require(energy<1,"Backward Euler damps an undamped oscillator");
        }
}
void mit_leapfrog() {
    // MIT notes 6.3: position first, then velocity from that new position.
    // A direct dependency between the two adders expresses the update order;
    // only the two explicit delays own the previous state.
    const auto make=[](double h) {
        s7::Schema schema("mit_leapfrog");schema.set_rate(s7::Rate::from_seconds(h));
        schema.add_element("x",[]{return std::make_unique<s7::Delay>(1.);});
        schema.add_element("v",[]{return std::make_unique<s7::Delay>(0.);});
        schema.add_element("x_next",[=]{return std::make_unique<s7::Adder>(std::vector<double>{1,h});});
        schema.add_element("v_next",[=]{return std::make_unique<s7::Adder>(std::vector<double>{1,-h});});
        schema.connect("x",0,"x_next",0);schema.connect("v",0,"x_next",1);
        schema.connect("v",0,"v_next",0);schema.connect("x_next",0,"v_next",1);
        schema.connect("x_next",0,"x",0);schema.connect("v_next",0,"v",0);
        return schema;
    };
    for(const double h:{.125,.5,1.9}) {
        s7::NodeRuntime runtime(s7::Compiler::compile(make(h)));
        const double theta=2*std::asin(h/2);
        for(int n=0;n<1000;++n) {
            runtime.step();
            const double x=runtime.real_output_of("x"),v=runtime.real_output_of("v");
            const double expected=std::cos(n*theta)+std::tan(theta/2)*std::sin(n*theta);
            require(std::abs(x-expected)<3e-11,"MIT Leapfrog independent modal solution");
            require(std::abs(x*x+v*v+h*x*v-1)<3e-11,"MIT Leapfrog modified quadratic invariant");
        }
    }
    for(const double h:{2.,2.1}) {
        s7::NodeRuntime runtime(s7::Compiler::compile(make(h)));
        for(int n=0;n<20;++n) runtime.step();
        const double x=runtime.real_output_of("x"),v=runtime.real_output_of("v");
        require(x*x+v*v>100,"MIT Leapfrog boundary h=2 and larger steps are not bounded oscillator methods");
    }
}
void explicit_rc() {
    for(const double h:{.125,.5,2.,3.}) for(const auto method:{Method::Forward,Method::Backward,Method::Trapezoid}) {
        double pole=1-h;
        if(method==Method::Backward) pole=1/(1+h);
        if(method==Method::Trapezoid) pole=(1-h/2)/(1+h/2);
        s7::Schema schema("explicit_rc");schema.set_rate(s7::Rate::from_seconds(h));
        schema.add_element("state",[]{return std::make_unique<s7::Delay>(0.);});
        schema.add_element("u",[]{return std::make_unique<s7::Constant>(1.);});
        schema.add_element("next",[=]{return std::make_unique<s7::Adder>(std::vector<double>{pole,1-pole});});
        schema.connect("state",0,"next",0);schema.connect("u",0,"next",1);schema.connect("next",0,"state",0);
        s7::NodeRuntime runtime(s7::Compiler::compile(schema));
        for(int n=0;n<20;++n) {
            runtime.step();const double expected=1-std::pow(pole,n);
            require(std::abs(runtime.real_output_of("state")-expected)<1e-13*std::max(1.,std::abs(expected)),"Explicit RC recurrence with no iterative-solver timestep restriction");
        }
    }
}
void registry_and_validation() {
    s7::Schema schema("backward_registry");schema.set_rate(s7::Rate::from_hz(8));
    auto& registry=s7::ElementRegistry::instance();
    s7::register_builtin_elements(registry);
    schema.add_element("one",[]{return std::make_unique<s7::Constant>(1.);});
    schema.add_element(s7::make_element_spec("area",*registry.find("BackwardEulerIntegrator"),{{"initial_value",2.}}));
    schema.connect("one",0,"area",0);
    s7::NodeRuntime runtime(s7::Compiler::compile(schema));
    for(int n=0;n<5;++n) {runtime.step();require(runtime.real_output_of("area")==2+n*.125,"Backward Euler registry adopts domain rate");}
    s7::Schema loop("direct_backward_loop");
    loop.add_element("x",[]{return std::make_unique<s7::BackwardEulerIntegrator>(.125,1.);});
    loop.connect("x",0,"x",0);
    require(!s7::Validator::validate(loop).ok(),"Backward Euler direct feedthrough must not legalize an algebraic loop");
}
void finite_transitions() {
    double input=std::numeric_limits<double>::infinity(); const std::array<s7::InputBinding,1> bindings{&input};
    s7::EulerIntegrator forward(.125,3.);forward.bind_inputs(bindings);
    rejects([&]{forward.step();});require(forward.output_ports()[0].as<double>()==3.,"Failed Euler transition preserves state");
    s7::BackwardEulerIntegrator backward(.125,3.);backward.bind_inputs(bindings);
    rejects([&]{backward.step();});input=1.;backward.step();
    require(backward.output_ports()[0].as<double>()==3.,"Failed backward startup preserves initialization");
    input=std::numeric_limits<double>::infinity();rejects([&]{backward.step();});
    input=1.;backward.step();require(backward.output_ports()[0].as<double>()==3.125,"Backward retry must not consume failed input");
    for(const double initial:{std::numeric_limits<double>::infinity(),std::numeric_limits<double>::quiet_NaN()}) {
        rejects([&]{s7::EulerIntegrator x(.125,initial);});rejects([&]{s7::BackwardEulerIntegrator x(.125,initial);});
        rejects([&]{s7::TrapezoidIntegrator x(.125,initial);});rejects([&]{s7::SymplecticIntegrator x(.125,initial);});
    }
}
}
int main() try {
    local_quadrature<s7::SignalType::F16>();local_quadrature<s7::SignalType::F32>();
    local_quadrature<s7::SignalType::F64>();local_quadrature<s7::SignalType::F128>();
    local_quadrature<s7::SignalType::C16>();local_quadrature<s7::SignalType::C32>();
    local_quadrature<s7::SignalType::C64>();local_quadrature<s7::SignalType::C128>();
    oscillator_poles();mit_leapfrog();explicit_rc();registry_and_validation();finite_transitions();
    std::cout<<"Integrator recurrences, explicit feedback, modal behavior, types and failures passed\n";
} catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
