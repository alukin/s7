// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef S7TESTS_NUMERICAL_INTEGRATION_MODELS_HPP
#define S7TESTS_NUMERICAL_INTEGRATION_MODELS_HPP

#include <s7/core.hpp>

#include <cmath>
#include <memory>
#include <string>
#include <vector>

namespace s7tests::research {

enum class Model { Oscillator, RC, RLC };

struct Case {
    Model model;
    double h;
    double resistance{1};
    double input{0};
    double initial_first{0};
    double initial_second{0};
};

inline s7::Schema schema_for(const Case& test, bool reverse_states = false, bool trapezoid = false) {
    s7::Schema schema("phase3_linear_research");
    const auto add_integrator = [&](const std::string& id, double initial) {
        schema.add_element(id, [=]() -> std::unique_ptr<s7::Element> {
            if (trapezoid) return std::make_unique<s7::TrapezoidIntegrator>(test.h, initial);
            return std::make_unique<s7::EulerIntegrator>(test.h, initial);
        });
    };
    if (test.model == Model::RC) {
        add_integrator("x", test.initial_first);
        schema.add_element("input", [=] { return std::make_unique<s7::Constant>(test.input); });
        schema.add_element("rhs", [] { return std::make_unique<s7::Adder>(std::vector<double>{1, -1}); });
        schema.connect("input", 0, "rhs", 0);
        schema.connect("x", 0, "rhs", 1);
        schema.connect("rhs", 0, "x", 0);
    } else if (test.model == Model::Oscillator) {
        if (reverse_states) { add_integrator("v", test.initial_second); add_integrator("x", test.initial_first); }
        else { add_integrator("x", test.initial_first); add_integrator("v", test.initial_second); }
        schema.add_element("minus_x", [] { return std::make_unique<s7::Gain>(-1.); });
        schema.connect("v", 0, "x", 0);
        schema.connect("x", 0, "minus_x", 0);
        schema.connect("minus_x", 0, "v", 0);
    } else {
        if (reverse_states) { add_integrator("i", test.initial_second); add_integrator("q", test.initial_first); }
        else { add_integrator("q", test.initial_first); add_integrator("i", test.initial_second); }
        schema.add_element("input", [=] { return std::make_unique<s7::Constant>(test.input); });
        schema.add_element("rhs", [=] { return std::make_unique<s7::Adder>(std::vector<double>{1, -test.resistance, -4}); });
        schema.connect("i", 0, "q", 0);
        schema.connect("input", 0, "rhs", 0);
        schema.connect("i", 0, "rhs", 1);
        schema.connect("q", 0, "rhs", 2);
        schema.connect("rhs", 0, "i", 0);
    }
    return schema;
}

inline std::vector<double> exact_state(const Case& test, double t) {
    if (test.model == Model::Oscillator)
        return {test.initial_first*std::cos(t)+test.initial_second*std::sin(t),
                test.initial_second*std::cos(t)-test.initial_first*std::sin(t)};
    if (test.model == Model::RC)
        return {test.input+(test.initial_first-test.input)*std::exp(-t)};
    // L=1, C=0.25, so omega0=2. Primary step cases start at zero.
    if (test.resistance == 0 && test.input == 0) {
        return {test.initial_first*std::cos(2*t)+test.initial_second*std::sin(2*t)/2,
                test.initial_second*std::cos(2*t)-2*test.initial_first*std::sin(2*t)};
    }
    const double alpha = test.resistance/2;
    const double delta = alpha*alpha-4;
    double q = 0, current = 0;
    if (std::abs(delta) < 1e-14) {
        q = .25*test.input*(1-(1+alpha*t)*std::exp(-alpha*t));
        current = test.input*t*std::exp(-alpha*t);
    } else if (delta < 0) {
        const double wd = std::sqrt(-delta), decay = std::exp(-alpha*t);
        q = .25*test.input*(1-decay*(std::cos(wd*t)+alpha*std::sin(wd*t)/wd));
        current = test.input*decay*std::sin(wd*t)/wd;
    } else {
        const double root = std::sqrt(delta), p = -alpha+root, m = -alpha-root;
        q = .25*test.input*(1+(m*std::exp(p*t)-p*std::exp(m*t))/(p-m));
        current = test.input*(std::exp(p*t)-std::exp(m*t))/(p-m);
    }
    return {q, current};
}

} // namespace s7tests::research

#endif
