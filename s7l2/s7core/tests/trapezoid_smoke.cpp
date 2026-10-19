#include <s7/core.hpp>

#include <algorithm>
#include <cmath>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

namespace {
void require(bool ok, const std::string& message) {
    if (!ok) throw std::runtime_error(message);
}

s7::Schema decay(double dt, double factor = -1.0) {
    s7::Schema schema("trapezoid_decay");
    schema.add_element("x", [=] { return std::make_unique<s7::TrapezoidIntegrator>(dt, 1.0); });
    schema.add_element("f", [=] { return std::make_unique<s7::Gain>(factor); });
    schema.connect("x", 0, "f", 0);
    schema.connect("f", 0, "x", 0);
    return schema;
}

void scalar_convergence() {
    double previous = 0;
    for (const double dt : {0.1, 0.05, 0.025}) {
        auto schema = decay(dt);
        require(s7::Validator::validate(schema).ok(), "Implicit scalar feedback must validate without Delay");
        s7::NodeRuntime runtime(s7::Compiler::compile(schema));
        const int n = static_cast<int>(std::llround(2 / dt));
        for (int i = 0; i <= n; ++i) runtime.step();
        const double value = runtime.real_output_of("x");
        const double discrete = std::pow((1-dt/2)/(1+dt/2), n);
        require(std::abs(value-discrete) < 2e-14, "Scalar implicit trapezoid amplification factor");
        const double error = std::abs(value-std::exp(-2.0));
        if (previous) require(previous/error > 3.8 && previous/error < 4.2, "Scalar second-order convergence");
        previous = error;
    }
}

template <s7::SignalType Type> void typed_step() {
    using V = s7::signal_storage_t<Type>;
    using R = s7::component_storage_t<Type>;
    const R h = R(0.125);
    const V initial = [] {
        if constexpr (s7::is_complex_signal_type(Type)) return V(R(1), R(0.5));
        else return V(1);
    }();
    s7::Schema schema("typed_trapezoid");
    schema.add_element("x", [=] { return std::make_unique<s7::TrapezoidIntegratorT<Type>>(h, initial); });
    schema.add_element("f", [] { return std::make_unique<s7::GainT<Type>>(V(-1)); });
    schema.connect("x", 0, "f", 0);
    schema.connect("f", 0, "x", 0);
    s7::NodeRuntime runtime(s7::Compiler::compile(schema));
    runtime.step();
    require(runtime.output_of<Type>("x") == initial, "Initial state must remain published for first step");
    runtime.step();
    const V expected = initial * ((R(1)-h/R(2))/(R(1)+h/R(2)));
    constexpr auto component = s7::component_signal_type(Type);
    constexpr int digits = component == s7::SignalType::F16 ? 11 : component == s7::SignalType::F32 ? 24 :
                           component == s7::SignalType::F64 ? 53 : 113;
    require(s7::math::abs(runtime.output_of<Type>("x")-expected) < s7::math::ldexp(R(1), 6-digits),
            "Typed implicit trapezoid step");
}

void oscillator(bool reverse) {
    s7::Schema schema("coupled_trapezoid");
    const auto x = [] { return std::make_unique<s7::TrapezoidIntegrator>(0.01, 1.0); };
    const auto v = [] { return std::make_unique<s7::TrapezoidIntegrator>(0.01, 0.0); };
    if (reverse) { schema.add_element("v", v); schema.add_element("x", x); }
    else { schema.add_element("x", x); schema.add_element("v", v); }
    schema.add_element("minus", [] { return std::make_unique<s7::Gain>(-1); });
    schema.connect("v", 0, "x", 0);
    schema.connect("x", 0, "minus", 0);
    schema.connect("minus", 0, "v", 0);
    s7::NodeRuntime runtime(s7::Compiler::compile(schema));
    double max_energy = 0;
    for (int n = 0; n <= 20000; ++n) {
        runtime.step();
        const double q = runtime.real_output_of("x"), velocity = runtime.real_output_of("v");
        const double phase = n * 2 * std::atan(0.005);
        require(std::abs(q-std::cos(phase)) < 2e-10 && std::abs(velocity+std::sin(phase)) < 2e-10,
                "Coupled trapezoid discrete phase/order independence");
        max_energy = std::max(max_energy, std::abs(q*q+velocity*velocity-1));
    }
    require(max_energy < 1e-9, "Conservative oscillator energy must not drift");
}

void nonlinear() {
    // x'=-x^2, x(0)=1: independent continuous solution 1/(1+t),
    // independent implicit root at each step from the quadratic equation.
    double previous = 0;
    for (const double dt : {0.1, 0.05, 0.025}) {
        s7::Schema schema("nonlinear_trapezoid");
        schema.add_element("x", [=] { return std::make_unique<s7::TrapezoidIntegrator>(dt, 1.0); });
        schema.add_element("square", [] { return std::make_unique<s7::Multiply>(); });
        schema.add_element("minus", [] { return std::make_unique<s7::Gain>(-1); });
        schema.connect("x", 0, "square", 0); schema.connect("x", 0, "square", 1);
        schema.connect("square", 0, "minus", 0); schema.connect("minus", 0, "x", 0);
        s7::NodeRuntime runtime(s7::Compiler::compile(schema));
        long double expected = 1;
        const int count = static_cast<int>(std::llround(2 / dt));
        for (int n = 0; n <= count; ++n) {
            runtime.step();
            if (n) {
                const long double h = dt;
                const long double rhs = expected - h*expected*expected/2;
                expected = 2*rhs/(1+std::sqrt(1+2*h*rhs)); // stable positive root
            }
            require(std::abs(runtime.real_output_of("x")-expected) < 2e-13, "Nonlinear implicit trapezoid root");
        }
        const double error = std::abs(runtime.real_output_of("x")-1.0/3);
        if (previous) require(previous/error > 3.8 && previous/error < 4.2, "Nonlinear second-order convergence");
        previous = error;
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

void validation() {
    s7::Schema schema("algebraic_subcycle");
    schema.add_element("x", [] { return std::make_unique<s7::TrapezoidIntegrator>(0.1, 1.0); });
    // A direct cycle inside a larger SCC containing causal memory is still
    // invalid; never recurse into it or make numerical stages legalize it.
    schema.add_element("sum", [] { return std::make_unique<s7::Adder>(std::vector<double>{1,1}); });
    schema.connect("sum", 0, "sum", 0); schema.connect("x", 0, "sum", 1);
    schema.connect("sum", 0, "x", 0);
    require(!s7::Validator::validate(schema).ok(), "Direct algebraic cycle must remain invalid");

    s7::Schema unsafe("stateful_feedback");
    unsafe.add_element("x", [] { return std::make_unique<s7::TrapezoidIntegrator>(0.1, 1.0); });
    unsafe.add_element("history", [] { return std::make_unique<StatefulDerivative>(); });
    unsafe.connect("x", 0, "history", 0); unsafe.connect("history", 0, "x", 0);
    const auto result = s7::Validator::validate(unsafe);
    require(!result.ok() && std::any_of(result.errors.begin(), result.errors.end(), [](const auto& error) {
        return error.find("stateful") != std::string::npos && error.find("history") != std::string::npos;
    }), "Stateful feedback must be rejected with element context");
}

class TickSource final : public s7::Element {
public:
    TickSource() : Element("TickSource", true) {
        register_output(0, "out", s7::SignalType::F64, &value_);
    }
    void step() override { value_ += 1; }
private: double value_{};
};

void held_source() {
    s7::Schema schema("held_source");
    schema.add_element("source", [] { return std::make_unique<TickSource>(); });
    schema.add_element("area", [] { return std::make_unique<s7::TrapezoidIntegrator>(0.1); });
    schema.connect("source", 0, "area", 0);
    s7::NodeRuntime runtime(s7::Compiler::compile(schema));
    for (int step = 1; step <= 20; ++step) {
        runtime.step();
        require(runtime.real_output_of("source") == step, "Stateful source must advance only once per runtime step");
        require(std::abs(runtime.real_output_of("area") - 0.1*step*(step-1)/2) < 1e-12,
                "Independent input is held throughout an implicit interval");
    }
}

class TrialDerivative final : public s7::Element {
public:
    explicit TrialDerivative(const bool* fail) : Element("TrialDerivative"), fail_(fail) {
        register_input(0, "in", s7::SignalType::F64, nullptr);
        register_output(0, "out", s7::SignalType::F64, &value_);
    }
    void step() override {
        const double x = read_input<double>(0);
        if (*fail_ && x > 1.11) throw std::runtime_error("trial failure");
        value_ = x;
    }
private: const bool* fail_; double value_{};
};

void rollback() {
    bool fail = false;
    s7::Schema schema("trial_rollback");
    schema.add_element("x", [] { return std::make_unique<s7::TrapezoidIntegrator>(0.1, 1.0); });
    schema.add_element("f", [&] { return std::make_unique<TrialDerivative>(&fail); });
    schema.connect("x", 0, "f", 0); schema.connect("f", 0, "x", 0);
    s7::NodeRuntime runtime(s7::Compiler::compile(schema));
    runtime.step();
    fail = true;
    bool threw = false;
    try { runtime.step(); } catch (const std::runtime_error&) { threw = true; }
    require(threw && runtime.step_count() == 1 && runtime.real_output_of("x") == 1 && runtime.real_output_of("f") == 1,
            "Failed trial must restore published values and step count");
    fail = false;
    runtime.step();
    require(std::abs(runtime.real_output_of("x") - 1.05/0.95) < 1e-14, "Retry must preserve staged state");
    runtime.step();
    require(std::abs(runtime.real_output_of("x") - std::pow(1.05/0.95, 2)) < 1e-14, "Retry must not advance twice");

    s7::NodeRuntime divergent(s7::Compiler::compile(decay(1.0, -4)));
    for (int attempt = 0; attempt < 2; ++attempt) {
        threw = false;
        try { divergent.step(); } catch (const std::runtime_error& error) {
            threw = std::string(error.what()).find("did not converge") != std::string::npos;
        }
        require(threw && divergent.step_count() == 0 && divergent.real_output_of("x") == 1,
                "Nonconvergence must fail explicitly without changing published state");
    }
}
} // namespace

int main() try {
    scalar_convergence();
    typed_step<s7::SignalType::F16>(); typed_step<s7::SignalType::F32>();
    typed_step<s7::SignalType::F64>(); typed_step<s7::SignalType::F128>();
    typed_step<s7::SignalType::C16>(); typed_step<s7::SignalType::C32>();
    typed_step<s7::SignalType::C64>(); typed_step<s7::SignalType::C128>();
    oscillator(false); oscillator(true); nonlinear(); validation(); held_source(); rollback();
    std::cout << "Implicit trapezoid: typed states, second-order scalar/nonlinear convergence, coupled energy, validation and rollback passed\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n'; return 1;
}
