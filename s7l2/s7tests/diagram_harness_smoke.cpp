// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Validates the headless diagram runner (diagram_harness.hpp,
// ../docs/VISUAL_NUMERICAL_TESTS_PLAN.md section 2) by re-expressing Stage
// 1.1's scalar linear decay (linear_scalar_smoke.cpp) as a Studio-openable
// .s7diagram + .probes.json pair instead of a hand-built C++ schema, and
// checking it reproduces the same numbers: the plan's own instruction
// ("validated against a re-expression of 1.1 as a diagram before any new
// case depends on it", section 6 step 4) before any new stage is built on
// top of this runner.

#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "s7/core.hpp"
#include "diagram_harness.hpp"

#ifndef S7TESTS_DIAGRAM_DIR
#error "S7TESTS_DIAGRAM_DIR must be defined by CMake to the diagrams/ directory."
#endif

namespace {

std::vector<double> analytical_decay(const double a, const double x0, const double dt, const std::size_t steps)
{
    std::vector<double> reference;
    reference.reserve(steps);
    for (std::size_t n = 1; n <= steps; ++n) {
        reference.push_back(x0 * std::exp(a * static_cast<double>(n) * dt));
    }
    return reference;
}

void test_diagram_matches_analytical_decay()
{
    const double a = -1.0;
    const double x0 = 1.0;
    const double dt = 0.01;
    const std::size_t steps = 100;

    auto diagram_case = s7tests::load_and_compile(std::filesystem::path(S7TESTS_DIAGRAM_DIR) / "euler_scalar_decay.s7diagram",
                                                   std::filesystem::path(S7TESTS_DIAGRAM_DIR) / "euler_scalar_decay.probes.json");
    const auto trace = s7tests::run_and_sample(diagram_case, steps);
    assert(trace.size() == steps);

    // Section 2 step 7: a full trace dump, for the separate gnuplot pipeline
    // (section 3), off by default so ctest stays fast.
    if (std::getenv("S7TESTS_DUMP_TRACE") != nullptr) {
        s7tests::write_trace("euler_scalar_decay.trace.dat", diagram_case.probes, trace);
    }

    std::vector<double> x_trace;
    x_trace.reserve(trace.size());
    for (const auto& snapshot : trace) {
        assert(snapshot.samples.size() == 1);
        x_trace.push_back(snapshot.samples.front().to_display_double());
    }

    const auto reference = analytical_decay(a, x0, dt, steps);
    double worst = 0.0;
    for (std::size_t i = 0; i < steps; ++i) {
        worst = std::max(worst, std::abs(x_trace[i] - reference[i]));
    }
    std::cout << "diagram euler scalar decay: max abs error=" << worst << '\n';
    if (worst > 1e-2) {
        throw std::runtime_error("diagram euler scalar decay error too large: " + std::to_string(worst));
    }

    // Model time: dt=0.01, so after 100 steps the runtime should read 1.0 s.
    const auto last_model_time_ns = trace.back().model_time_ns;
    assert(last_model_time_ns.has_value());
    const double last_model_time_s = static_cast<double>(*last_model_time_ns) / 1e9;
    if (std::abs(last_model_time_s - 1.0) > 1e-9) {
        throw std::runtime_error("diagram model time did not advance by the diagram's own dt");
    }
}

// Stage 0's own requirement (numerical_harness.hpp), re-checked through the
// diagram runner: the same diagram, run twice, must produce identical traces.
void test_diagram_is_deterministic()
{
    const std::size_t steps = 50;
    auto run = [&] {
        auto diagram_case = s7tests::load_and_compile(std::filesystem::path(S7TESTS_DIAGRAM_DIR) / "euler_scalar_decay.s7diagram",
                                                       std::filesystem::path(S7TESTS_DIAGRAM_DIR) / "euler_scalar_decay.probes.json");
        return s7tests::run_and_sample(diagram_case, steps);
    };
    const auto trace_a = run();
    const auto trace_b = run();
    assert(trace_a.size() == steps && trace_b.size() == steps);
    for (std::size_t i = 0; i < steps; ++i) {
        assert(trace_a[i].samples.front().to_display_double() == trace_b[i].samples.front().to_display_double());
    }
}

} // namespace

int main() try {
    s7::register_builtin_elements(s7::ElementRegistry::instance());
    test_diagram_matches_analytical_decay();
    test_diagram_is_deterministic();
    std::cout << "diagram_harness_smoke: all checks passed\n";
    return 0;
} catch (const std::exception& ex) {
    std::cerr << "diagram_harness_smoke: fatal error: " << ex.what() << '\n';
    return 1;
} catch (...) {
    std::cerr << "diagram_harness_smoke: fatal error: unknown exception\n";
    return 1;
}
