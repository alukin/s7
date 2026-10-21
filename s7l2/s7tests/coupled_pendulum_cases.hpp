// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "diagram_harness.hpp"

#include <array>
#include <complex>
#include <iomanip>
#include <string_view>

namespace s7tests::pendulum {

using State = std::array<double, 4>; // theta1, omega1, theta2, omega2
inline constexpr double duration = 20.0;
inline constexpr std::array frequencies{1000, 10000, 100000};
inline constexpr std::array<std::string_view, 3> methods{"euler", "symplectic", "trapezoid"};
inline constexpr std::array<std::string_view, 3> modes{"slow", "fast", "mixed"};

inline std::string name(std::string_view method, std::string_view mode)
{
    return "double_pendulum_" + std::string(mode) + "_" + std::string(method);
}

inline State initial(std::string_view mode)
{
    const double root2 = std::sqrt(2.0);
    if (mode == "slow") return {0.1, 0.0, root2 * 0.1, 0.0};
    if (mode == "fast") return {0.1, 0.0, -root2 * 0.1, 0.0};
    return {0.2, 0.0, 0.1, 0.0};
}

// Independent normal-mode solutions, derived from eigenvalues of the
// continuous equations, not from S7 elements or an S7 reference run.
// The discrete solutions additionally pin the actual graph realizations:
// Euler's one-step map, velocity-first symplectic Euler, and the coupled
// implicit trapezoidal rule.
inline State reference(std::string_view method, std::string_view mode, long n, double dt, bool continuous)
{
    using Real = long double;
    using Complex = std::complex<Real>;
    const Real root2 = std::sqrt(Real(2));
    const auto start = initial(mode);
    const std::array<Real, 2> amplitude{
        (Real(start[0]) + Real(start[2]) / root2) / 2,
        (Real(start[0]) - Real(start[2]) / root2) / 2};
    std::array<Real, 2> q{}, v{};
    for (int m = 0; m < 2; ++m) {
        const Real omega = std::sqrt(Real(9.81) * (2 + (m == 0 ? -root2 : root2)));
        const Real h = dt;
        const Real y = omega * h;
        if (continuous) {
            q[m] = amplitude[m] * std::cos(omega * n * h);
            v[m] = -amplitude[m] * omega * std::sin(omega * n * h);
        } else if (method == "symplectic") {
            // q[n+1] - (2-y^2) q[n] + q[n-1] = 0,
            // q[0]=A, q[1]=(1-y^2)A, v[n]=(q[n]-q[n-1])/h.
            const Real phi = 2 * std::asin(y / 2);
            const Real b = -y * y / (2 * std::sin(phi));
            const Real angle = Real(n) * phi;
            q[m] = amplitude[m] * (std::cos(angle) + b * std::sin(angle));
            v[m] = n == 0 ? 0 : amplitude[m] *
                ((std::cos(angle) - std::cos(angle - phi)) +
                 b * (std::sin(angle) - std::sin(angle - phi))) / h;
        } else {
            const Complex z(0, y);
            Complex evolution;
            if (method == "euler") {
                evolution = std::pow(Complex(1) + z, Real(n));
            } else {
                // Implicit trapezoid: (1-z/2)u[n+1]=(1+z/2)u[n].
                const Complex factor = (Complex(1) + z/Real(2)) / (Complex(1) - z/Real(2));
                evolution = std::pow(factor, Real(n));
            }
            q[m] = amplitude[m] * evolution.real();
            v[m] = -amplitude[m] * omega * evolution.imag();
        }
    }
    return {double(q[0] + q[1]), double(v[0] + v[1]),
            double(root2 * (q[0] - q[1])), double(root2 * (v[0] - v[1]))};
}

inline double energy(const State& z)
{
    // Physical mass matrix M=[[2,1],[1,1]], stiffness K=g*diag(2,1).
    return (2*z[1]*z[1] + 2*z[1]*z[3] + z[3]*z[3] +
            9.81*(2*z[0]*z[0] + z[2]*z[2])) / 2;
}

inline boost::json::object diagram(std::string_view method, std::string_view mode, double dt)
{
    auto root = read_json_object(std::filesystem::path(S7_PENDULUM_EXAMPLES_DIR) / (name(method, mode) + ".s7diagram"));
    root["dt"] = dt;
    for (auto& element : root.at("schema").as_object().at("elements").as_array()) {
        auto& params = element.as_object().at("params").as_object();
        if (params.contains("dt")) params["dt"] = dt;
    }
    return root;
}

struct Checkpoint { long step; State value; };
struct Result {
    double max_angle = 0, max_velocity = 0, sum_angle_squared = 0;
    double max_discrete = 0, max_energy_relative = 0;
    std::size_t samples = 0;
    std::vector<Checkpoint> checkpoints;
    double rms_angle() const { return std::sqrt(sum_angle_squared / (2 * samples)); }
};

// The runtime labels a post-step observation k*h. Causal-memory outputs
// contain z[k-1] for all three causal-memory methods, so these
// diagrams have the SAME state alignment. Check both
// labels and values explicitly; never hide this offset in a fitted phase.
inline Result run_headless(std::string_view method, std::string_view mode, int frequency,
                           const std::filesystem::path& trace_path = {})
{
    const double dt = 1.0 / frequency;
    const long steps = static_cast<long>(duration * frequency);
    const long stride = frequency / 1000; // compare on a common 1 ms grid
    auto root = diagram(method, mode, dt);
    auto schema = build_schema(root);
    const auto validation = s7::Validator::validate(schema);
    if (!validation.ok()) throw std::runtime_error(name(method, mode) + ": invalid schema");
    s7::NodeRuntime runtime(s7::Compiler::compile(schema));
    runtime.set_model_time_step_ns(std::llround(dt * 1e9));
    s7::ObservationService observer(runtime);
    std::vector<s7::ProbePoint> points;
    for (const auto& probe : root.at("probes").as_array()) {
        points.push_back({std::string(probe.as_object().at("element").as_string()),
                          probe.as_object().at("port").to_number<std::size_t>()});
    }
    std::ofstream trace;
    if (!trace_path.empty()) {
        trace.open(trace_path);
        if (!trace) throw std::runtime_error("Cannot write trace " + trace_path.string());
        trace << std::setprecision(17)
              << "step,time_s,state_time_s,theta1,omega1,theta2,omega2,ref_theta1,ref_omega1,ref_theta2,ref_omega2,energy_relative\n";
    }
    Result result;
    const double e0 = energy(initial(mode));
    for (long step = 1; step <= steps; ++step) {
        runtime.step();
        if (step != 1 && step != steps && (step - 1) % stride != 0) continue;
        const auto snapshot = observer.snapshot(points);
        if (snapshot.step != static_cast<std::size_t>(step) ||
            snapshot.model_time_ns != std::optional<std::int64_t>(step * std::llround(dt * 1e9))) {
            throw std::runtime_error("Incorrect runtime observation timestamp");
        }
        State actual{};
        const auto exact = reference(method, mode, step - 1, dt, true);
        const auto discrete = reference(method, mode, step - 1, dt, false);
        for (std::size_t i = 0; i < actual.size(); ++i) {
            actual[i] = snapshot.samples[i].as_real();
            if (!std::isfinite(actual[i])) throw std::runtime_error("Non-finite model output");
            const double error = std::abs(actual[i] - exact[i]);
            result.max_discrete = std::max(result.max_discrete, std::abs(actual[i] - discrete[i]));
            if (i % 2 == 0) {
                result.max_angle = std::max(result.max_angle, error);
                result.sum_angle_squared += error * error;
            } else {
                result.max_velocity = std::max(result.max_velocity, error);
            }
        }
        const double drift = energy(actual) / e0 - 1;
        result.max_energy_relative = std::max(result.max_energy_relative, std::abs(drift));
        result.checkpoints.push_back({step, actual});
        ++result.samples;
        if (trace.is_open() && (step == 1 || step == steps || (step - 1) % (10 * stride) == 0)) {
            trace << step << ',' << step * dt << ',' << (step - 1) * dt;
            for (const double x : actual) trace << ',' << x;
            for (const double x : exact) trace << ',' << x;
            trace << ',' << drift << '\n';
        }
    }
    if (result.max_discrete > 1e-8) {
        throw std::runtime_error(name(method, mode) + ": graph disagrees with its discrete reference: " +
                                 std::to_string(result.max_discrete));
    }
    // Quality budget at the finest tested dt, separate from exact map agreement.
    if (frequency == 100000 && result.max_angle > 0.002) {
        throw std::runtime_error(name(method, mode) + ": finest-step angle error exceeds 0.002 rad");
    }
    if (method == "symplectic" && result.max_energy_relative > 10 * dt) {
        throw std::runtime_error(name(method, mode) + ": symplectic energy error is not bounded by 10*dt");
    }
    if (method == "trapezoid" && result.max_energy_relative > 1e-8) {
        throw std::runtime_error(name(method, mode) + ": implicit trapezoid energy drift exceeds 1e-8");
    }
    if (method == "trapezoid" && frequency == 100000 && result.max_angle > 2e-8) {
        throw std::runtime_error(name(method, mode) + ": finest-step trapezoid angle error exceeds 2e-8 rad");
    }
    return result;
}

} // namespace s7tests::pendulum
