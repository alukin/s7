// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: LGPL-3.0-or-later

// A step that faults in any phase publishes nothing: direct outputs, published
// causal-memory outputs and staged values return to their pre-step state, the
// step count does not advance, and the next successful step continues exactly
// as a run without the fault.

#include <s7/core.hpp>
#include <s7/resampler.hpp>

#include <algorithm>
#include <bit>
#include <cstdint>
#include <limits>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

// Causal memory (like Delay) that throws while *fail is set, after writing
// its next output.
class FaultingDelay final : public s7::Element {
public:
    explicit FaultingDelay(const bool* fail)
        : Element("FaultingDelay", true, s7::Feedthrough::None), fail_(fail) {
        register_input(0, "in", s7::SignalType::F64, nullptr);
        register_output(0, "out", s7::SignalType::F64, &value_);
    }
    void step() override {
        value_ = read_input<double>(0);
        if (fail_ && *fail_) throw std::runtime_error("injected memory fault");
    }

private:
    const bool* fail_;
    double value_{-1.0};
};

// Direct element that throws while *fail is set.
class FaultingGain final : public s7::Element {
public:
    explicit FaultingGain(const bool* fail) : Element("FaultingGain"), fail_(fail) {
        register_input(0, "in", s7::SignalType::F64, nullptr);
        register_output(0, "out", s7::SignalType::F64, &value_);
    }
    void step() override {
        value_ = 2.0 * read_input<double>(0);
        if (fail_ && *fail_) throw std::runtime_error("injected direct fault");
    }

private:
    const bool* fail_;
    double value_{};
};

// Stateless direct source: 0, or NaN while *fail is set. It never throws
// itself; the numerical stage that reads it as a held input faults, so the
// rollback must also undo this already-published direct output.
class FaultingForcing final : public s7::Element {
public:
    explicit FaultingForcing(const bool* fail) : Element("FaultingForcing"), fail_(fail) {
        register_output(0, "out", s7::SignalType::F64, &value_);
    }
    void step() override {
        value_ = fail_ && *fail_ ? std::numeric_limits<double>::quiet_NaN() : 0.0;
    }

private:
    const bool* fail_;
    double value_{};
};

// Direct feedthrough with private state: out = in + steps taken so far.
class StatefulCounter final : public s7::Element {
public:
    StatefulCounter() : Element("StatefulCounter", true) {
        register_input(0, "in", s7::SignalType::F64, nullptr);
        register_output(0, "out", s7::SignalType::F64, &value_);
    }
    void step() override { value_ = read_input<double>(0) + static_cast<double>(count_++); }

private:
    double value_{};
    std::uint64_t count_{};
};

enum class FaultSite { FirstMemory, MiddleMemory, LastMemory, Direct, CoupledStage, TrapezoidStage };

// A ramp (one + Delay), a FaultingDelay and a FaultingGain on the ramp, and a
// coupled trapezoid oscillator. Memory order follows declaration order.
s7::Schema graph(const bool* fail, FaultSite site) {
    s7::Schema schema("rollback");
    schema.set_rate(s7::Rate::from_hz(100));
    const auto faulting_memory = [fail, site](FaultSite here) {
        return [fail, site, here] { return std::make_unique<FaultingDelay>(site == here ? fail : nullptr); };
    };
    schema.add_element("held_first", faulting_memory(FaultSite::FirstMemory));
    schema.add_element("one", [] { return std::make_unique<s7::Constant>(1.0); });
    schema.add_element("ramp", [] { return std::make_unique<s7::Adder>(); });
    schema.add_element("ramp_delay", [] { return std::make_unique<s7::Delay>(0.0); });
    schema.add_element("held_middle", faulting_memory(FaultSite::MiddleMemory));
    schema.add_element("x", [] { return std::make_unique<s7::TrapezoidIntegrator>(0.01, 1.0); });
    schema.add_element("v", [] { return std::make_unique<s7::TrapezoidIntegrator>(0.01, 0.0); });
    schema.add_element("minus", [] { return std::make_unique<s7::Gain>(-1.0); });
    schema.add_element("doubled", [fail, site] {
        return std::make_unique<FaultingGain>(site == FaultSite::Direct ? fail : nullptr);
    });
    schema.add_element("held_last", faulting_memory(FaultSite::LastMemory));
    // Trapezoid forcing: v' = -x + forcing.
    schema.add_element("trapezoid_forcing", [fail, site] {
        return std::make_unique<FaultingForcing>(site == FaultSite::TrapezoidStage ? fail : nullptr);
    });
    schema.add_element("v_derivative", [] { return std::make_unique<s7::Adder>(); });
    // A coupled Gauss-Legendre state: g' = -g + forcing.
    schema.add_element("g", [] { return std::make_unique<s7::GaussLegendreIntegrator>(0.01, 1.0); });
    schema.add_element("g_minus", [] { return std::make_unique<s7::Gain>(-1.0); });
    schema.add_element("g_forcing", [fail, site] {
        return std::make_unique<FaultingForcing>(site == FaultSite::CoupledStage ? fail : nullptr);
    });
    schema.add_element("g_derivative", [] { return std::make_unique<s7::Adder>(); });
    schema.connect("one", 0, "ramp", 0);
    schema.connect("ramp_delay", 0, "ramp", 1);
    schema.connect("ramp", 0, "ramp_delay", 0);
    schema.connect("ramp", 0, "held_first", 0);
    schema.connect("ramp", 0, "held_last", 0);
    schema.connect("ramp", 0, "held_middle", 0);
    schema.connect("ramp", 0, "doubled", 0);
    schema.connect("v", 0, "x", 0);
    schema.connect("x", 0, "minus", 0);
    schema.connect("minus", 0, "v_derivative", 0);
    schema.connect("trapezoid_forcing", 0, "v_derivative", 1);
    schema.connect("v_derivative", 0, "v", 0);
    schema.connect("g", 0, "g_minus", 0);
    schema.connect("g_minus", 0, "g_derivative", 0);
    schema.connect("g_forcing", 0, "g_derivative", 1);
    schema.connect("g_derivative", 0, "g", 0);
    return schema;
}

const std::vector<std::string> kOutputs{"held_first", "ramp", "ramp_delay", "held_middle", "x", "v", "minus", "doubled",
    "held_last", "trapezoid_forcing", "v_derivative", "g", "g_minus", "g_forcing", "g_derivative"};

bool same(const std::vector<double>& a, const std::vector<double>& b) {
    return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](double x, double y) {
        return std::bit_cast<std::uint64_t>(x) == std::bit_cast<std::uint64_t>(y);
    });
}

std::vector<double> outputs(const s7::NodeRuntime& runtime) {
    std::vector<double> values;
    for (const auto& id : kOutputs) values.push_back(runtime.real_output_of(id));
    return values;
}

// Faults on each listed step (1-based count of attempted steps), retrying
// until it succeeds, and compares with an unfaulted run after every attempt.
void check(FaultSite site, const std::vector<int>& faulting_attempts, const std::string& label) {
    bool fail = false;
    const bool never = false;
    s7::NodeRuntime runtime(s7::Compiler::compile(graph(&fail, site)));
    s7::NodeRuntime reference(s7::Compiler::compile(graph(&never, site)));
    int attempt = 0;
    for (int completed = 0; completed < 30;) {
        ++attempt;
        fail = std::find(faulting_attempts.begin(), faulting_attempts.end(), attempt) != faulting_attempts.end();
        const auto before = outputs(runtime);
        if (fail) {
            bool threw = false;
            try {
                runtime.step();
            } catch (const std::runtime_error&) {
                threw = true;
            }
            require(threw, label + ": injected fault did not propagate");
            require(runtime.step_count() == static_cast<std::size_t>(completed), label + ": faulting step advanced");
            require(same(outputs(runtime), before), label + ": faulting step published part of its outputs");
            continue;
        }
        runtime.step();
        reference.step();
        ++completed;
        require(runtime.step_count() == reference.step_count(), label + ": step count");
        require(same(outputs(runtime), outputs(reference)),
                label + ": recovered run differs from unfaulted run at step " + std::to_string(completed));
    }
    // No element with private state stepped before these faults (ADR-029).
    require(runtime.retry_exactness().inexact_faults == 0, label + ": exact retry reported as inexact");
}

// ADR-029: when an element with private state steps before the fault, the
// retry advances that state again. The runtime must say so, and only then.
void check_retry_exactness() {
    for (const bool counter_first : {true, false}) {
        bool fail = false;
        const bool never = false;
        const auto graph = [counter_first](const bool* flag) {
            s7::Schema schema("retry_exactness");
            schema.set_rate(s7::Rate::from_hz(100));
            schema.add_element("one", [] { return std::make_unique<s7::Constant>(1.0); });
            schema.add_element("counter", [] { return std::make_unique<StatefulCounter>(); });
            schema.add_element("faulting", [flag] { return std::make_unique<FaultingGain>(flag); });
            if (counter_first) {
                schema.connect("one", 0, "counter", 0);
                schema.connect("counter", 0, "faulting", 0);
            } else {
                schema.connect("one", 0, "faulting", 0);
                schema.connect("faulting", 0, "counter", 0);
            }
            return schema;
        };
        s7::NodeRuntime runtime(s7::Compiler::compile(graph(&fail)));
        s7::NodeRuntime reference(s7::Compiler::compile(graph(&never)));
        for (int step = 0; step < 3; ++step) { runtime.step(); reference.step(); }
        fail = true;
        bool threw = false;
        try { runtime.step(); } catch (const std::runtime_error&) { threw = true; }
        require(threw, "retry exactness: injected fault did not propagate");
        fail = false;
        runtime.step();
        reference.step();
        const auto& retry = runtime.retry_exactness();
        if (counter_first) {
            require(retry.inexact_faults == 1 && retry.last_elements == std::vector<std::string>{"counter"},
                    "retry exactness: advanced private state was not reported");
            // The documented consequence: the counter advanced twice.
            require(runtime.real_output_of("counter") == reference.real_output_of("counter") + 1.0,
                    "retry exactness: expected the counter to have advanced once more");
        } else {
            require(retry.inexact_faults == 0 && retry.last_elements.empty(),
                    "retry exactness: a fault before any stateful element was reported as inexact");
            require(runtime.real_output_of("counter") == reference.real_output_of("counter"),
                    "retry exactness: retry before the stateful element is not exact");
        }
    }
}
// Two rate domains joined by a Resampler: a 100 Hz ramp with a faulting
// memory element feeds a 50 Hz domain. ADR-021 holds per domain step
// (ADR-022): the faulting domain publishes nothing, the Resampler captures
// nothing from it, and retrying continues exactly. A fault inside the slow
// domain itself cannot be retried exactly: its Resampler has already
// advanced private state, which the runtime reports (ADR-029).
s7::Schema multirate_graph(const bool* fail) {
    s7::Schema schema("rollback_multirate");
    schema.set_rate(s7::Rate::from_hz(100));
    schema.add_element("one", [] { return std::make_unique<s7::Constant>(1.0); });
    schema.add_element("ramp", [] { return std::make_unique<s7::Adder>(); });
    schema.add_element("ramp_delay", [] { return std::make_unique<s7::Delay>(0.0); });
    schema.add_element("fast_held", [fail] { return std::make_unique<FaultingDelay>(fail); });
    schema.add_element("down", [] { return std::make_unique<s7::Resampler>(0); });
    // Pins the slow domain to 50 Hz.
    schema.add_element("slow_sum", [] { return std::make_unique<s7::EulerIntegrator>(0.02, 0.0); });
    schema.connect("one", 0, "ramp", 0);
    schema.connect("ramp_delay", 0, "ramp", 1);
    schema.connect("ramp", 0, "ramp_delay", 0);
    schema.connect("ramp", 0, "fast_held", 0);
    schema.connect("fast_held", 0, "down", 0);
    schema.connect("down", 0, "slow_sum", 0);
    return schema;
}

void check_multirate(const std::vector<int>& faulting_attempts, const std::string& label) {
    const std::vector<std::string> fast{"ramp", "ramp_delay", "fast_held"}, slow{"down", "slow_sum"};
    const auto values = [](const s7::NodeRuntime& runtime, const std::vector<std::string>& ids) {
        std::vector<double> result;
        for (const auto& id : ids) result.push_back(runtime.real_output_of(id));
        return result;
    };
    bool fail = false;
    const bool never = false;
    s7::NodeRuntime runtime(s7::Compiler::compile(multirate_graph(&fail)));
    s7::NodeRuntime reference(s7::Compiler::compile(multirate_graph(&never)));
    runtime.set_model_time_rate(s7::Rate::from_hz(100));
    reference.set_model_time_rate(s7::Rate::from_hz(100));
    int attempt = 0, faults = 0;
    for (int completed = 0; completed < 40;) {
        ++attempt;
        fail = std::find(faulting_attempts.begin(), faulting_attempts.end(), attempt) != faulting_attempts.end();
        auto before = values(runtime, fast);
        const auto slow_before = values(runtime, slow);
        before.insert(before.end(), slow_before.begin(), slow_before.end());
        if (fail) {
            bool threw = false;
            try {
                runtime.step();
            } catch (const std::runtime_error&) {
                threw = true;
            }
            if (threw) {
                ++faults;
                require(runtime.step_count() == static_cast<std::size_t>(completed), label + ": faulting step advanced");
                auto after = values(runtime, fast);
                const auto slow_after = values(runtime, slow);
                after.insert(after.end(), slow_after.begin(), slow_after.end());
                require(same(after, before), label + ": faulting step published part of its outputs");
                continue;
            }
            require(false, label + ": injected fault did not propagate");
        } else {
            runtime.step();
        }
        reference.step();
        ++completed;
        require(same(values(runtime, fast), values(reference, fast)) && same(values(runtime, slow), values(reference, slow)),
                label + ": recovered run differs from unfaulted run at step " + std::to_string(completed));
    }
    require(faults > 0, label + ": no fault was injected");
}
} // namespace

int main() try {
    for (const auto& [site, name] : {std::pair{FaultSite::FirstMemory, "first memory element"},
                                     std::pair{FaultSite::MiddleMemory, "middle memory element"},
                                     std::pair{FaultSite::LastMemory, "last memory element"},
                                     std::pair{FaultSite::Direct, "direct element"},
                                     std::pair{FaultSite::CoupledStage, "Gauss-Legendre stage"},
                                     std::pair{FaultSite::TrapezoidStage, "implicit trapezoid stage"}}) {
        // A fault on the first step (nothing published yet), a later fault,
        // and consecutive faults.
        check(site, {1}, std::string(name) + ", first step");
        check(site, {4}, std::string(name) + ", later step");
        check(site, {6, 7, 8}, std::string(name) + ", consecutive faults");
    }
    check_retry_exactness();
    check_multirate({1}, "two rate domains, first step");
    check_multirate({5, 6}, "two rate domains, later steps");
    std::cout << "Faulting direct, causal-memory and numerical-stage steps roll back completely and recover exactly,"
                 " also across rate domains\n";
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
}
