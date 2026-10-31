// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <s7/core.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
enum class Model { Oscillator, RC, RLC };
enum class Method { Euler, EulerSubsteps, GaussLegendre, Trapezoid };
struct Case {
    Model model;
    double h;
    double resistance{1};
    double input{};
    double initial_first{};
    double initial_second{};
};

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}
void near(double actual, double expected, double tolerance, const std::string& message) {
    if (!std::isfinite(actual) || std::abs(actual-expected) > tolerance)
        throw std::runtime_error(message + ": got " + std::to_string(actual) +
                                 ", expected " + std::to_string(expected));
}

s7::Schema graph(const Case& test, Method method, int substeps = 1, bool reverse = false) {
    s7::Schema schema("coupled_integration_smoke");
    const auto state = [&](const std::string& id, double initial) {
        schema.add_element(id, [=]() -> std::unique_ptr<s7::Element> {
            if (method == Method::GaussLegendre)
                return std::make_unique<s7::GaussLegendreIntegrator>(test.h, initial);
            if (method == Method::Trapezoid)
                return std::make_unique<s7::TrapezoidIntegrator>(test.h, initial);
            return std::make_unique<s7::EulerIntegrator>(test.h, initial,
                method == Method::EulerSubsteps ? substeps : 1);
        });
    };
    if (test.model == Model::RC) {
        state("x", test.initial_first);
        schema.add_element("input", [=] { return std::make_unique<s7::Constant>(test.input); });
        schema.add_element("rhs", [] { return std::make_unique<s7::Adder>(std::vector<double>{1, -1}); });
        schema.connect("input", 0, "rhs", 0);
        schema.connect("x", 0, "rhs", 1);
        schema.connect("rhs", 0, "x", 0);
    } else if (test.model == Model::Oscillator) {
        if (reverse) { state("v", test.initial_second); state("x", test.initial_first); }
        else { state("x", test.initial_first); state("v", test.initial_second); }
        schema.add_element("minus", [] { return std::make_unique<s7::Gain>(-1); });
        schema.connect("v", 0, "x", 0);
        schema.connect("x", 0, "minus", 0);
        schema.connect("minus", 0, "v", 0);
    } else {
        if (reverse) { state("i", test.initial_second); state("q", test.initial_first); }
        else { state("q", test.initial_first); state("i", test.initial_second); }
        schema.add_element("input", [=] { return std::make_unique<s7::Constant>(test.input); });
        schema.add_element("rhs", [=] {
            return std::make_unique<s7::Adder>(std::vector<double>{1, -test.resistance, -4});
        });
        schema.connect("i", 0, "q", 0);
        schema.connect("input", 0, "rhs", 0);
        schema.connect("i", 0, "rhs", 1);
        schema.connect("q", 0, "rhs", 2);
        schema.connect("rhs", 0, "i", 0);
    }
    return schema;
}

std::array<double, 2> state_of(const s7::NodeRuntime& runtime, Model model) {
    if (model == Model::RC) return {runtime.real_output_of("x"), 0};
    if (model == Model::Oscillator) return {runtime.real_output_of("x"), runtime.real_output_of("v")};
    return {runtime.real_output_of("q"), runtime.real_output_of("i")};
}

std::array<double, 2> exact(const Case& test, double t) {
    if (test.model == Model::Oscillator)
        return {test.initial_first*std::cos(t)+test.initial_second*std::sin(t),
                test.initial_second*std::cos(t)-test.initial_first*std::sin(t)};
    if (test.model == Model::RC)
        return {test.input+(test.initial_first-test.input)*std::exp(-t), 0};
    if (test.resistance == 0 && test.input == 0)
        return {test.initial_first*std::cos(2*t)+test.initial_second*std::sin(2*t)/2,
                test.initial_second*std::cos(2*t)-2*test.initial_first*std::sin(2*t)};
    const double alpha = test.resistance/2, discriminant = alpha*alpha-4;
    if (std::abs(discriminant) < 1e-14)
        return {.25*test.input*(1-(1+alpha*t)*std::exp(-alpha*t)),
                test.input*t*std::exp(-alpha*t)};
    if (discriminant < 0) {
        const double wd = std::sqrt(-discriminant), decay = std::exp(-alpha*t);
        return {.25*test.input*(1-decay*(std::cos(wd*t)+alpha*std::sin(wd*t)/wd)),
                test.input*decay*std::sin(wd*t)/wd};
    }
    const double root = std::sqrt(discriminant), p = -alpha+root, m = -alpha-root;
    return {.25*test.input*(1+(m*std::exp(p*t)-p*std::exp(m*t))/(p-m)),
            test.input*(std::exp(p*t)-std::exp(m*t))/(p-m)};
}

// Independent 1x1/2x2 Padé matrix calculation, including the held input B.
std::array<double, 2> pade_step(const Case& test) {
    const double h = test.h;
    if (test.model == Model::RC) {
        const double p = 1+h/2+h*h/12, n = 1-h/2+h*h/12;
        return {(n*test.initial_first+h*test.input)/p, 0};
    }
    const double a21 = test.model == Model::Oscillator ? -1 : -4;
    const double a22 = test.model == Model::Oscillator ? 0 : -test.resistance;
    const double aa11 = a21, aa12 = a22, aa21 = a22*a21, aa22 = a21+a22*a22;
    const double p11 = 1+h*h*aa11/12, p12 = -h/2+h*h*aa12/12;
    const double p21 = -h*a21/2+h*h*aa21/12, p22 = 1-h*a22/2+h*h*aa22/12;
    const double n11 = 1+h*h*aa11/12, n12 = h/2+h*h*aa12/12;
    const double n21 = h*a21/2+h*h*aa21/12, n22 = 1+h*a22/2+h*h*aa22/12;
    const double rhs1 = n11*test.initial_first+n12*test.initial_second;
    const double rhs2 = n21*test.initial_first+n22*test.initial_second+
        (test.model == Model::RLC ? h*test.input : 0);
    const double det = p11*p22-p12*p21;
    return {(p22*rhs1-p12*rhs2)/det, (-p21*rhs1+p11*rhs2)/det};
}

void substep_equivalence() {
    for (const Case test : {Case{Model::Oscillator, .2, 1, 0, 1, 0},
                            Case{Model::RC, .2, 1, 1, 0, 0},
                            Case{Model::RLC, .2, 1, 1, 0, 0},
                            Case{Model::RLC, .2, 6, 1, 0, 0}}) {
        for (int m : {1, 2, 4, 8, 16, 32}) {
            auto coarse_schema = graph(test, Method::EulerSubsteps, m);
            require(s7::Validator::validate(coarse_schema).ok(), "Coupled Euler schema validation");
            s7::NodeRuntime coarse(s7::Compiler::compile(coarse_schema));
            auto fine_case = test; fine_case.h /= m;
            s7::NodeRuntime fine(s7::Compiler::compile(graph(fine_case, Method::Euler)));
            coarse.step(); fine.step();
            for (int outer = 0; outer < 16; ++outer) {
                coarse.step();
                for (int j = 0; j < m; ++j) fine.step();
                const auto a = state_of(coarse, test.model), b = state_of(fine, test.model);
                near(a[0], b[0], 3e-13, "Euler substep/fine first state");
                near(a[1], b[1], 3e-13, "Euler substep/fine second state");
            }
            if (m > 1) {
                const auto counters = coarse.numerical_diagnostics();
                require(counters.accepted_steps == 17 && counters.base_derivative_vectors == 17 &&
                        counters.trial_derivative_vectors == 17*static_cast<std::size_t>(m-1),
                        "Euler derivative-vector counts");
            }
        }
    }
}

void gl_transition_and_convergence() {
    for (const Case test : {Case{Model::Oscillator, .4, 1, 0, 1, 0},
                            Case{Model::RC, 3, 1, 1, 0, 0},
                            Case{Model::RC, 3, 1, 0, 1, 0},
                            Case{Model::RLC, .2, 1, 1, 0, 0},
                            Case{Model::RLC, .2, 4, 1, 0, 0},
                            Case{Model::RLC, .2, 6, 1, 0, 0}}) {
        s7::NodeRuntime runtime(s7::Compiler::compile(graph(test, Method::GaussLegendre)));
        runtime.step();
        near(state_of(runtime, test.model)[0], test.initial_first, 0, "First publication is initial state");
        runtime.step();
        const auto expected = pade_step(test), actual = state_of(runtime, test.model);
        near(actual[0], expected[0], 3e-13, "GL Padé first state and input operator");
        near(actual[1], expected[1], 3e-13, "GL Padé second state and input operator");
        const auto count = runtime.numerical_diagnostics();
        require(count.stage_residual_evaluations*2 == count.trial_derivative_vectors &&
                count.last_normalized_residual <= 1, "GL solver work/residual accounting");
    }
    double previous = 0;
    for (double h : {.2, .1, .05}) {
        const Case test{Model::Oscillator, h, 1, 0, 1, 0};
        s7::NodeRuntime runtime(s7::Compiler::compile(graph(test, Method::GaussLegendre)));
        const auto steps = static_cast<int>(std::llround(2/h));
        for (int j = 0; j <= steps; ++j) runtime.step();
        const auto state = state_of(runtime, Model::Oscillator);
        const double error = std::hypot(state[0]-std::cos(2.), state[1]+std::sin(2.));
        if (previous) require(previous/error > 14 && previous/error < 18, "Fourth-order GL refinement");
        previous = error;
    }
}

void energy_phase_and_order() {
    const Case oscillator{Model::Oscillator, .1, 1, 0, 1, 0};
    s7::NodeRuntime a(s7::Compiler::compile(graph(oscillator, Method::GaussLegendre)));
    s7::NodeRuntime b(s7::Compiler::compile(graph(oscillator, Method::GaussLegendre, 1, true)));
    double maximum_energy = 0;
    for (int j = 0; j <= 1000; ++j) {
        a.step(); b.step();
        const auto x = state_of(a, Model::Oscillator), y = state_of(b, Model::Oscillator);
        near(x[0], y[0], 2e-13, "GL insertion-order position");
        near(x[1], y[1], 2e-13, "GL insertion-order velocity");
        near(a.real_output_of("minus"), -x[0], 2e-13,
             "Trial derivative outputs must restore to the published state");
        maximum_energy = std::max(maximum_energy, std::abs(x[0]*x[0]+x[1]*x[1]-1));
    }
    require(maximum_energy < 1e-9, "GL oscillator quadratic energy");
    const auto x = state_of(a, Model::Oscillator);
    const double phase_error = std::remainder(std::atan2(-x[1], x[0])-100., 2*std::acos(-1.));
    require(std::abs(phase_error) < 2e-5, "GL long-run oscillator phase");
    const Case lossless{Model::RLC, .1, 0, 0, .25, 0};
    s7::NodeRuntime lc(s7::Compiler::compile(graph(lossless, Method::GaussLegendre)));
    double maximum_lc_energy = 0;
    for (int j = 0; j <= 200; ++j) {
        lc.step();
        const auto state = state_of(lc, Model::RLC);
        maximum_lc_energy = std::max(maximum_lc_energy,
            std::abs((4*state[0]*state[0]+state[1]*state[1])/.25-1));
    }
    require(maximum_lc_energy < 1e-10, "GL lossless RLC energy");
}

void responses() {
    for (const double h : {.1, .2, .4}) {
        for (const Case test : {Case{Model::RC, h, 1, 1, 0, 0},
                                Case{Model::RC, h, 1, 0, 1, 0},
                                Case{Model::RLC, h, 1, 1, 0, 0},
                                Case{Model::RLC, h, 4, 1, 0, 0},
                                Case{Model::RLC, h, 6, 1, 0, 0}}) {
            s7::NodeRuntime runtime(s7::Compiler::compile(graph(test, Method::GaussLegendre)));
            runtime.step();
            const int steps = static_cast<int>(std::llround(2/h));
            double maximum_error = 0, overshoot = 0;
            for (int j = 1; j <= steps; ++j) {
                runtime.step();
                const auto expected = exact(test, j*h), actual = state_of(runtime, test.model);
                maximum_error = std::max({maximum_error, std::abs(actual[0]-expected[0]),
                                          std::abs(actual[1]-expected[1])});
                if (test.model == Model::RLC) overshoot = std::max(overshoot, 4*actual[0]-1);
            }
            require(maximum_error < .02, "GL RC/RLC closed-form response");
            if (test.model == Model::RLC && test.resistance >= 4)
                require(overshoot < .01, "Critical/overdamped RLC must not ring materially");
        }
    }
    const Case coarse{Model::RC, 3, 1, 0, 1, 0};
    s7::NodeRuntime gl(s7::Compiler::compile(graph(coarse, Method::GaussLegendre)));
    gl.step(); gl.step();
    near(state_of(gl, Model::RC)[0], 1./13., 2e-13, "GL coarse RC pole");
    s7::NodeRuntime euler(s7::Compiler::compile(graph(coarse, Method::Euler)));
    euler.step(); euler.step();
    near(state_of(euler, Model::RC)[0], -2., 0, "Euler coarse RC instability");
    const Case very_coarse{Model::RC, 30, 1, 0, 1, 0};
    s7::NodeRuntime stable(s7::Compiler::compile(graph(very_coarse, Method::GaussLegendre)));
    stable.step(); stable.step();
    require(state_of(stable, Model::RC)[0] > .5 && state_of(stable, Model::RC)[0] < 1,
            "GL A-stability without L-stability or coarse accuracy");
}

class StatefulRhs final : public s7::Element {
public:
    StatefulRhs() : Element("StatefulRhs", true) {
        register_input(0, "in", s7::SignalType::F64, nullptr);
        register_output(0, "out", s7::SignalType::F64, &value_);
    }
    void step() override { value_ += read_input<double>(0); }
private: double value_{};
};

class UnknownPureRhs final : public s7::Element {
public:
    UnknownPureRhs() : Element("UnknownPureRhs") {
        register_input(0, "in", s7::SignalType::F64, nullptr);
        register_output(0, "out", s7::SignalType::F64, &value_);
    }
    void step() override { value_ = read_input<double>(0); }
private: double value_{};
};

class ChangingSource final : public s7::Element {
public:
    explicit ChangingSource(const double* input) : Element("ChangingSource"), input_(input) {
        register_output(0, "out", s7::SignalType::F64, &value_);
    }
    void step() override { value_ = *input_; }
private:
    const double* input_;
    double value_{};
};

void restrictions_and_failure() {
    s7::Schema mixed("mixed_numerical_feedback");
    mixed.add_element("gl", [] { return std::make_unique<s7::GaussLegendreIntegrator>(.1, 1.); });
    mixed.add_element("euler", [] { return std::make_unique<s7::EulerIntegrator>(.1, 0.); });
    mixed.connect("gl", 0, "euler", 0);
    mixed.connect("euler", 0, "gl", 0);
    const auto mixed_result = s7::Validator::validate(mixed);
    require(!mixed_result.ok() && mixed_result.errors.front().find("crosses method") != std::string::npos,
            "Mixed coupled methods must fail validation");

    s7::Schema unsafe("stateful_trial");
    unsafe.add_element("x", [] { return std::make_unique<s7::GaussLegendreIntegrator>(.1, 1.); });
    unsafe.add_element("rhs", [] { return std::make_unique<StatefulRhs>(); });
    unsafe.connect("x", 0, "rhs", 0); unsafe.connect("rhs", 0, "x", 0);
    require(!s7::Validator::validate(unsafe).ok(), "Stateful trial operation must be rejected");

    s7::Schema unknown("unknown_pure_trial");
    unknown.add_element("x", [] { return std::make_unique<s7::GaussLegendreIntegrator>(.1, 1.); });
    unknown.add_element("rhs", [] { return std::make_unique<UnknownPureRhs>(); });
    unknown.connect("x", 0, "rhs", 0); unknown.connect("rhs", 0, "x", 0);
    require(!s7::Validator::validate(unknown).ok(), "Unknown plugin-like trial operation must be rejected");

    s7::Schema mixed_trap("mixed_gl_trapezoid_feedback");
    mixed_trap.add_element("gl", [] { return std::make_unique<s7::GaussLegendreIntegrator>(.1, 1.); });
    mixed_trap.add_element("trap", [] { return std::make_unique<s7::TrapezoidIntegrator>(.1, 0.); });
    mixed_trap.connect("gl", 0, "trap", 0); mixed_trap.connect("trap", 0, "gl", 0);
    require(!s7::Validator::validate(mixed_trap).ok(), "Mixed GL/trapezoid feedback must be rejected");

    s7::Schema independent("independent_mixed_regions");
    independent.add_element("gl", [] { return std::make_unique<s7::GaussLegendreIntegrator>(.1, 1.); });
    independent.add_element("gl_rhs", [] { return std::make_unique<s7::Gain>(-1.); });
    independent.add_element("sub", [] { return std::make_unique<s7::EulerIntegrator>(.1, 1., 4); });
    independent.add_element("sub_rhs", [] { return std::make_unique<s7::Gain>(-1.); });
    independent.connect("gl", 0, "gl_rhs", 0); independent.connect("gl_rhs", 0, "gl", 0);
    independent.connect("sub", 0, "sub_rhs", 0); independent.connect("sub_rhs", 0, "sub", 0);
    require(s7::Validator::validate(independent).ok(), "Disconnected policies may coexist");
    s7::NodeRuntime two_regions(s7::Compiler::compile(independent));
    two_regions.step(); two_regions.step();
    near(two_regions.real_output_of("gl"), pade_step(Case{Model::RC, .1, 1, 0, 1, 0})[0],
         2e-13, "Independent GL region");
    near(two_regions.real_output_of("sub"), std::pow(1-.1/4, 4), 2e-13,
         "Independent Euler substep region");
    require(two_regions.numerical_diagnostics().accepted_steps == 2,
            "Accepted-step count is physical steps, not summed region solves");

    bool invalid = false;
    try { s7::EulerIntegratorT<s7::SignalType::F32> unsupported(.1f, 0.f, 2); }
    catch (const std::invalid_argument&) { invalid = true; }
    require(invalid, "Non-F64 substeps must fail");
    invalid = false;
    try { s7::EulerIntegrator unsupported(.1, 0., 0); }
    catch (const std::invalid_argument&) { invalid = true; }
    require(invalid, "Invalid substep count must fail");

    s7::NodeRuntime nonfinite(s7::Compiler::compile(graph(Case{Model::RC, .1, 1,
        std::numeric_limits<double>::infinity(), 0, 0}, Method::GaussLegendre)));
    bool failed = false;
    try { nonfinite.step(); } catch (const std::runtime_error&) { failed = true; }
    require(failed && nonfinite.step_count() == 0 && state_of(nonfinite, Model::RC)[0] == 0,
            "Nonfinite derivative must roll back publication");

    s7::Schema divergent("nonconvergent_gl");
    divergent.add_element("x", [] { return std::make_unique<s7::GaussLegendreIntegrator>(1., 1.); });
    divergent.add_element("square", [] { return std::make_unique<s7::Multiply>(); });
    divergent.add_element("gain", [] { return std::make_unique<s7::Gain>(1000.); });
    divergent.connect("x", 0, "square", 0); divergent.connect("x", 0, "square", 1);
    divergent.connect("square", 0, "gain", 0); divergent.connect("gain", 0, "x", 0);
    s7::NodeRuntime unstable(s7::Compiler::compile(divergent));
    failed = false;
    try { unstable.step(); } catch (const std::runtime_error&) { failed = true; }
    require(failed && unstable.step_count() == 0 && state_of(unstable, Model::RC)[0] == 1,
            "Failed nonlinear solve must leave state unpublished");

    double source_value = 0;
    s7::Schema retry_schema("retry_after_nonfinite_input");
    retry_schema.add_element("x", [] { return std::make_unique<s7::GaussLegendreIntegrator>(.1, 1.); });
    retry_schema.add_element("input", [&] { return std::make_unique<ChangingSource>(&source_value); });
    retry_schema.add_element("rhs", [] { return std::make_unique<s7::Adder>(std::vector<double>{1, -1}); });
    retry_schema.connect("input", 0, "rhs", 0); retry_schema.connect("x", 0, "rhs", 1);
    retry_schema.connect("rhs", 0, "x", 0);
    s7::NodeRuntime retry(s7::Compiler::compile(retry_schema));
    retry.step();
    source_value = std::numeric_limits<double>::infinity();
    failed = false;
    try { retry.step(); } catch (const std::runtime_error&) { failed = true; }
    require(failed && retry.step_count() == 1 && state_of(retry, Model::RC)[0] == 1,
            "Failed step must keep prior publication and pending endpoint");
    source_value = 0;
    retry.step();
    near(state_of(retry, Model::RC)[0], pade_step(Case{Model::RC, .1, 1, 0, 1, 0})[0], 2e-13,
         "Retry must publish the previously accepted endpoint exactly once");
}

void registry_configuration() {
    auto& registry = s7::ElementRegistry::instance();
    s7::register_builtin_elements(registry);
    const auto* gl = registry.find("GaussLegendreIntegrator");
    const auto* euler = registry.find("EulerIntegrator");
    require(gl && euler, "Public integration metadata must be registered");
    s7::Schema schema("serialized_parameter_path");
    schema.set_rate(s7::Rate::from_hz(10));
    schema.add_element(s7::make_element_spec("x", *euler,
        {{"type", "F64"}, {"dt", .9}, {"initial_value", 1.}, {"substeps", 4}}));
    schema.add_element(s7::make_element_spec("gain", *registry.find("Gain"),
        {{"type", "F64"}, {"factor", -1.}}));
    schema.connect("x", 0, "gain", 0); schema.connect("gain", 0, "x", 0);
    s7::NodeRuntime runtime(s7::Compiler::compile(schema));
    runtime.step(); runtime.step();
    near(runtime.real_output_of("x"), std::pow(1-.1/4, 4), 2e-14,
         "Serialized Euler substeps must follow the domain rate, not legacy dt");
    s7::Schema gl_schema("serialized_gauss_legendre");
    gl_schema.set_rate(s7::Rate::from_hz(10));
    gl_schema.add_element(s7::make_element_spec("x", *gl, {{"dt", .9}, {"initial_value", 1.}}));
    gl_schema.add_element("gain", [] { return std::make_unique<s7::Gain>(-1.); });
    gl_schema.connect("x", 0, "gain", 0); gl_schema.connect("gain", 0, "x", 0);
    s7::NodeRuntime gl_runtime(s7::Compiler::compile(gl_schema));
    gl_runtime.step(); gl_runtime.step();
    near(gl_runtime.real_output_of("x"), pade_step(Case{Model::RC, .1, 1, 0, 1, 0})[0], 2e-13,
         "Serialized Gauss-Legendre element must follow the domain rate, not legacy dt");
}

void region_boundaries() {
    s7::Schema disconnected("two_disconnected_gl_loops");
    for (const std::string id : {"a", "b"}) {
        disconnected.add_element(id, [] { return std::make_unique<s7::GaussLegendreIntegrator>(.1, 1.); });
        disconnected.add_element(id+"_rhs", [] { return std::make_unique<s7::Gain>(-1.); });
        disconnected.connect(id, 0, id+"_rhs", 0);
        disconnected.connect(id+"_rhs", 0, id, 0);
    }
    auto graph = s7::Compiler::compile(disconnected);
    require(graph.coupled_regions.size() == 2, "Disconnected states need separate Newton regions");
    s7::NodeRuntime runtime(std::move(graph));
    runtime.step(); runtime.step();
    near(runtime.real_output_of("a"), runtime.real_output_of("b"), 0,
         "Independent regions have identical transitions");

    s7::Schema one_way("one_way_coupled_gl");
    one_way.add_element("a", [] { return std::make_unique<s7::GaussLegendreIntegrator>(.1, 1.); });
    one_way.add_element("b", [] { return std::make_unique<s7::GaussLegendreIntegrator>(.1, 0.); });
    one_way.add_element("negative", [] { return std::make_unique<s7::Gain>(-1.); });
    one_way.connect("a", 0, "negative", 0); one_way.connect("negative", 0, "a", 0);
    one_way.connect("a", 0, "b", 0);
    require(s7::Compiler::compile(one_way).coupled_regions.size() == 1,
            "One-way coupled states share their Runge-Kutta stages");
}
} // namespace

int main() try {
    substep_equivalence();
    gl_transition_and_convergence();
    energy_phase_and_order();
    responses();
    restrictions_and_failure();
    registry_configuration();
    region_boundaries();
    std::cout << "Coupled integration production checks passed\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
}
