#include <s7/core.hpp>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

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
    s7::StepGeneratorT<T> step(V(-2),V(3),8,2);
    s7::ImpulseGeneratorT<T> impulse(V(0.5),8,2);
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
    schema.add_element("delta",[] {return std::make_unique<s7::ImpulseGeneratorT<T>>(V(1),8,2);});
    schema.add_element("integral",[] {return std::make_unique<s7::EulerIntegratorT<T>>(V(0.125));});
    schema.connect("delta",0,"integral",0);
    s7::NodeRuntime runtime(s7::Compiler::compile(schema));
    for(int n=0;n<12;++n) {
        runtime.step();
        require(runtime.output_of<T>("integral")==V(n>2 ? 1 : 0), "Delta integrates to unit step with causal integrator latency");
    }
}

void registry() {
    auto& registry=s7::ElementRegistry::instance();
    s7::register_builtin_elements(registry);
    for (const char* type : {"TriangleGenerator","PulseGenerator","ImpulseGenerator","StepGenerator","ControlledOscillator"}) {
        for (const char* signal : {"F16","F32","F64","F128"}) {
            s7::Schema schema("registered_source");
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
        {"ImpulseGenerator",{{"sample_rate_hz",0}}},
        {"ControlledOscillator",{{"type","I32"}}}}) {
        s7::Schema schema("invalid");
        add(schema,"source",entry.first,entry.second);
        require(!s7::Validator::validate(schema).ok(), "Invalid registry parameter is rejected");
    }
    s7::Schema mismatch("mismatched_rates");
    add(mismatch,"source","ImpulseGenerator",{{"sample_rate_hz",8}});
    add(mismatch,"integral","EulerIntegrator",{{"dt",0.25}});
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
        registry();
        std::cout << "Sources: waveforms, VCO phase, delta integration, registry and validation passed\n";
    } catch(const std::exception& e) {std::cerr << e.what() << '\n';return 1;}
}
