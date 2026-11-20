// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: LGPL-3.0-or-later
#include "radio.hpp"
#include <s7/element_registry.hpp>
#include <array>
#include <cmath>
#include <numbers>

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

    auto fm = create("FmModulator", {{"sample_rate_hz", 48000}, {"deviation_hz", 12000}});
    if (fm->declared_rate() != s7::Rate::reduced(48000, 1)) return 2;
    fm->bind_inputs(input);
    auto demod = create("FmDiscriminator");
    const std::array iq{s7::bind_input<s7::SignalType::C32>(
        fm->output_ports().front().as<std::complex<float>>())};
    demod->bind_inputs(iq);
    message = 0;
    fm->step();
    demod->step();
    if (demod->output_ports().front().as<double>() != 0) return 3;
    for (const double sample : {1.0, 0.5, 1.0, -1.0, -1.0, -1.0, -1.0}) {
        message = sample;
        fm->step();
        demod->step();
        if (std::abs(demod->output_ports().front().as<double>() - sample * std::numbers::pi / 2) > 1e-6 ||
            std::abs(std::abs(fm->output_ports().front().as<std::complex<float>>()) - 1) > 1e-6) return 4;
    }
    return 0;
}
