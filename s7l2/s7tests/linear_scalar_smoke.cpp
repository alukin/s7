// Stage 0 / Stage 1.1 of ../doc/S7_Numerical_Modeling_Test_Plan.md: a
// deterministic headless harness, then the scalar linear system
// x_dot = a*x against its analytical solution x(t) = x0 * exp(a*t).
//
// The reference is computed with std::exp directly in this file, not by
// running S7 at a very small dt: the test plan requires an independent
// reference, and a closed form is the strongest one available here.
//
// Graph: EulerIntegrator --x--> Gain(a) --a*x--> EulerIntegrator (input).
// EulerIntegrator is Feedthrough::None (causal memory), so this two-element
// loop is a valid cycle on its own and needs no separate Delay element: it
// is exactly forward Euler applied to x_dot = a*x, x[n+1] = x[n] + dt*a*x[n].
//
// TrapezoidIntegrator is deliberately not exercised the same way here: it
// has Feedthrough::Direct, so a bare integrator-gain loop around it is an
// algebraic cycle with no causal-memory element and the validator rejects
// it (core_smoke.cpp's make_invalid_schema covers the general case). Closing
// such a loop needs an explicit Delay, which changes the equation being
// solved (see AGENTS.md's execution-ordering note and the test plan's
// Stage 5 hypothesis about integrators gaining dynamics from how they are
// wired as causal graph elements, not from their nominal accuracy order).
// That case is follow-up work, not this test.

#include <cassert>
#include <cmath>
#include <cstddef>
#include <exception>
#include <iostream>
#include <memory>
#include <vector>

#include "s7/core.hpp"
#include "numerical_harness.hpp"

namespace {

// Builds a schema computing x_dot = a*x via forward Euler at the given dt,
// starting from x0, and returns the compiled runtime plus a probe for x.
s7::NodeRuntime make_euler_decay_runtime(double a, double x0, double dt)
{
    s7::Schema schema{"euler_scalar_decay"};
    schema.add_element("x", [=] { return std::make_unique<s7::EulerIntegrator>(dt, x0); });
    schema.add_element("ax", [=] { return std::make_unique<s7::Gain>(a); });
    schema.connect("x", 0, "ax", 0);
    schema.connect("ax", 0, "x", 0);

    const auto validation = s7::Validator::validate(schema);
    if (!validation.ok()) {
        throw std::runtime_error("euler_scalar_decay failed validation");
    }
    return s7::NodeRuntime{s7::Compiler::compile(schema)};
}

std::vector<double> analytical_decay(double a, double x0, double dt, std::size_t steps)
{
    std::vector<double> reference;
    reference.reserve(steps);
    for (std::size_t n = 1; n <= steps; ++n) {
        reference.push_back(x0 * std::exp(a * static_cast<double>(n) * dt));
    }
    return reference;
}

// Stage 0: the same model, run twice, must produce identical traces.
void test_deterministic_repeat()
{
    const double a = -1.0;
    const double x0 = 1.0;
    const double dt = 0.01;
    const std::size_t steps = 100;

    auto runtime_a = make_euler_decay_runtime(a, x0, dt);
    auto runtime_b = make_euler_decay_runtime(a, x0, dt);
    const auto trace_a = s7tests::run_and_sample(runtime_a, [&] { return runtime_a.real_output_of("x"); }, steps);
    const auto trace_b = s7tests::run_and_sample(runtime_b, [&] { return runtime_b.real_output_of("x"); }, steps);

    assert(trace_a.size() == steps);
    for (std::size_t i = 0; i < steps; ++i) {
        assert(trace_a[i] == trace_b[i]);
    }
}

// Stage 1.1: forward Euler on x_dot = a*x should converge to the analytical
// solution at first order as dt shrinks (global error roughly halves when
// dt halves), and match it closely at a small, fixed dt.
void test_euler_matches_analytical_and_converges_first_order()
{
    const double a = -1.0;
    const double x0 = 1.0;
    const double t_final = 1.0;

    const double h1 = 0.01;
    const double h2 = 0.005;

    const auto steps1 = static_cast<std::size_t>(std::llround(t_final / h1));
    const auto steps2 = static_cast<std::size_t>(std::llround(t_final / h2));

    auto runtime1 = make_euler_decay_runtime(a, x0, h1);
    auto trace1 = s7tests::run_and_sample(runtime1, [&] { return runtime1.real_output_of("x"); }, steps1);
    auto reference1 = analytical_decay(a, x0, h1, steps1);

    auto runtime2 = make_euler_decay_runtime(a, x0, h2);
    auto trace2 = s7tests::run_and_sample(runtime2, [&] { return runtime2.real_output_of("x"); }, steps2);
    auto reference2 = analytical_decay(a, x0, h2, steps2);

    const double e1 = s7tests::max_abs_error(trace1, reference1);
    const double e2 = s7tests::max_abs_error(trace2, reference2);

    // The reference exp(-1) at t=1 is about 0.3679; forward Euler at
    // dt=0.01 should already track it well within 1e-2.
    if (e1 > 1e-2) {
        throw std::runtime_error("euler decay error too large at dt=0.01: " + std::to_string(e1));
    }

    const double order = s7tests::observed_order(h1, h2, e1, e2);
    std::cout << "euler scalar decay: e(h=" << h1 << ")=" << e1 << " e(h=" << h2 << ")=" << e2
               << " observed order=" << order << '\n';

    // Forward Euler is first order; allow slack around 1.0 for a short,
    // non-asymptotic run.
    if (order < 0.8 || order > 1.3) {
        throw std::runtime_error("euler scalar decay: observed order " + std::to_string(order)
                                  + " is not close to the expected first order");
    }
}

// A nonzero steady input: x_dot = a*x + b, a<0, analytical solution
// x(t) = (x0 + b/a) * exp(a*t) - b/a. Exercises Adder/Constant alongside
// the integrator loop, still with EulerIntegrator closing the cycle.
void test_euler_forced_response()
{
    const double a = -2.0;
    const double b = 3.0;
    const double x0 = 0.0;
    const double dt = 0.001;
    // Time constant is 1/|a| = 0.5 s; run 10 time constants so the
    // analytical solution is within exp(-10) ~ 4.5e-5 of steady state.
    const double t_final = 5.0;
    const auto steps = static_cast<std::size_t>(std::llround(t_final / dt));

    s7::Schema schema{"euler_scalar_forced"};
    schema.add_element("x", [=] { return std::make_unique<s7::EulerIntegrator>(dt, x0); });
    schema.add_element("ax", [=] { return std::make_unique<s7::Gain>(a); });
    schema.add_element("b", [=] { return std::make_unique<s7::Constant>(b); });
    schema.add_element("sum", [] { return std::make_unique<s7::Adder>(); });
    schema.connect("x", 0, "ax", 0);
    schema.connect("ax", 0, "sum", 0);
    schema.connect("b", 0, "sum", 1);
    schema.connect("sum", 0, "x", 0);

    const auto validation = s7::Validator::validate(schema);
    if (!validation.ok()) {
        throw std::runtime_error("euler_scalar_forced failed validation");
    }
    auto runtime = s7::NodeRuntime{s7::Compiler::compile(schema)};
    auto trace = s7tests::run_and_sample(runtime, [&] { return runtime.real_output_of("x"); }, steps);

    std::vector<double> reference;
    reference.reserve(steps);
    const double steady = -b / a;
    for (std::size_t n = 1; n <= steps; ++n) {
        const double t = static_cast<double>(n) * dt;
        reference.push_back((x0 - steady) * std::exp(a * t) + steady);
    }

    const double e = s7tests::max_abs_error(trace, reference);
    std::cout << "euler scalar forced response: max abs error=" << e << " steady state=" << steady << '\n';
    if (e > 1e-2) {
        throw std::runtime_error("euler forced response error too large: " + std::to_string(e));
    }
    // Final value should be close to the analytical steady state.
    if (std::abs(trace.back() - steady) > 1e-2) {
        throw std::runtime_error("euler forced response did not settle near the steady state");
    }
}

} // namespace

int main() try {
    test_deterministic_repeat();
    test_euler_matches_analytical_and_converges_first_order();
    test_euler_forced_response();
    std::cout << "linear_scalar_smoke: all checks passed\n";
    return 0;
} catch (const std::exception& ex) {
    std::cerr << "linear_scalar_smoke: fatal error: " << ex.what() << '\n';
    return 1;
} catch (...) {
    std::cerr << "linear_scalar_smoke: fatal error: unknown exception\n";
    return 1;
}
