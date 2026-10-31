// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: LGPL-3.0-or-later

#include <s7/core.hpp>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
void require(bool ok, const char* message) {
    if (!ok) throw std::runtime_error(message);
}
template <typename E> auto sample(E& element) {
    element.step();
    return element.output_ports()[0].template as<typename E::V>();
}
template <typename F> void rejects(F action) {
    bool rejected = false;
    try { action(); } catch (const std::exception&) { rejected = true; }
    require(rejected, "Expected invalid source configuration/input to be rejected");
}

template <s7::SignalType T> void waveforms() {
    using V = s7::signal_storage_t<T>;
    s7::TriangleGeneratorT<T> triangle(V(2), V(3), V(1), 8);
    const double expected[] = {1,2,3,4,5,4,3,2};
    for (int n=0;n<24;++n)
        require(sample(triangle)==V(expected[n%8]), "Triangle amplitude/offset/period");
    s7::TriangleGeneratorT<T> rising(V(1),V(0),V(1),4,0,1);
    s7::TriangleGeneratorT<T> falling(V(1),V(0),V(1),4,0,0);
    for (int n=0;n<12;++n) {
        require(sample(rising)==V(-1+0.5*(n%4)), "Rising saw resets at period");
        require(sample(falling)==V(1-0.5*(n%4)), "Falling saw resets at period");
    }
    s7::TriangleGeneratorT<T> reverse(V(1),V(0),V(-1),4,-0.25,1);
    for (double e : {0.5,0.0,-0.5,-1.0}) require(sample(reverse)==V(e), "Wrapped initial phase / reverse frequency");
    s7::TriangleGeneratorT<T> asymmetric(V(1),V(0),V(1),4,0,0.25);
    require(sample(asymmetric)==V(-1) && sample(asymmetric)==V(1), "Triangle rise fraction");
    s7::PulseGeneratorT<T> pulse(V(-2),V(3),V(1),8,0.25);
    for (int n=0;n<24;++n) require(sample(pulse)==V(n%8<2 ? 3 : -2), "Pulse duty and levels");
    s7::PulseGeneratorT<T> off(V(-2),V(3),V(1),8,0);
    s7::PulseGeneratorT<T> on(V(-2),V(3),V(1),8,1);
    for (int n=0;n<16;++n) require(sample(off)==V(-2) && sample(on)==V(3), "Pulse endpoint duty");
    s7::StepGeneratorT<T> step(V(-2),V(3),8.0,0.25);       // 2 samples at 8 Hz
    s7::ImpulseGeneratorT<T> impulse(V(0.5),8.0,0.25);
    for (int n=0;n<12;++n) {
        require(sample(step)==V(n<2 ? -2 : 3), "Step transition sample");
        require(sample(impulse)==V(n==2 ? 4 : 0), "Impulse is one sample, area/dt");
    }
    s7::StepGeneratorT<T> immediate;
    s7::ImpulseGeneratorT<T> delta;
    require(sample(immediate)==V(1), "Step at sample zero");
    require(sample(delta)==V(1000) && sample(delta)==V(0), "Delta at sample zero");

    // Timing stays accurate for F16 at sample rates that exceed its finite range.
    s7::TriangleGeneratorT<T> fast(V(1),V(0),V(1),100000);
    double peak=-2;
    for(int n=0;n<100000;++n) peak=std::max(peak,double(sample(fast)));
    require(peak>0.99, "Carrier phase advances at high sample rates for every output type");
}

template <s7::SignalType T> void vco() {
    using V = s7::signal_storage_t<T>;
    V frequency=V(1);
    s7::ControlledOscillatorT<T> osc(V(2),V(3),8);
    const s7::InputBinding bindings[]={&frequency};
    osc.bind_inputs(bindings);
    require(sample(osc)==V(3), "VCO initial phase"); // phase becomes 1/8
    frequency=V(2);
    require(std::abs(double(sample(osc))-(3+std::sqrt(2.0)))<0.005, "VCO frequency change preserves phase"); // 3/8
    frequency=V(0);
    const V held=sample(osc);
    require(sample(osc)==held, "Zero frequency holds phase");
    frequency=V(-1);
    (void)sample(osc); // 3/8 -> 1/4
    require(std::abs(double(sample(osc))-5)<0.005, "Negative frequency reverses phase");
    frequency=std::numeric_limits<V>::infinity();
    rejects([&] {osc.step();});
    s7::ControlledOscillatorT<T> phased(V(1),V(0),8,V(s7::math::pi_v<double>/2));
    frequency=V(0); phased.bind_inputs(bindings);
    require(std::abs(double(sample(phased))-1)<0.005, "Initial VCO phase in radians");
}

void add(s7::Schema& schema, const char* id, const char* type, boost::json::object params={}) {
    const auto* descriptor=s7::ElementRegistry::instance().find(type);
    require(descriptor!=nullptr, "Source is registered");
    schema.add_element(s7::make_element_spec(id,*descriptor,std::move(params)));
}

template <s7::SignalType T> void integrate_delta() {
    using V = s7::signal_storage_t<T>;
    s7::Schema schema("integrated_delta");
    schema.add_element("delta",[] {return std::make_unique<s7::ImpulseGeneratorT<T>>(V(1),8.0,0.25);});
    schema.add_element("integral",[] {return std::make_unique<s7::EulerIntegratorT<T>>(V(0.125));});
    schema.connect("delta",0,"integral",0);
    s7::NodeRuntime runtime(s7::Compiler::compile(schema));
    for(int n=0;n<12;++n) {
        runtime.step();
        require(runtime.output_of<T>("integral")==V(n>2 ? 1 : 0), "Delta integrates to unit step with causal integrator latency");
    }
}

template <s7::SignalType T> void sweeps_and_prbs() {
    using V = s7::signal_storage_t<T>;
    for (bool logarithmic : {false,true}) for (bool repeat : {false,true}) {
        s7::ChirpGeneratorT<T> source(V(1),V(4),0.125,64.0,V(2),V(3),V(0), // 8 samples at 64 Hz
            logarithmic ? s7::ChirpSweep::Logarithmic : s7::ChirpSweep::Linear,repeat);
        const double ratio=std::pow(4.0,1.0/8);
        const auto sum = [&](int n) {
            return logarithmic ? (std::pow(ratio,n)-1)/(ratio-1) : n+3.0*n*(n-1)/16;
        };
        for (int n=0;n<100;++n) {
            const double phase_sum = repeat ? (n/8)*sum(8)+sum(n%8) :
                n<=8 ? sum(n) : sum(8)+(n-8)*4;
            const double expected=3+2*std::sin(2*s7::math::pi_v<double>*phase_sum/64);
            require(std::abs(double(sample(source))-expected)<0.005, "Chirp phase follows frequency sum through sweep boundaries");
        }
    }
    for(unsigned order : {7u,15u}) {
        const std::size_t period=(1u<<order)-1;
        s7::PrbsGeneratorT<T> source(order,1,0.001,1000.0,V(-2),V(3));
        std::vector<unsigned> bits;
        std::size_t ones=0;
        for(std::size_t n=0;n<period;++n) {
            const V value=sample(source);
            require(value==V(-2)||value==V(3), "PRBS configured levels");
            bits.push_back(value==V(3)); ones+=bits.back();
        }
        require(ones==(1u<<(order-1)), "Maximal PRBS bit balance");
        std::vector<bool> seen(1u<<order);
        for(std::size_t n=0;n<period;++n) {
            unsigned word=0;
            for(unsigned k=0;k<order;++k) word=(word<<1)|bits[(n+k)%period];
            require(word!=0&&!seen[word], "Each nonzero PRBS window occurs exactly once per period");
            seen[word]=true;
            require(sample(source)==V(bits[n] ? 3 : -2), "PRBS repeats after its full period");
        }
    }
    s7::PrbsGeneratorT<T> slow(7,65,0.003), fast(7,65,0.001); // 3 and 1 samples at 1 kHz
    for(int n=0;n<50;++n) {
        const V expected=sample(fast);
        for(int k=0;k<3;++k) require(sample(slow)==expected, "PRBS bit hold and alternate seed");
    }
}

void registry() {
    auto& registry=s7::ElementRegistry::instance();
    s7::register_builtin_elements(registry);
    for (const char* type : {"TriangleGenerator","PulseGenerator","ImpulseGenerator","StepGenerator","ControlledOscillator","ChirpGenerator","PRBSGenerator"}) {
        for (const char* signal : {"F16","F32","F64","F128"}) {
            s7::Schema schema("registered_source");
            schema.set_rate(s7::Rate::from_hz(1000)); // document sources follow the schema's rate
            add(schema,"source",type,{{"type",signal}});
            if(std::string(type)=="ControlledOscillator") {
                add(schema,"frequency","Constant",{{"value",1}});
                schema.connect("frequency",0,"source",0);
            }
            require(s7::Validator::validate(schema).ok(), "Registry metadata, inference and defaults validate");
            s7::NodeRuntime runtime(s7::Compiler::compile(schema));
            runtime.step();
        }
    }
    for (const auto& entry : {
        std::pair{"TriangleGenerator",boost::json::object{{"rise_fraction",1.1}}},
        {"PulseGenerator",{{"duty",-0.1}}},
        {"StepGenerator",{{"start_sample",-1}}},
        {"ImpulseGenerator",{{"start_time",-1}}},
        {"StepGenerator",{{"start_time",0.0005}}}, // half a sample at 1 kHz
        {"PRBSGenerator",{{"bit_time",0.0015}}},
        {"ControlledOscillator",{{"type","I32"}}}}) {
        s7::Schema schema("invalid");
        schema.set_rate(s7::Rate::from_hz(1000));
        add(schema,"source",entry.first,entry.second);
        require(!s7::Validator::validate(schema).ok(), "Invalid registry parameter is rejected");
    }
    // Durations are times: a legacy sample count converts at its stored rate
    // (4 samples at 8 Hz = 0.5 s), then counts at the run's rate (8 at 16 Hz).
    for (const auto& params : {boost::json::object{{"start_sample",4},{"sample_rate_hz",8}},
                               boost::json::object{{"start_time",0.5}}}) {
        s7::Schema timed("timed_step");
        timed.set_rate(s7::Rate::from_hz(16));
        add(timed,"source","StepGenerator",params);
        s7::NodeRuntime runtime(s7::Compiler::compile(timed));
        for (int n=0;n<12;++n) {
            runtime.step();
            require(runtime.real_output_of("source")==(n<8 ? 0 : 1), "Step at 0.5 s whatever rate the document used");
        }
    }
    // Rate-authoritative pins (built in C++) still conflict.
    s7::Schema mismatch("mismatched_rates");
    mismatch.add_element("source",[] { return std::make_unique<s7::ImpulseGenerator>(1.0,8.0,0.0); });
    mismatch.add_element("integral",[] { return std::make_unique<s7::EulerIntegrator>(0.25); });
    mismatch.connect("source",0,"integral",0);
    require(!s7::Validator::validate(mismatch).ok(), "Conflicting model rates rejected");
}
}

int main() {
    try {
        waveforms<s7::SignalType::F16>(); waveforms<s7::SignalType::F32>();
        waveforms<s7::SignalType::F64>(); waveforms<s7::SignalType::F128>();
        vco<s7::SignalType::F16>(); vco<s7::SignalType::F32>();
        vco<s7::SignalType::F64>(); vco<s7::SignalType::F128>();
        integrate_delta<s7::SignalType::F16>(); integrate_delta<s7::SignalType::F32>();
        integrate_delta<s7::SignalType::F64>(); integrate_delta<s7::SignalType::F128>();
        rejects([] { s7::TriangleGenerator invalid(1,0,1,0); });
        rejects([] { s7::PulseGenerator invalid(0,1,1,1000,2); });
        rejects([] { s7::StepGenerator invalid(0,1,std::numeric_limits<double>::infinity()); });
        rejects([] { s7::ImpulseGeneratorT<s7::SignalType::F16> invalid(s7::float16_t(1),100000); });
        sweeps_and_prbs<s7::SignalType::F16>(); sweeps_and_prbs<s7::SignalType::F32>();
        sweeps_and_prbs<s7::SignalType::F64>(); sweeps_and_prbs<s7::SignalType::F128>();
        rejects([] {s7::ChirpGenerator bad(0,1,8,1000,1,0,0,s7::ChirpSweep::Logarithmic);});
        rejects([] {s7::ChirpGenerator bad(1,2,0.0);});
        rejects([] {s7::PRBSGenerator bad(8);});
        rejects([] {s7::PRBSGenerator bad(7,0);});
        rejects([] {s7::PRBSGenerator bad(7,128);});
        rejects([] {s7::PRBSGenerator bad(7,1,0.0);});
        rejects([] {s7::StepGenerator bad(0,1,1000.0,0.0005);}); // not a whole number of samples
        registry();
        std::cout << "Sources: waveforms, VCO/chirp phase, maximal PRBS periods, delta integration, registry and validation passed\n";
    } catch(const std::exception& e) {std::cerr << e.what() << '\n';return 1;}
}
