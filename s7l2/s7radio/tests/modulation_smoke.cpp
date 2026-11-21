// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: LGPL-3.0-or-later
#include "radio.hpp"
#include <s7/element_registry.hpp>
#include <array>
#include <cmath>
#include <limits>
#include <numbers>
#include <stdexcept>

int main() {
    auto& registry = s7::ElementRegistry::instance();
    s7::radio::register_elements(registry);
    const auto create = [&](const char* type, const boost::json::object& overrides = {}) {
        const auto& descriptor = *registry.find(type);
        const auto params = s7::checked_params(type, descriptor.metadata(), overrides);
        return descriptor.create({descriptor.metadata(), params});
    };

    double message{};
    const std::array input{s7::bind_input<s7::SignalType::F64>(message)};
    auto am = create("AmModulator", {{"depth", 1}});
    am->bind_inputs(input);
    for (const double sample : {-1.0, 0.0, 1.0}) {
        message = sample;
        am->step();
        if (am->output_ports().front().as<std::complex<float>>() !=
            std::complex<float>(static_cast<float>((1 + sample) / 2), 0)) return 1;
    }

    am = create("AmModulator", {{"depth", 0.6}});
    am->bind_inputs(input);
    auto am_demod = create("AmDemodulator", {{"depth", 0.6}});
    std::complex<float> received{};
    const std::array am_iq{s7::bind_input<s7::SignalType::C32>(received)};
    am_demod->bind_inputs(am_iq);
    for (const double sample : {-1.0, -0.25, 0.0, 0.5, 1.0}) {
        message = sample;
        am->step();
        received = am->output_ports().front().as<std::complex<float>>() * std::complex<float>(0, 1);
        am_demod->step();
        if (std::abs(am_demod->output_ports().front().as<double>() - sample) > 1e-6) return 5;
    }

    const boost::json::object fm_params{{"sample_rate_hz", 48000}, {"deviation_hz", 12000}};
    auto fm = create("FmModulator", fm_params);
    if (fm->declared_rate() != s7::Rate::reduced(48000, 1)) return 2;
    fm->bind_inputs(input);
    auto demod = create("FmDiscriminator");
    auto fm_demod = create("FmDemodulator", fm_params);
    if (fm_demod->declared_rate() != fm->declared_rate() || demod->declared_rate()) return 6;
    const std::array iq{s7::bind_input<s7::SignalType::C32>(
        fm->output_ports().front().as<std::complex<float>>())};
    demod->bind_inputs(iq);
    fm_demod->bind_inputs(iq);
    message = 0;
    fm->step();
    demod->step();
    fm_demod->step();
    if (demod->output_ports().front().as<double>() != 0 || fm_demod->output_ports().front().as<double>() != 0) return 3;
    for (const double sample : {1.0, 0.5, 1.0, -1.0, -1.0, -1.0, -1.0}) {
        message = sample;
        fm->step();
        demod->step();
        fm_demod->step();
        if (std::abs(demod->output_ports().front().as<double>() - sample * std::numbers::pi / 2) > 1e-6 ||
            std::abs(std::abs(fm->output_ports().front().as<std::complex<float>>()) - 1) > 1e-6) return 4;
        if (std::abs(fm_demod->output_ports().front().as<double>() - sample) > 1e-6) return 7;
    }
    fm_demod->bind_inputs(am_iq);
    received = {};
    fm_demod->step();
    if (fm_demod->output_ports().front().as<double>() != 0) return 8;
    received = {1, 0};
    fm_demod->step();
    if (fm_demod->output_ports().front().as<double>() != 0) return 9;
    try { (void)create("AmDemodulator", {{"depth", 0}}); return 10; }
    catch (const std::invalid_argument&) {}
    try { (void)create("FmDemodulator", {{"sample_rate_hz", 48000}, {"deviation_hz", 24000}}); return 11; }
    catch (const std::invalid_argument&) {}
    received = {std::numeric_limits<float>::infinity(), 0};
    for (auto* receiver : {am_demod.get(), fm_demod.get()}) {
        try { receiver->step(); return 12; }
        catch (const std::domain_error&) {}
    }
    return 0;
}
