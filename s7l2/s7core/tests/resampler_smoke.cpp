// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: LGPL-3.0-or-later

// Resampler and rate domains (doc/S7_Sample_Rate_Model.md section 8.3):
// interpolation orders are exact on signals they reproduce, the anti-aliasing
// filter passes the passband and rejects the stopband, domains step on the
// exact rational schedule, and feedback across domains is rejected.

#include <s7/builtin_element_registry.hpp>
#include <s7/core.hpp>
#include <s7/element_registry.hpp>
#include <s7/resampler.hpp>

#include <cmath>
#include <functional>
#include <iostream>
#include <memory>
#include <numbers>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

std::string errors(const s7::Schema& schema) {
    std::string text;
    for (const auto& error : s7::Validator::validate(schema).errors) text += error + "\n";
    return text;
}

// Drives a standalone Resampler as the runtime would: before output sample j
// the input domain has taken every sample at or before j's time.
std::vector<double> resample(int order, s7::Rate in, s7::Rate out, const std::function<double(std::uint64_t)>& signal,
                             std::uint64_t outputs, std::size_t* delay = nullptr) {
    s7::Resampler resampler(order);
    double input = 0;
    const s7::InputBinding binding = &input;
    resampler.bind_inputs(std::span(&binding, 1));
    resampler.adopt_rate(out);
    resampler.adopt_input_rate(in);
    if (delay) *delay = resampler.filter_delay();
    std::vector<double> result;
    std::uint64_t taken = 0;
    for (std::uint64_t j = 0; j < outputs; ++j) {
        for (const auto need = s7::resampling::samples_through(j, in, out); taken < need; ++taken) {
            input = signal(taken);
            resampler.capture_input();
        }
        resampler.step();
        result.push_back(*static_cast<const double*>(resampler.outputs()[0]));
    }
    return result;
}

void interpolation_orders() {
    const auto in = s7::Rate::from_hz(1), out = s7::Rate::from_hz(3);
    // Order 0 holds the latest input sample at or before each output time.
    const auto held = resample(0, in, out, [](std::uint64_t i) { return 10.0 * i; }, 12);
    for (std::uint64_t j = 0; j < held.size(); ++j) require(held[j] == 10.0 * (j / 3), "Order 0 sample and hold");
    // Order 1 is exact on a line, one input period late; before that it holds x[0].
    const auto line = resample(1, in, out, [](std::uint64_t i) { return 2.0 * i + 1; }, 12);
    for (std::uint64_t j = 0; j < line.size(); ++j) {
        const double p = j / 3.0 - 1;
        require(std::abs(line[j] - (p < 0 ? 1.0 : 2 * p + 1)) < 1e-12, "Order 1 interpolates a line one period late");
    }
    // Order 2 is exact on a parabola once three samples exist.
    const auto parabola = resample(2, in, out, [](std::uint64_t i) { return 1.0 * i * i; }, 15);
    for (std::uint64_t j = 6; j < parabola.size(); ++j) {
        const double p = j / 3.0 - 1;
        require(std::abs(parabola[j] - p * p) < 1e-12, "Order 2 interpolates a parabola one period late");
    }
    // A rational 3/2 ratio, exactly.
    const auto rational = resample(1, s7::Rate::from_hz(2), out, [](std::uint64_t i) { return 0.5 * i; }, 30);
    for (std::uint64_t j = 3; j < rational.size(); ++j) {
        const double p = j * 2.0 / 3.0 - 1;
        require(std::abs(rational[j] - 0.5 * p) < 1e-12, "Order 1 at a 2:3 ratio");
    }
    // Interpolation never reads past the input received: causal at any order.
    for (int order : {0, 1, 2}) static_cast<void>(resample(order, s7::Rate::from_hz(7), s7::Rate::from_hz(5), [](auto i) { return i; }, 50));
}

void anti_aliasing() {
    const auto in = s7::Rate::from_hz(8000), out = s7::Rate::from_hz(1000);
    std::size_t delay = 0;
    const auto tone = [](double hz) {
        return [hz](std::uint64_t i) { return std::sin(2 * std::numbers::pi * hz * i / 8000.0); };
    };
    // 100 Hz is in the passband (below 400 Hz): unit gain, delayed by the filter.
    const auto pass = resample(0, in, out, tone(100), 400, &delay);
    require(delay > 0, "Lowering the rate adds the filter's group delay");
    double error = 0;
    for (std::uint64_t j = 100; j < pass.size(); ++j) {
        const double expected = std::sin(2 * std::numbers::pi * 100 * (j * 8.0 - static_cast<double>(delay)) / 8000.0);
        error = std::max(error, std::abs(pass[j] - expected));
    }
    require(error < 2e-3, "Passband tone passes with unit gain and the documented delay: " + std::to_string(error));
    // 3 kHz is far above the output Nyquist frequency: rejected by at least 60 dB.
    const auto stop = resample(0, in, out, tone(3000), 400);
    double leak = 0;
    for (std::uint64_t j = 100; j < stop.size(); ++j) leak = std::max(leak, std::abs(stop[j]));
    require(leak < 1e-3, "Stopband tone is rejected: " + std::to_string(leak));
    // Raising the rate needs no anti-aliasing filter.
    s7::Resampler up;
    up.adopt_rate(s7::Rate::from_hz(4000));
    up.adopt_input_rate(s7::Rate::from_hz(1000));
    require(up.filter_delay() == 0, "Raising the rate adds no filter");
}

// A C++ source pinned at a rate, emitting its own step index.
class Counter final : public s7::Element {
public:
    explicit Counter(s7::Rate rate, bool with_input = false) : Element("Counter", true), rate_(rate) {
        if (with_input) register_input(0, "in", s7::SignalType::F64, nullptr);
        register_output(0, "out", s7::SignalType::F64, &value_);
    }
    void step() override { value_ = static_cast<double>(count_++); }
    [[nodiscard]] std::optional<s7::Rate> declared_rate() const override { return rate_; }
    std::uint64_t count_{};
private:
    s7::Rate rate_;
    double value_{};
};

s7::Schema domains(const s7::Rate input, Counter** counter, int order = 0) {
    auto& registry = s7::ElementRegistry::instance();
    s7::Schema schema("domains");
    schema.set_rate(s7::Rate::from_hz(3)); // the run's rate
    schema.add_element("counter", [input, counter] {
        auto made = std::make_unique<Counter>(input);
        *counter = made.get();
        return made;
    });
    schema.add_element(s7::make_element_spec("resample", *registry.find("Resampler"), {{"order", order}}));
    schema.add_element(s7::make_element_spec("gain", *registry.find("Gain"), {{"factor", 1.0}}));
    schema.connect("counter", 0, "resample", 0);
    schema.connect("resample", 0, "gain", 0);
    return schema;
}

void rate_domains() {
    // 2 Hz counter -> Resampler -> 3 Hz run. Step k (time k/3) holds the
    // counter's latest sample floor(2k/3), and the counter has taken exactly
    // floor(2k/3) + 1 samples.
    Counter* counter = nullptr;
    auto schema = domains(s7::Rate::from_hz(2), &counter);
    require(errors(schema).empty(), "Resampler joins two rate domains: " + errors(schema));
    s7::NodeRuntime runtime(s7::Compiler::compile(schema));
    std::string unscheduled;
    try { runtime.step(); } catch (const std::logic_error& error) { unscheduled = error.what(); }
    require(unscheduled.find("model-time rate") != std::string::npos, "Domains need a model time to be scheduled");
    require(runtime.step_count() == 0 && counter->count_ == 0, "Unscheduled step changed nothing");
    std::string conflict;
    try { runtime.set_model_time_rate(s7::Rate::from_hz(5)); } catch (const std::invalid_argument& error) { conflict = error.what(); }
    require(conflict.find("conflicts") != std::string::npos, "A model time matching no domain is rejected");
    runtime.set_model_time_rate(s7::Rate::from_hz(3));
    for (std::uint64_t k = 0; k < 30; ++k) {
        runtime.step();
        require(counter->count_ == 2 * k / 3 + 1, "Input domain takes every sample up to the step's time");
        require(runtime.real_output_of("gain") == static_cast<double>(2 * k / 3), "Output holds the latest input sample");
    }
    // Lowering the rate: a 6 Hz counter takes two samples per 3 Hz step.
    Counter* fast = nullptr;
    s7::NodeRuntime down(s7::Compiler::compile(domains(s7::Rate::from_hz(6), &fast, 1)));
    down.set_model_time_rate(s7::Rate::from_hz(3));
    for (std::uint64_t k = 0; k < 20; ++k) {
        down.step();
        require(fast->count_ == 2 * k + 1, "Faster input domain steps twice per run step");
        require(std::isfinite(down.real_output_of("gain")), "Filtered output is finite");
    }
}

void equal_rates_pass_through() {
    auto& registry = s7::ElementRegistry::instance();
    s7::Schema schema("equal");
    schema.set_rate(s7::Rate::from_hz(10));
    schema.add_element(s7::make_element_spec("ramp", *registry.find("ChirpGenerator"), {}));
    schema.add_element(s7::make_element_spec("resample", *registry.find("Resampler"), {{"order", 2}}));
    schema.connect("ramp", 0, "resample", 0);
    const auto graph = s7::Compiler::compile(schema);
    require(graph.domains.size() == 1 && graph.domain_links.empty(), "Equal rates are one domain");
    s7::NodeRuntime runtime(s7::Compiler::compile(schema));
    for (int n = 0; n < 20; ++n) {
        runtime.step();
        require(runtime.real_output_of("resample") == runtime.real_output_of("ramp"), "Equal rates pass values through unchanged");
    }
}

void feedback_across_domains() {
    // 3 Hz gain -> Resampler -> 2 Hz counter-with-input -> Resampler -> gain: a loop across rates.
    auto& registry = s7::ElementRegistry::instance();
    s7::Schema schema("loop");
    schema.set_rate(s7::Rate::from_hz(3));
    schema.add_element(s7::make_element_spec("sum", *registry.find("Adder"), {}));
    schema.add_element(s7::make_element_spec("one", *registry.find("Constant"), {{"value", 1.0}}));
    schema.add_element(s7::make_element_spec("down", *registry.find("Resampler"), {}));
    schema.add_element("slow", [] { return std::make_unique<Counter>(s7::Rate::from_hz(2), true); });
    schema.add_element(s7::make_element_spec("up", *registry.find("Resampler"), {}));
    schema.add_element(s7::make_element_spec("memory", *registry.find("Delay"), {}));
    schema.connect("one", 0, "sum", 0);
    schema.connect("memory", 0, "sum", 1);
    schema.connect("sum", 0, "down", 0);
    schema.connect("down", 0, "slow", 0);
    schema.connect("slow", 0, "up", 0);
    schema.connect("up", 0, "memory", 0);
    const auto text = errors(schema);
    require(text.find("Feedback across rate domains") != std::string::npos, "Feedback across rate domains is rejected: " + text);
}

void nested_resampler() {
    // A nested schema at the run's 3 Hz with a 2 Hz counter resampled inside it.
    auto& registry = s7::ElementRegistry::instance();
    Counter* counter = nullptr;
    s7::Schema parent("parent");
    parent.set_rate(s7::Rate::from_hz(3));
    parent.add_element("inner", [&registry, &counter] {
        auto inner = std::make_unique<s7::Schema>("inner");
        inner->set_rate(s7::Rate::from_hz(3));
        inner->add_element("counter", [&counter] {
            auto made = std::make_unique<Counter>(s7::Rate::from_hz(2));
            counter = made.get();
            return made;
        });
        inner->add_element(s7::make_element_spec("resample", *registry.find("Resampler"), {}));
        inner->connect("counter", 0, "resample", 0);
        inner->expose_output("out", s7::SignalType::F64, "resample", 0);
        return inner;
    });
    s7::NodeRuntime runtime(s7::Compiler::compile(parent));
    runtime.set_model_time_rate(s7::Rate::from_hz(3));
    for (std::uint64_t k = 0; k < 12; ++k) {
        runtime.step();
        require(runtime.real_output_of("inner") == static_cast<double>(2 * k / 3), "Nested domains follow the parent's steps");
    }
}
} // namespace

int main() try {
    s7::register_builtin_elements(s7::ElementRegistry::instance());
    interpolation_orders();
    anti_aliasing();
    rate_domains();
    equal_rates_pass_through();
    feedback_across_domains();
    nested_resampler();
    std::cout << "Resampler orders, anti-aliasing, rate domain scheduling, pass-through and feedback rules passed\n";
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
}
