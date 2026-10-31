// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "numerical_integration_models.hpp"
#include "numerical_integration_research.hpp"

#include <s7/core.hpp>

#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>

namespace {
using s7tests::research::Case;
using s7tests::research::CoupledPolicy;
using s7tests::research::Model;
using s7tests::research::schema_for;

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
void near(double actual, double expected, double tolerance, const char* message) {
    if (std::abs(actual-expected) > tolerance) throw std::runtime_error(message);
}
CoupledPolicy policy_for(const Case& test, CoupledPolicy::Method method, int m = 1,
                         bool reverse = false, int limit = 12) {
    auto schema = schema_for(test, reverse);
    require(s7::Validator::validate(schema).ok(), "Research schema must validate");
    return CoupledPolicy(schema, s7::Compiler::compile(schema), test.h, method, m, limit);
}
void compare_states(const Case& test, const CoupledPolicy& policy, const s7::NodeRuntime& fine) {
    const char* first = test.model == Model::RLC ? "q" : "x";
    near(policy.published_of(first), fine.real_output_of(first), 2e-13, "Substep/fine Euler first state mismatch");
    if (test.model != Model::RC) {
        const char* second = test.model == Model::RLC ? "i" : "v";
        near(policy.published_of(second), fine.real_output_of(second), 2e-13, "Substep/fine Euler second state mismatch");
    }
}
void euler_equivalence() {
    for (const Case test : {Case{Model::Oscillator, .2, 1, 0, 1, 0},
                            Case{Model::RC, .2, 1, 1, 0, 0},
                            Case{Model::RLC, .2, 1, 1, 0, 0},
                            Case{Model::RLC, .2, 6, 1, 0, 0}}) {
        for (const int m : {1, 2, 4, 8, 16, 32}) {
            auto policy = policy_for(test, CoupledPolicy::Method::EulerSubsteps, m);
            auto fine_case = test;
            fine_case.h /= m;
            auto fine_schema = schema_for(fine_case);
            s7::NodeRuntime fine(s7::Compiler::compile(fine_schema));
            for (int outer = 0; outer < 16; ++outer) {
                policy.step();
                for (int j = 0; j < (outer == 0 ? 1 : m); ++j) fine.step();
                compare_states(test, policy, fine);
            }
            const auto& counts = policy.counts();
            require(counts.base_derivatives == 16 && counts.trial_derivatives == 16*static_cast<std::size_t>(m-1),
                    "Euler substep derivative count");
            require(counts.accepted_steps == 16 && counts.failed_steps == 0, "Euler substep accepted-step count");
        }
    }
}

// Independent 1x1/2x2 Padé computation; does not call the stage solver.
std::array<double, 2> analytical_gl(const Case& test) {
    const double h = test.h;
    if (test.model == Model::RC) {
        const double p = 1+h/2+h*h/12, n = 1-h/2+h*h/12;
        return {(n*test.initial_first+h*test.input)/p, 0};
    }
    const double a11 = 0, a12 = 1;
    const double a21 = test.model == Model::Oscillator ? -1 : -4;
    const double a22 = test.model == Model::Oscillator ? 0 : -test.resistance;
    const double aa11 = a11*a11+a12*a21, aa12 = a11*a12+a12*a22;
    const double aa21 = a21*a11+a22*a21, aa22 = a21*a12+a22*a22;
    const double p11 = 1-h*a11/2+h*h*aa11/12;
    const double p12 = -h*a12/2+h*h*aa12/12;
    const double p21 = -h*a21/2+h*h*aa21/12;
    const double p22 = 1-h*a22/2+h*h*aa22/12;
    const double n11 = 1+h*a11/2+h*h*aa11/12;
    const double n12 = h*a12/2+h*h*aa12/12;
    const double n21 = h*a21/2+h*h*aa21/12;
    const double n22 = 1+h*a22/2+h*h*aa22/12;
    const double b2 = test.model == Model::RLC ? test.input : 0;
    const double rhs1 = n11*test.initial_first+n12*test.initial_second;
    const double rhs2 = n21*test.initial_first+n22*test.initial_second+h*b2;
    const double determinant = p11*p22-p12*p21;
    return {(p22*rhs1-p12*rhs2)/determinant, (-p21*rhs1+p11*rhs2)/determinant};
}
void analytical_transition() {
    for (const Case test : {Case{Model::Oscillator, .4, 1, 0, 1, 0},
                            Case{Model::RC, 3., 1, 1, 0, 0},
                            Case{Model::RC, 3., 1, 0, 1, 0},
                            Case{Model::RLC, .2, 1, 1, 0, 0},
                            Case{Model::RLC, .2, 4, 1, 0, 0},
                            Case{Model::RLC, .2, 6, 1, 0, 0}}) {
        auto policy = policy_for(test, CoupledPolicy::Method::GaussLegendre2);
        policy.step();
        const auto expected = analytical_gl(test);
        near(policy.staged_of(test.model == Model::RLC ? "q" : "x"), expected[0], 2e-13,
             "Gauss-Legendre first-state matrix/input transition");
        if (test.model != Model::RC)
            near(policy.staged_of(test.model == Model::RLC ? "i" : "v"), expected[1], 2e-13,
                 "Gauss-Legendre second-state matrix/input transition");
        require(policy.counts().trial_derivatives == 2*policy.counts().stage_residual_evaluations,
                "Every GL stage residual must evaluate two derivative vectors");
        require(policy.counts().max_normalized_residual <= 1, "Accepted GL residual must meet tolerance");
    }
}
void fourth_order_and_energy() {
    double previous = 0;
    for (double h : {.2, .1, .05}) {
        const Case test{Model::Oscillator, h, 1, 0, 1, 0};
        auto policy = policy_for(test, CoupledPolicy::Method::GaussLegendre2);
        const int steps = static_cast<int>(std::llround(2/h));
        for (int j = 0; j < steps; ++j) policy.step();
        const double error = std::hypot(policy.staged_of("x")-std::cos(2.),
                                        policy.staged_of("v")+std::sin(2.));
        if (previous) require(previous/error > 14 && previous/error < 18, "GL fourth-order refinement");
        previous = error;
    }
    auto policy = policy_for(Case{Model::Oscillator, .1, 1, 0, 1, 0},
                             CoupledPolicy::Method::GaussLegendre2);
    double max_energy = 0;
    for (int j = 0; j < 1000; ++j) {
        policy.step();
        const double energy = std::pow(policy.staged_of("x"), 2)+std::pow(policy.staged_of("v"), 2);
        max_energy = std::max(max_energy, std::abs(energy-1));
    }
    require(max_energy < 1e-9, "Gauss-Legendre lossless oscillator energy");
    auto lc_circuit = policy_for(Case{Model::RLC, .1, 0, 0, .25, 0},
                                 CoupledPolicy::Method::GaussLegendre2);
    double max_lc_drift = 0;
    for (int j = 0; j < 200; ++j) {
        lc_circuit.step();
        const double q = lc_circuit.staged_of("q"), i = lc_circuit.staged_of("i");
        max_lc_drift = std::max(max_lc_drift, std::abs((4*q*q+i*i)/.25-1));
    }
    require(max_lc_drift < 1e-10, "Gauss-Legendre lossless RLC energy");
}
void responses_and_stability() {
    for (const double h : {.1, .2, .4}) {
        for (const Case test : {Case{Model::RC, h, 1, 1, 0, 0}, Case{Model::RC, h, 1, 0, 1, 0},
                                Case{Model::RLC, h, 1, 1, 0, 0}, Case{Model::RLC, h, 4, 1, 0, 0},
                                Case{Model::RLC, h, 6, 1, 0, 0}}) {
            auto policy = policy_for(test, CoupledPolicy::Method::GaussLegendre2);
            const int steps = static_cast<int>(std::llround(2/h));
            double max_error = 0;
            for (int j = 1; j <= steps; ++j) {
                policy.step();
                const auto expected = s7tests::research::exact_state(test, j*h);
                max_error = std::max(max_error, std::abs(policy.staged_of(test.model == Model::RLC ? "q" : "x")-expected[0]));
                if (test.model == Model::RLC)
                    max_error = std::max(max_error, std::abs(policy.staged_of("i")-expected[1]));
            }
            require(max_error < .02, "GL RC/RLC response at tested h");
        }
    }
    for (double h : {3., 30.}) {
        auto policy = policy_for(Case{Model::RC, h, 1, 0, 1, 0},
                                 CoupledPolicy::Method::GaussLegendre2);
        policy.step();
        require(std::abs(policy.staged_of("x")) < 1, "GL A-stable RC pole");
        if (h == 30.) require(policy.staged_of("x") > .5, "GL is not L-stable or accurate at huge RC step");
    }
    auto coarse = policy_for(Case{Model::RC, 3., 1, 0, 1, 0}, CoupledPolicy::Method::EulerSubsteps);
    auto split = policy_for(Case{Model::RC, 3., 1, 0, 1, 0}, CoupledPolicy::Method::EulerSubsteps, 2);
    coarse.step(); split.step();
    near(coarse.staged_of("x"), -2., 0, "Ordinary Euler RC pole beyond stability boundary");
    near(split.staged_of("x"), .25, 0, "Two Euler substeps restore RC stability");
}
void publication_and_order() {
    for (const Case test : {Case{Model::Oscillator, .2, 1, 0, 1, 0},
                            Case{Model::RLC, .2, 1, 1, 0, 0}}) {
        auto normal = policy_for(test, CoupledPolicy::Method::GaussLegendre2);
        auto reversed = policy_for(test, CoupledPolicy::Method::GaussLegendre2, 1, true);
        const char* first = test.model == Model::RLC ? "q" : "x";
        const char* second = test.model == Model::RLC ? "i" : "v";
        for (int j = 0; j < 30; ++j) {
            normal.step(); reversed.step();
            near(normal.published_of(first), reversed.published_of(first), 2e-13, "GL insertion-order first state");
            near(normal.published_of(second), reversed.published_of(second), 2e-13, "GL insertion-order second state");
            near(normal.staged_of(first), reversed.staged_of(first), 2e-13, "GL staged insertion-order first state");
            near(normal.staged_of(second), reversed.staged_of(second), 2e-13, "GL staged insertion-order second state");
            if (j == 0) near(normal.published_of(first), test.initial_first, 0, "First step publishes initial state");
        }
        auto duplicate = policy_for(test, CoupledPolicy::Method::GaussLegendre2);
        for (int j = 0; j < 30; ++j) duplicate.step();
        require(duplicate.published_of(first) == normal.published_of(first), "Deterministic repeated graph run");
        require(normal.temporary_bytes_upper_bound() > 0, "Stage storage accounting");
        require(normal.state_count() == 2, "GL stages must not add persistent states");
    }
}
class StatefulDerivative final : public s7::Element {
public:
    StatefulDerivative() : Element("StatefulDerivative", true) {
        register_input(0, "in", s7::SignalType::F64, nullptr);
        register_output(0, "out", s7::SignalType::F64, &value_);
    }
    void step() override { value_ += read_input<double>(0); }
private: double value_{};
};
void restrictions_and_rollback() {
    // Separate minimal mixed graph, which ordinary validation permits.
    s7::Schema unsupported("mixed_research_methods");
    unsupported.add_element("e", [] { return std::make_unique<s7::EulerIntegrator>(.1, 1.); });
    unsupported.add_element("t", [] { return std::make_unique<s7::TrapezoidIntegrator>(.1, 0.); });
    unsupported.connect("t", 0, "e", 0); unsupported.connect("e", 0, "t", 0);
    require(s7::Validator::validate(unsupported).ok(), "Mixed graph should pass ordinary causal validation");
    bool rejected = false;
    try { CoupledPolicy policy(unsupported, s7::Compiler::compile(unsupported), .1,
                               CoupledPolicy::Method::GaussLegendre2); }
    catch (const std::invalid_argument&) { rejected = true; }
    require(rejected, "Research policy must reject mixed integrator types");

    s7::Schema unsafe("stateful_research_rhs");
    unsafe.add_element("x", [] { return std::make_unique<s7::EulerIntegrator>(.1, 1.); });
    unsafe.add_element("history", [] { return std::make_unique<StatefulDerivative>(); });
    unsafe.connect("x", 0, "history", 0); unsafe.connect("history", 0, "x", 0);
    require(s7::Validator::validate(unsafe).ok(), "Ordinary Euler graph may contain stateful derivative");
    rejected = false;
    try { CoupledPolicy policy(unsafe, s7::Compiler::compile(unsafe), .1,
                               CoupledPolicy::Method::EulerSubsteps, 2); }
    catch (const std::invalid_argument&) { rejected = true; }
    require(rejected, "Research policy must reject stateful derivative");

    auto policy = policy_for(Case{Model::RC, .1, 1, 0, 1, 0}, CoupledPolicy::Method::GaussLegendre2);
    policy.step();
    near(policy.published_of("x"), 1, 0, "First accepted step publishes initial state");
    const double pending = policy.staged_of("x");
    policy.set_solver_limit(0);
    bool failed = false;
    try { policy.step(); } catch (const std::runtime_error&) { failed = true; }
    require(failed && policy.counts().accepted_steps == 1 && policy.counts().failed_steps == 1,
            "Solver limit must reject without advancing step count");
    near(policy.published_of("x"), 1, 0, "Rejected stage must restore publication");
    near(policy.staged_of("x"), pending, 0, "Rejected stage must retain pending state");
    policy.set_solver_limit(12);
    policy.step();
    near(policy.published_of("x"), pending, 0, "Retry must publish pending state once");

    auto bad = policy_for(Case{Model::RC, .1, 1, std::numeric_limits<double>::infinity(), 0, 0},
                          CoupledPolicy::Method::GaussLegendre2);
    failed = false;
    try { bad.step(); } catch (const std::runtime_error&) { failed = true; }
    require(failed && bad.published_of("x") == 0 && bad.counts().accepted_steps == 0,
            "Nonfinite derivative must not publish a candidate");
}
} // namespace

int main() try {
    euler_equivalence();
    analytical_transition();
    fourth_order_and_energy();
    responses_and_stability();
    publication_and_order();
    restrictions_and_rollback();
    std::cout << "Phase 3 real-graph Euler substeps and Gauss-Legendre checks passed\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
}
