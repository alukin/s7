// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: LGPL-3.0-or-later

// A step that faults in any phase publishes nothing: direct outputs, published
// causal-memory outputs and staged values return to their pre-step state, the
// step count does not advance, and the next successful step continues exactly
// as a run without the fault.

#include <s7/core.hpp>

#include <algorithm>
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

enum class FaultSite { FirstMemory, LastMemory, Direct };

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
    schema.add_element("x", [] { return std::make_unique<s7::TrapezoidIntegrator>(0.01, 1.0); });
    schema.add_element("v", [] { return std::make_unique<s7::TrapezoidIntegrator>(0.01, 0.0); });
    schema.add_element("minus", [] { return std::make_unique<s7::Gain>(-1.0); });
    schema.add_element("doubled", [fail, site] {
        return std::make_unique<FaultingGain>(site == FaultSite::Direct ? fail : nullptr);
    });
    schema.add_element("held_last", faulting_memory(FaultSite::LastMemory));
    schema.connect("one", 0, "ramp", 0);
    schema.connect("ramp_delay", 0, "ramp", 1);
    schema.connect("ramp", 0, "ramp_delay", 0);
    schema.connect("ramp", 0, "held_first", 0);
    schema.connect("ramp", 0, "held_last", 0);
    schema.connect("ramp", 0, "doubled", 0);
    schema.connect("v", 0, "x", 0);
    schema.connect("x", 0, "minus", 0);
    schema.connect("minus", 0, "v", 0);
    return schema;
}

const std::vector<std::string> kOutputs{"held_first", "ramp", "ramp_delay", "x", "v", "minus", "doubled", "held_last"};

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
            require(outputs(runtime) == before, label + ": faulting step published part of its outputs");
            continue;
        }
        runtime.step();
        reference.step();
        ++completed;
        require(runtime.step_count() == reference.step_count(), label + ": step count");
        require(outputs(runtime) == outputs(reference),
                label + ": recovered run differs from unfaulted run at step " + std::to_string(completed));
    }
}
} // namespace

int main() try {
    for (const auto& [site, name] : {std::pair{FaultSite::FirstMemory, "first memory element"},
                                     std::pair{FaultSite::LastMemory, "last memory element"},
                                     std::pair{FaultSite::Direct, "direct element"}}) {
        // A fault on the first step (nothing published yet), a later fault,
        // and consecutive faults.
        check(site, {1}, std::string(name) + ", first step");
        check(site, {4}, std::string(name) + ", later step");
        check(site, {6, 7, 8}, std::string(name) + ", consecutive faults");
    }
    std::cout << "Faulting direct and causal-memory steps roll back completely and recover exactly\n";
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
}
