// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

// Shared helpers for S7 numerical-modeling validation tests
// (see ../doc/S7_Numerical_Modeling_Test_Plan.md). These are plain functions,
// not a framework: tests still use assert()/throw in a hand-rolled main(),
// matching s7core/tests/core_smoke.cpp. This header only factors out the
// comparison/convergence math so every stage test uses the same definitions.
//
// Note on dt: each test here picks one `dt`, in simulated seconds, and
// passes it explicitly to every element constructor that needs it (and,
// where a test cares about the model-time axis rather than only the step
// index, to NodeRuntime::set_model_time_step_ns) -- s7::Schema still has no
// implicit propagation, by design (doc/S7_Sample_Rate_Model.md section 4:
// nothing converts or resamples implicitly, the same rule as SignalType).
//
// What changed since this comment was first written: two integrators
// silently disagreeing on dt is no longer just a convention a test author
// has to uphold by discipline. RateResolver (s7/rate_inference.hpp) is wired
// into Validator::validate()/Compiler::compile() as a mandatory check: an
// EulerIntegrator's own "dt" parameter pins its connected component's rate
// (Element::declared_rate()), and every schema below has exactly one
// integrator in its one connected component, so nothing here needs an
// explicit Schema::set_rate() seed -- the resolver already proves these
// schemas have one consistent rate, not merely infers it. A schema that
// mixed two differently-pinned elements with no rate-changing element
// between them would now fail validation instead of running silently wrong.

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

namespace s7tests {

// Runs `runtime.step()` `steps` times, sampling `probe()` (typically
// `[&]{ return runtime.real_output_of("x"); }`) after every step. The
// returned vector has `steps` entries: trace[n] is the state after the
// (n+1)-th step, i.e. the simulated value at time (n+1)*dt for a schema
// stepped at a fixed dt.
template <class Runtime, class Probe>
std::vector<double> run_and_sample(Runtime& runtime, Probe&& probe, std::size_t steps)
{
    std::vector<double> trace;
    trace.reserve(steps);
    for (std::size_t i = 0; i < steps; ++i) {
        runtime.step();
        trace.push_back(probe());
    }
    return trace;
}

inline double max_abs_error(const std::vector<double>& actual, const std::vector<double>& reference)
{
    const std::size_t n = std::min(actual.size(), reference.size());
    double worst = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        worst = std::max(worst, std::abs(actual[i] - reference[i]));
    }
    return worst;
}

inline double rms_error(const std::vector<double>& actual, const std::vector<double>& reference)
{
    const std::size_t n = std::min(actual.size(), reference.size());
    if (n == 0) {
        return 0.0;
    }
    double sum_sq = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        const double d = actual[i] - reference[i];
        sum_sq += d * d;
    }
    return std::sqrt(sum_sq / static_cast<double>(n));
}

// Observed order of convergence between two runs of the same method at
// step sizes h1 and h2 (h1 > h2) whose worst-case errors against an
// independent reference were e1 and e2: p such that error ~ C*h^p.
// A forward-Euler integrator should show p close to 1; a method with
// global error O(h^2) should show p close to 2. Only meaningful when both
// errors are well above floating-point noise.
inline double observed_order(double h1, double h2, double e1, double e2)
{
    return std::log(e1 / e2) / std::log(h1 / h2);
}

} // namespace s7tests
