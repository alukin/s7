// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Opt-in, same-implementation Release benchmark. Prints compact CSV; no
// generated data belongs in the source tree.
#include <s7/core.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <ctime>
#include <iostream>
#include <memory>
#include <string>
#include <tuple>
#include <vector>

namespace {
enum class Method { Euler, Substeps, Trap, Gauss };

class AlternatingSource final : public s7::Element {
public:
    AlternatingSource() : Element("AlternatingSource", true) {
        register_output(0, "out", s7::SignalType::F64, &value_);
    }
    void step() override { value_ = -value_ + 1.; }
private:
    double value_{};
};

s7::Schema schema_for(bool rlc, double h, Method method, int m, int independent = 1,
                      bool dynamic_input = false) {
    s7::Schema schema(rlc ? "rlc_benchmark" : "rc_benchmark");
    for (int index = 0; index < independent; ++index) {
        const std::string suffix = "_" + std::to_string(index);
        const auto state = [&](const std::string& id, double initial) {
            schema.add_element(id+suffix, [=]() -> std::unique_ptr<s7::Element> {
                if (method == Method::Gauss) return std::make_unique<s7::GaussLegendreIntegrator>(h, initial);
                if (method == Method::Trap) return std::make_unique<s7::TrapezoidIntegrator>(h, initial);
                return std::make_unique<s7::EulerIntegrator>(h, initial, method == Method::Substeps ? m : 1);
            });
        };
        schema.add_element("input"+suffix, [=]() -> std::unique_ptr<s7::Element> {
            if (dynamic_input) return std::make_unique<AlternatingSource>();
            return std::make_unique<s7::Constant>(1.);
        });
        if (!rlc) {
            state("x", 0.);
            schema.add_element("rhs"+suffix, [] {
                return std::make_unique<s7::Adder>(std::vector<double>{1,-1});
            });
            schema.connect("input"+suffix, 0, "rhs"+suffix, 0);
            schema.connect("x"+suffix, 0, "rhs"+suffix, 1);
            schema.connect("rhs"+suffix, 0, "x"+suffix, 0);
        } else {
            state("q", 0.); state("i", 0.);
            schema.add_element("rhs"+suffix, [] {
                return std::make_unique<s7::Adder>(std::vector<double>{1,-1,-4});
            });
            schema.connect("i"+suffix, 0, "q"+suffix, 0);
            schema.connect("input"+suffix, 0, "rhs"+suffix, 0);
            schema.connect("i"+suffix, 0, "rhs"+suffix, 1);
            schema.connect("q"+suffix, 0, "rhs"+suffix, 2);
            schema.connect("rhs"+suffix, 0, "i"+suffix, 0);
        }
    }
    return schema;
}

std::pair<double,double> exact_rlc(double t) {
    const double w = std::sqrt(3.75), decay = std::exp(-t/2);
    return {.25*(1-decay*(std::cos(w*t)+std::sin(w*t)/(2*w))),
            decay*std::sin(w*t)/w};
}

struct Result {
    double error{}, wall{}, cpu{};
    int timed_steps{};
    s7::NodeRuntime::NumericalDiagnostics counts;
};

Result measure(bool rlc, double h, Method method, int m, int independent, int timed_steps) {
    auto schema = schema_for(rlc, h, method, m, independent);
    s7::NodeRuntime accuracy(s7::Compiler::compile(schema));
    accuracy.step();
    const int samples = static_cast<int>(std::llround((rlc ? 20. : 10.)/h));
    double error = 0;
    for (int n = 1; n <= samples; ++n) {
        accuracy.step();
        if (!rlc) {
            error = std::max(error, std::abs(accuracy.real_output_of("x_0")-(1-std::exp(-n*h))));
        } else {
            const auto [q,i] = exact_rlc(n*h);
            error = std::max({error, std::abs(accuracy.real_output_of("q_0")-q),
                              std::abs(accuracy.real_output_of("i_0")-i)});
        }
    }
    std::vector<double> wall, cpu;
    s7::NodeRuntime::NumericalDiagnostics timed_counts;
    for (int repeat = 0; repeat < 5; ++repeat) {
        s7::NodeRuntime runtime(s7::Compiler::compile(schema_for(rlc, h, method, m, independent, true)));
        const auto t0 = std::chrono::steady_clock::now();
        const auto c0 = std::clock();
        for (int n = 0; n < timed_steps; ++n) runtime.step();
        wall.push_back(std::chrono::duration<double>(std::chrono::steady_clock::now()-t0).count()/timed_steps);
        cpu.push_back(static_cast<double>(std::clock()-c0)/CLOCKS_PER_SEC/timed_steps);
        timed_counts = runtime.numerical_diagnostics();
    }
    std::ranges::sort(wall); std::ranges::sort(cpu);
    return {error,wall[2],cpu[2],timed_steps,timed_counts};
}
}

int main() try {
    std::cout << "model,method,h,states,max_state_error,wall_seconds_per_step,cpu_seconds_per_step,"
                 "derivative_vectors_per_step,pure_evaluations_per_step,newton_iterations_per_step,scratch_payload_bytes\n";
    for (bool rlc : {false, true}) {
        const double h = rlc ? .2 : .5;
        for (const auto [method,m,name] : {
                std::tuple{Method::Euler,1,"euler"},
                std::tuple{Method::Substeps,4,"euler_m4"},
                std::tuple{Method::Substeps,8,"euler_m8"},
                std::tuple{Method::Substeps,16,"euler_m16"},
                std::tuple{Method::Substeps,32,"euler_m32"},
                std::tuple{Method::Trap,1,"trapezoid"},
                std::tuple{Method::Gauss,1,"gauss_legendre_2"}}) {
            const auto result = measure(rlc,h,method,m,1,20000);
            std::cout << (rlc ? "rlc_r1" : "rc_charge") << ',' << name << ',' << h << ",1,"
                      << result.error << ',' << result.wall << ',' << result.cpu << ',';
            if (method == Method::Euler || method == Method::Trap) std::cout << "NA,NA,NA,NA\n";
            else std::cout << static_cast<double>(result.counts.base_derivative_vectors+
                                                  result.counts.trial_derivative_vectors)/result.timed_steps
                           << ',' << static_cast<double>(result.counts.pure_element_evaluations)/result.timed_steps << ','
                           << static_cast<double>(result.counts.solver_iterations)/result.timed_steps << ','
                           << result.counts.scratch_payload_bytes << '\n';
        }
    }
    for (int states : {1,2,4,8,16}) {
        const auto result = measure(false,.1,Method::Gauss,1,states,2000);
        std::cout << "independent_rc,gauss_legendre_2,.1," << states << ',' << result.error << ','
                  << result.wall << ',' << result.cpu << ",NA,NA,NA," << result.counts.scratch_payload_bytes << '\n';
    }
    return 0;
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
}
