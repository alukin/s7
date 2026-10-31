// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: LGPL-3.0-or-later

#include <s7/core.hpp>
#include <boost/json.hpp>
#include <cmath>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <stdexcept>

void require(bool ok, const char* message) {
    if (!ok) throw std::runtime_error(message);
}

template <s7::SignalType T> void pulses() {
    using V = s7::signal_storage_t<T>;
    // Use an actual compiled graph to exercise port wiring and state scheduling.
    s7::Schema schema("pulses");
    schema.add_element("duty", [] { return std::make_unique<s7::ConstantT<T>>(V(0.25)); });
    schema.add_element("pwm", [] { return std::make_unique<s7::PwmT<T>>(8 / 100000.0, 100000.0); });
    schema.connect("duty", 0, "pwm", 0);
    s7::NodeRuntime runtime(s7::Compiler::compile(schema));
    for (int n=0; n<32; ++n) {
        runtime.step();
        require(runtime.output_of<T>("pwm") == V(n % 8 < 2), "PWM pulse sequence");
    }
}

void boundaries() {
    double duty = 0.5;
    s7::PWM pwm(0.004, 1000.0); // 4 samples
    const s7::InputBinding binding[] = {&duty};
    pwm.bind_inputs(binding);
    const auto gate = [&] { pwm.step(); return pwm.output_ports()[0].as<double>(); };
    require(gate() == 1, "First carrier sample high");
    duty = -1;
    require(gate() == 1 && gate() == 0 && gate() == 0, "Duty held until boundary");
    for (int n=0;n<4;++n) require(gate()==0, "Negative duty clamped off");
    duty=2;
    for (int n=0;n<4;++n) require(gate()==1, "Duty above one clamped on");
    duty=std::numeric_limits<double>::quiet_NaN();
    bool rejected=false;
    try { pwm.step(); } catch(const std::domain_error&) {rejected=true;}
    require(rejected, "Nonfinite duty rejected");
    rejected=false;
    try { s7::PWM invalid(0.001, 1000.0); } catch(const std::invalid_argument&) {rejected=true;}
    require(rejected, "Invalid carrier period rejected");
    rejected=false;
    try { s7::PWM fractional(0.0045, 1000.0); } catch(const std::invalid_argument&) {rejected=true;}
    require(rejected, "A period that is not a whole number of samples is rejected");
    // Without a rate the PWM follows the rate it adopts; the period stays a time.
    s7::PWM following(0.004, std::nullopt);
    require(!following.declared_rate(), "Following PWM declares no rate");
    following.adopt_rate(s7::Rate::from_hz(2000));
    double half = 0.5;
    const s7::InputBinding half_binding[] = {&half};
    following.bind_inputs(half_binding);
    int high = 0;
    for (int n = 0; n < 8; ++n) { following.step(); high += following.output_ports()[0].as<double>() == 1; }
    require(high == 4, "0.004 s at 2 kHz is an 8-sample carrier");
    rejected=false;
    s7::PWM odd(0.0045, std::nullopt);
    try { odd.adopt_rate(s7::Rate::from_hz(1000)); } catch(const std::invalid_argument&) {rejected=true;}
    require(rejected, "Adopting a rate the period does not fit is rejected");
}

void example() {
    std::ifstream file(S7_PWM_EXAMPLE);
    require(file.good(), "Example exists");
    const std::string text{std::istreambuf_iterator<char>(file), {}};
    const auto document = boost::json::parse(text).as_object();
    const auto& saved = document.at("schema").as_object();
    s7::register_builtin_elements(s7::ElementRegistry::instance());
    s7::Schema schema("pwm_pid_rlc");
    // The diagram's dt is the run rate every element follows, as in Studio.
    schema.set_rate(s7::Rate::from_seconds(document.at("dt").to_number<double>()));
    for (const auto& v : saved.at("elements").as_array()) {
        const auto& e = v.as_object();
        const auto* descriptor = s7::ElementRegistry::instance().find(std::string(e.at("type").as_string()));
        require(descriptor, "Example element registered");
        schema.add_element(s7::make_element_spec(std::string(e.at("id").as_string()), *descriptor, e.at("params").as_object()));
    }
    for (const auto& v : saved.at("connections").as_array()) {
        const auto& c = v.as_object();
        schema.connect(std::string(c.at("from_element").as_string()), c.at("from_port").as_int64(),
                       std::string(c.at("to_element").as_string()), c.at("to_port").as_int64());
    }
    const auto checked = s7::Validator::validate(schema);
    for (const auto& error : checked.errors) std::cerr << error << '\n';
    require(checked.ok(), "Example validates");
    s7::NodeRuntime runtime(s7::Compiler::compile(schema));
    double sum=0, peak=0, error=0, minimum_r=100, maximum_r=0;
    for (int n=0;n<100000;++n) {
        runtime.step();
        const double voltage=runtime.real_output_of("output_voltage");
        const double r=runtime.real_output_of("load_resistance");
        require(std::isfinite(voltage), "Finite voltage");
        require(runtime.real_output_of("duty_limit") >= 0 && runtime.real_output_of("duty_limit") <= 1, "Bounded duty");
        peak=std::max(peak,voltage); minimum_r=std::min(minimum_r,r); maximum_r=std::max(maximum_r,r);
        if(n>=50000) {sum+=voltage;error=std::max(error,std::abs(voltage-12));}
    }
    std::cout << "Mean voltage=" << sum/50000 << " V, peak=" << peak << " V, settled max error=" << error << " V\n";
    require(std::abs(sum/50000-12)<0.15, "Regulates average voltage under varying load");
    require(error<0.7 && peak<16, "Bounded ripple and startup overshoot");
    require(minimum_r<4.01 && maximum_r>11.99, "Load covers 4..12 ohm");
}
int main() {
    try {
        pulses<s7::SignalType::F16>(); pulses<s7::SignalType::F32>();
        pulses<s7::SignalType::F64>(); pulses<s7::SignalType::F128>();
        boundaries();
        example();
    } catch(const std::exception& e) {std::cerr << e.what() << '\n'; return 1;}
}
