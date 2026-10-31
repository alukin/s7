// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "numerical_integration_models.hpp"
#include "numerical_integration_research.hpp"

#include <s7/core.hpp>

#include <boost/json.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <numeric>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using s7tests::research::Case;
using s7tests::research::CoupledPolicy;
using s7tests::research::Model;
namespace json = boost::json;

struct Run {
    std::string label;
    Case test;
    std::size_t steps{};
    double max_error{};
    double final_error{};
    double final_energy_ratio{};
    double final_phase_error{};
    double max_relative_energy_drift{};
    double overshoot{};
    double wall_seconds{};
    double cpu_seconds{};
    int benchmark_repeats{5};
    int substeps{};
    std::size_t stage_scratch_bytes_estimate{};
    CoupledPolicy::Counts counts;
    std::string failure;
};

std::string model_name(const Case& test) {
    if (test.model == Model::Oscillator) return "oscillator";
    if (test.model == Model::RC) return test.input == 0 ? "rc_discharge" : "rc_charge";
    return "rlc_r" + std::to_string(static_cast<int>(test.resistance));
}
std::string file_label(const Case& test, const std::string& method) {
    return model_name(test) + "_h" + std::to_string(static_cast<int>(std::llround(test.h*1000))) + "_" + method;
}
double energy(const Case& test, const std::vector<double>& state) {
    if (test.model == Model::Oscillator) return .5*(state[0]*state[0]+state[1]*state[1]);
    if (test.model == Model::RLC) return .5*(4*state[0]*state[0]+state[1]*state[1]);
    return 0;
}
std::vector<double> policy_state(const CoupledPolicy& policy, const Case& test, bool staged) {
    const auto first = test.model == Model::RLC ? "q" : "x";
    std::vector<double> state{staged ? policy.staged_of(first) : policy.published_of(first)};
    if (test.model != Model::RC) {
        const auto second = test.model == Model::RLC ? "i" : "v";
        state.push_back(staged ? policy.staged_of(second) : policy.published_of(second));
    }
    return state;
}
std::vector<double> runtime_state(const s7::NodeRuntime& runtime, const Case& test, bool symplectic) {
    if (symplectic) return {runtime.real_output_of("state", 0), runtime.real_output_of("state", 1)};
    const auto first = test.model == Model::RLC ? "q" : "x";
    std::vector<double> state{runtime.real_output_of(first)};
    if (test.model != Model::RC) state.push_back(runtime.real_output_of(test.model == Model::RLC ? "i" : "v"));
    return state;
}
double state_error(const std::vector<double>& state, const std::vector<double>& reference) {
    double squared = 0;
    for (std::size_t i = 0; i < state.size(); ++i) squared += std::pow(state[i]-reference[i], 2);
    return std::sqrt(squared);
}
void record(std::ofstream& stream, std::size_t n, const Case& test, const std::vector<double>& state) {
    const auto reference = s7tests::research::exact_state(test, n*test.h);
    stream << n << ',' << std::setprecision(17) << n*test.h << ',' << state[0] << ','
           << (state.size() == 2 ? state[1] : 0) << ',' << reference[0] << ','
           << (reference.size() == 2 ? reference[1] : 0) << '\n';
}
void measure(Run& result, const std::vector<double>& state, std::size_t n) {
    const auto reference = s7tests::research::exact_state(result.test, n*result.test.h);
    const double error = state_error(state, reference);
    result.max_error = std::max(result.max_error, error);
    result.final_error = error;
    const auto initial_energy = energy(result.test, {result.test.initial_first, result.test.initial_second});
    if (initial_energy > 0)
        result.max_relative_energy_drift = std::max(result.max_relative_energy_drift,
                                                   std::abs(energy(result.test, state)/initial_energy-1));
    if (result.test.model == Model::RLC && result.test.input != 0)
        result.overshoot = std::max(result.overshoot, 4*state[0]-result.test.input);
    if (n == result.steps && result.test.model != Model::RC) {
        result.final_energy_ratio = initial_energy > 0 ? energy(result.test, state)/initial_energy : 0;
        if (result.test.model == Model::Oscillator)
            result.final_phase_error = std::remainder(std::atan2(-state[1], state[0])-n*result.test.h, 2*std::acos(-1.));
    }
}
template <typename Create, typename Step>
std::pair<double, double> benchmark(Create create, Step step, std::size_t count) {
    std::vector<double> wall, cpu;
    for (int repeat = 0; repeat < 5; ++repeat) {
        auto runner = create();
        const auto wall_start = std::chrono::steady_clock::now();
        const auto cpu_start = std::clock();
        for (std::size_t n = 0; n < count; ++n) step(runner);
        wall.push_back(std::chrono::duration<double>(std::chrono::steady_clock::now()-wall_start).count());
        cpu.push_back(static_cast<double>(std::clock()-cpu_start)/CLOCKS_PER_SEC);
    }
    std::ranges::sort(wall); std::ranges::sort(cpu);
    return {wall[2], cpu[2]};
}
Run run_policy(const Case& test, const std::string& label, const std::filesystem::path& directory,
               CoupledPolicy::Method method, int substeps) {
    auto schema = s7tests::research::schema_for(test);
    if (!s7::Validator::validate(schema).ok()) throw std::runtime_error("Research graph failed validation");
    CoupledPolicy policy(schema, s7::Compiler::compile(schema), test.h, method, substeps);
    Run result{.label=label, .test=test, .steps=static_cast<std::size_t>(std::llround((test.model == Model::Oscillator ? 100. : 20.)/test.h)), .substeps=substeps};
    std::ofstream csv(directory/(file_label(test, label)+".csv"));
    csv << "step,time_seconds,state0,state1,reference0,reference1\n";
    record(csv, 0, test, policy.published_state());
    try {
        for (std::size_t n = 1; n <= result.steps; ++n) {
            policy.step();
            const auto state = policy_state(policy, test, true);
            measure(result, state, n);
            record(csv, n, test, state);
        }
    } catch (const std::exception& error) {
        result.failure = error.what();
    }
    result.counts = policy.counts();
    result.stage_scratch_bytes_estimate = policy.stage_scratch_bytes_estimate();
    if (result.failure.empty()) {
        const auto create = [&] {
            auto bench_schema = s7tests::research::schema_for(test);
            return CoupledPolicy(bench_schema, s7::Compiler::compile(bench_schema), test.h, method, substeps);
        };
        const auto [wall, cpu] = benchmark(create, [](auto& runner) { runner.step(); }, result.steps);
        result.wall_seconds = wall;
        result.cpu_seconds = cpu;
    }
    return result;
}
Run run_production(const Case& test, const std::string& label, const std::filesystem::path& directory,
                   bool symplectic) {
    s7::Schema schema = [&] {
        if (!symplectic) return s7tests::research::schema_for(test, false, true);
        s7::Schema oscillator("phase3_symplectic_reference");
        oscillator.add_element("state", [=] { return std::make_unique<s7::SymplecticIntegrator>(test.h, 1., 0.); });
        oscillator.add_element("minus_x", [] { return std::make_unique<s7::Gain>(-1.); });
        oscillator.connect("state", 0, "minus_x", 0);
        oscillator.connect("minus_x", 0, "state", 0);
        return oscillator;
    }();
    if (!s7::Validator::validate(schema).ok()) throw std::runtime_error("Production reference graph failed validation");
    s7::NodeRuntime runtime(s7::Compiler::compile(schema));
    Run result{.label=label, .test=test, .steps=static_cast<std::size_t>(std::llround((test.model == Model::Oscillator ? 100. : 20.)/test.h))};
    std::ofstream csv(directory/(file_label(test, label)+".csv"));
    csv << "step,time_seconds,state0,state1,reference0,reference1\n";
    try {
        for (std::size_t n = 0; n <= result.steps; ++n) {
            runtime.step();  // first call publishes x0; call N+1 publishes xN
            const auto state = runtime_state(runtime, test, symplectic);
            measure(result, state, n);
            record(csv, n, test, state);
        }
    } catch (const std::exception& error) {
        result.failure = error.what();
    }
    result.counts.accepted_steps = runtime.step_count();
    result.counts.failed_steps = result.failure.empty() ? 0 : 1;
    if (result.failure.empty()) {
        const auto create = [&] {
            auto bench_schema = [&] {
                if (!symplectic) return s7tests::research::schema_for(test, false, true);
                s7::Schema oscillator("phase3_symplectic_benchmark");
                oscillator.add_element("state", [=] { return std::make_unique<s7::SymplecticIntegrator>(test.h, 1., 0.); });
                oscillator.add_element("minus_x", [] { return std::make_unique<s7::Gain>(-1.); });
                oscillator.connect("state", 0, "minus_x", 0);
                oscillator.connect("minus_x", 0, "state", 0);
                return oscillator;
            }();
            return s7::NodeRuntime(s7::Compiler::compile(bench_schema));
        };
        const auto [wall, cpu] = benchmark(create, [](auto& runner) { runner.step(); }, result.steps+1);
        result.wall_seconds = wall;
        result.cpu_seconds = cpu;
    }
    return result;
}
json::object to_json(const Run& run) {
    json::object result;
    result["model"] = model_name(run.test);
    result["method"] = run.label;
    result["h"] = run.test.h;
    result["outer_steps"] = run.steps;
    result["accepted_steps"] = run.counts.accepted_steps;
    result["failed_steps"] = run.counts.failed_steps;
    result["failure"] = run.failure.empty() ? json::value(nullptr) : json::value(run.failure);
    result["max_trajectory_error"] = run.max_error;
    result["final_trajectory_error"] = run.final_error;
    result["final_energy_ratio"] = run.final_energy_ratio;
    result["max_relative_energy_drift"] = run.max_relative_energy_drift;
    result["final_phase_error_rad"] = run.final_phase_error;
    result["overshoot"] = run.overshoot;
    result["wall_seconds"] = run.wall_seconds;
    result["cpu_seconds"] = run.cpu_seconds;
    result["timed_steps"] = run.failure.empty() ? run.counts.accepted_steps : 0;
    result["benchmark_repeats"] = run.benchmark_repeats;
    result["euler_substeps"] = run.substeps;
    const bool counted = run.label.starts_with("euler_m") || run.label.starts_with("euler_equal") ||
                         run.label == "gauss_legendre_2";
    result["stage_scratch_bytes_estimate"] = counted ? json::value(run.stage_scratch_bytes_estimate) : json::value(nullptr);
    result["base_derivative_evaluations"] = counted ? json::value(run.counts.base_derivatives) : json::value(nullptr);
    result["trial_derivative_evaluations"] = counted ? json::value(run.counts.trial_derivatives) : json::value(nullptr);
    result["total_derivative_evaluations"] = counted ? json::value(run.counts.total_derivatives()) : json::value(nullptr);
    result["pure_element_evaluations"] = counted ? json::value(run.counts.pure_element_evaluations) : json::value(nullptr);
    result["stage_residual_evaluations"] = counted ? json::value(run.counts.stage_residual_evaluations) : json::value(nullptr);
    result["jacobian_probes"] = counted ? json::value(run.counts.jacobian_probes) : json::value(nullptr);
    result["line_search_trials"] = counted ? json::value(run.counts.line_search_trials) : json::value(nullptr);
    result["solver_iterations"] = counted ? json::value(run.counts.solver_iterations) : json::value(nullptr);
    result["max_normalized_residual"] = counted ? json::value(run.counts.max_normalized_residual) : json::value(nullptr);
    return result;
}
}

int main(int argc, char** argv) try {
    if (argc != 2) throw std::invalid_argument("Usage: s7tests_numerical_integration_research OUTPUT_DIRECTORY");
    const std::filesystem::path directory(argv[1]);
    std::filesystem::create_directories(directory);
    json::array results;
    const auto run_case = [&](const Case& test) {
        for (int m : {1, 2, 4, 8, 16, 32})
            results.emplace_back(to_json(run_policy(test, "euler_m"+std::to_string(m), directory,
                                                    CoupledPolicy::Method::EulerSubsteps, m)));
        const auto gl = run_policy(test, "gauss_legendre_2", directory,
                                   CoupledPolicy::Method::GaussLegendre2, 1);
        results.emplace_back(to_json(gl));
        if (gl.failure.empty()) {
            const int equal_m = std::max(1, static_cast<int>(std::llround(
                static_cast<double>(gl.counts.total_derivatives())/gl.steps)));
            results.emplace_back(to_json(run_policy(test, "euler_equal_gl_work_m"+std::to_string(equal_m),
                                                    directory, CoupledPolicy::Method::EulerSubsteps, equal_m)));
        }
        results.emplace_back(to_json(run_production(test, "trapezoid", directory, false)));
        if (test.model == Model::Oscillator)
            results.emplace_back(to_json(run_production(test, "euler_cromer", directory, true)));
    };
    for (double h : {.05, .1, .2}) run_case(Case{Model::Oscillator, h, 1, 0, 1, 0});
    for (double h : {.1, .5, 2., 3.}) {
        run_case(Case{Model::RC, h, 1, 1, 0, 0});
        run_case(Case{Model::RC, h, 1, 0, 1, 0});
    }
    for (double h : {.05, .1, .2, .5}) {
        for (double resistance : {0., 1., 4., 6.})
            run_case(Case{Model::RLC, h, resistance, resistance == 0 ? 0. : 1.,
                          resistance == 0 ? .25 : 0., 0.});
    }
    json::object report;
    report["branch"] = "research/numerical-feedback-0.2.0";
    report["precision"] = "IEEE-754 binary64";
    report["solver"] = "damped finite-difference Newton, atol=rtol=1e-13, max_iterations=12";
    report["timing"] = "Research policy stages x_(n+1) after N calls; NodeRuntime publishes x_N after N+1 calls";
    report["benchmark"] = "Median of five Release C++ step-only loops; graph construction, trajectory checks and CSV output excluded";
    report["cases"] = std::move(results);
    std::ofstream out(directory/"results.json");
    out << json::serialize(report) << '\n';
    std::cout << "Wrote " << report["cases"].as_array().size() << " real-graph cases to " << directory << '\n';
    return 0;
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
}
