// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// SymplecticIntegrator (semi-implicit Euler / Euler-Cromer), added after
// examples/2int_osc.s7diagram showed its amplitude drifting above the
// theoretical exact value of 1.0 -- that diagram's x'=-v, v'=x loop through
// two plain EulerIntegrator elements is forward Euler on both state
// variables, unconditionally unstable on a center (energy-conserving,
// eigenvalues +-i) system (docs/OPEN_PROBLEMS.md).
//
// - test_plain_euler_drifts_as_predicted(): the forward-Euler loop's
//   amplitude growth matches the exact discrete amplification factor
//   sqrt(1+(omega*dt)^2) per step, not merely "grows".
// - test_symplectic_integrator_bounds_energy(): x''=-x through one
//   SymplecticIntegrator (acceleration=-position via Gain(-1)) stays bounded
//   near amplitude 1.0 for the whole run, far tighter than plain Euler's
//   drift over the same duration.
// - test_symplectic_integrator_registry_matches_closed_form(): the registry
//   path (make_element_spec, declared parameter/port names) against a
//   hand-derived two-step value, which also pins position/velocity port
//   order (the two expected values are not interchangeable).
//
// Systematic comparison of these (and other) integrators on more
// complicated, nonlinear systems (pendulum, Van der Pol, double pendulum)
// is intentionally not attempted here; see docs/OPEN_PROBLEMS.md.

#include <cassert>
#include <cmath>
#include <cstddef>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <vector>

#include "s7/core.hpp"

namespace {

constexpr double kDt = 0.001;
constexpr double kOmega = 1.0;
constexpr std::size_t kSteps = 50000; // 50 s simulated

// x' = -v, v' = x (omega=1): exactly examples/2int_osc.s7diagram's own
// element graph, built directly in C++ rather than loaded from the file (no
// JSON-diagram loader lives in s7core/s7tests yet).
s7::NodeRuntime make_plain_euler_oscillator()
{
    s7::Schema schema{"plain_euler_oscillator"};
    schema.add_element("x", [] { return std::make_unique<s7::EulerIntegrator>(kDt, 1.0); });
    schema.add_element("v", [] { return std::make_unique<s7::EulerIntegrator>(kDt, 0.0); });
    schema.add_element("zero", [] { return std::make_unique<s7::Constant>(0.0); });
    schema.add_element("neg_v", [] { return std::make_unique<s7::Adder>(std::vector<double>{1.0, -1.0}); });
    schema.connect("zero", 0, "neg_v", 0);
    schema.connect("v", 0, "neg_v", 1);
    schema.connect("neg_v", 0, "x", 0);
    schema.connect("x", 0, "v", 0);

    const auto validation = s7::Validator::validate(schema);
    if (!validation.ok()) {
        throw std::runtime_error("plain_euler_oscillator failed validation");
    }
    return s7::NodeRuntime{s7::Compiler::compile(schema)};
}

// x'' = -x (omega=1) through one SymplecticIntegrator: acceleration is fed
// back from the integrator's own position output through Gain(-1).
s7::NodeRuntime make_symplectic_oscillator()
{
    s7::Schema schema{"symplectic_oscillator"};
    schema.add_element("integ", [] { return std::make_unique<s7::SymplecticIntegrator>(kDt, 1.0, 0.0); });
    schema.add_element("neg", [] { return std::make_unique<s7::Gain>(-1.0); });
    schema.connect("integ", 0, "neg", 0);
    schema.connect("neg", 0, "integ", 0);

    const auto validation = s7::Validator::validate(schema);
    if (!validation.ok()) {
        throw std::runtime_error("symplectic_oscillator failed validation");
    }
    return s7::NodeRuntime{s7::Compiler::compile(schema)};
}

void test_plain_euler_drifts_as_predicted()
{
    auto runtime = make_plain_euler_oscillator();
    const double exact_growth_per_step = std::sqrt(1.0 + (kOmega * kDt) * (kOmega * kDt));

    double max_relative_error = 0.0;
    for (std::size_t step = 1; step <= kSteps; ++step) {
        runtime.step();
        const double x = runtime.real_output_of("x");
        const double v = runtime.real_output_of("v");
        const double amplitude = std::sqrt(x * x + v * v);
        // Causal-memory (Feedthrough::None) outputs lag by one step
        // (ELEMENT_DEV_GUIDE.md: "keeps their published outputs unchanged
        // until the step ends, and publishes the new outputs at the start of
        // the next step"), so after `step` calls to runtime.step() the
        // observed state reflects only `step - 1` applications of the map.
        const double theoretical = std::pow(exact_growth_per_step, static_cast<double>(step - 1));
        max_relative_error = std::max(max_relative_error, std::abs(amplitude - theoretical) / theoretical);
    }

    std::cout << "plain Euler oscillator: amplitude vs. sqrt(1+(omega*dt)^2)^N theory, max relative error="
               << max_relative_error << '\n';
    // The discrete map's eigenvalues are exactly 1 +/- i*omega*dt, so this
    // should match to floating-point precision, not just approximately.
    if (max_relative_error > 1e-9) {
        throw std::runtime_error("plain Euler oscillator amplitude did not match the predicted forward-Euler "
                                 "amplification factor");
    }

    // At t=50s this is a measurable ~2.5% drift -- confirms the mechanism
    // actually bites over a realistic run, not just in the limit.
    const double final_x = runtime.real_output_of("x");
    const double final_v = runtime.real_output_of("v");
    const double final_amplitude = std::sqrt(final_x * final_x + final_v * final_v);
    if (final_amplitude < 1.02) {
        throw std::runtime_error("expected plain Euler's amplitude to have drifted measurably above 1.0 by t=50s, got "
                                 + std::to_string(final_amplitude));
    }
}

void test_symplectic_integrator_bounds_energy()
{
    auto runtime = make_symplectic_oscillator();

    double min_amplitude = 1e9;
    double max_amplitude = 0.0;
    for (std::size_t step = 1; step <= kSteps; ++step) {
        runtime.step();
        const double x = runtime.real_output_of("integ", 0);
        const double v = runtime.real_output_of("integ", 1);
        const double amplitude = std::sqrt(x * x + v * v);
        min_amplitude = std::min(min_amplitude, amplitude);
        max_amplitude = std::max(max_amplitude, amplitude);
    }

    std::cout << "symplectic oscillator: amplitude range over " << kSteps << " steps = [" << min_amplitude << ", "
               << max_amplitude << "]\n";
    // Semi-implicit Euler on a harmonic oscillator keeps a modified energy
    // bounded (no secular drift), unlike plain forward Euler above: expect a
    // small ripple around 1.0, not monotonic growth. 5e-3 is generous
    // headroom over the ~2.5e-4 ripple actually observed at this dt.
    if (std::abs(max_amplitude - 1.0) > 5e-3 || std::abs(min_amplitude - 1.0) > 5e-3) {
        throw std::runtime_error("expected the symplectic integrator's amplitude to stay within 5e-3 of 1.0, got range ["
                                 + std::to_string(min_amplitude) + ", " + std::to_string(max_amplitude) + "]");
    }
}

// Through the registry (make_element_spec, declared "dt"/"initial_position"/
// "initial_velocity" parameters and "acceleration"/"position"/"velocity"
// ports), against a hand-derived value for the first application of the
// map: v[1] = v[0] + dt*a[0] = -dt*x[0]; x[1] = x[0] + dt*v[1] = x[0] - dt^2.
// Causal-memory outputs lag one step() call (see
// test_plain_euler_drifts_as_predicted's comment), so two calls are needed
// to observe this one application. The two expected values (1 - dt^2 and
// -dt) are not interchangeable, so this also pins position/velocity port
// order -- a forward-Euler (non-symplectic) scheme would instead give
// position = x[0] = 1 exactly, so this also distinguishes the two schemes.
void test_symplectic_integrator_registry_matches_closed_form()
{
    auto& registry = s7::ElementRegistry::instance();
    s7::register_builtin_elements(registry);
    const auto* descriptor = registry.find("SymplecticIntegrator");
    assert(descriptor != nullptr);

    const double dt = 0.1;
    s7::Schema schema{"symplectic_registry_check"};
    schema.set_rate(s7::Rate::from_seconds(dt)); // the run's dt, which the integrator follows
    schema.add_element(s7::make_element_spec("integ", *descriptor,
                                              {{"type", "F64"}, {"initial_position", 1.0}, {"initial_velocity", 0.0}}));
    schema.add_element("neg", [] { return std::make_unique<s7::Gain>(-1.0); });
    schema.connect("integ", 0, "neg", 0);
    schema.connect("neg", 0, "integ", 0);

    const auto validation = s7::Validator::validate(schema);
    if (!validation.ok()) {
        throw std::runtime_error("symplectic_registry_check failed validation");
    }
    s7::NodeRuntime runtime{s7::Compiler::compile(schema)};
    runtime.step();
    runtime.step();
    const double position = runtime.real_output_of("integ", 0);
    const double velocity = runtime.real_output_of("integ", 1);
    const double expected_position = 1.0 - dt * dt;
    const double expected_velocity = -dt;
    if (std::abs(position - expected_position) > 1e-12 || std::abs(velocity - expected_velocity) > 1e-12) {
        throw std::runtime_error("SymplecticIntegrator through the registry did not match the hand-derived "
                                 "semi-implicit Euler step: position=" + std::to_string(position) +
                                 " velocity=" + std::to_string(velocity));
    }
}

} // namespace

int main() try {
    test_plain_euler_drifts_as_predicted();
    test_symplectic_integrator_bounds_energy();
    test_symplectic_integrator_registry_matches_closed_form();
    std::cout << "symplectic_oscillator_smoke: all checks passed\n";
    return 0;
} catch (const std::exception& ex) {
    std::cerr << "symplectic_oscillator_smoke: fatal error: " << ex.what() << '\n';
    return 1;
} catch (...) {
    std::cerr << "symplectic_oscillator_smoke: fatal error: unknown exception\n";
    return 1;
}
