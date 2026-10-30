// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: LGPL-3.0-or-later

#include <s7/core.hpp>

#include <cmath>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

class RecordedGain final : public s7::Element {
public:
    RecordedGain(std::vector<std::string>& trace, std::string name, double factor)
        : Element("RecordedGain"), trace_(trace), name_(std::move(name)), factor_(factor) {
        register_input(0, "in", s7::SignalType::F64, nullptr);
        register_output(0, "out", s7::SignalType::F64, &value_);
    }
    void step() override {
        trace_.push_back(name_);
        value_ = read_input<double>(0) * factor_;
    }
private:
    std::vector<std::string>& trace_;
    std::string name_;
    double factor_;
    double value_{};
};

s7::Schema split_join(std::vector<std::string>& trace) {
    s7::Schema schema("split_join");
    schema.set_rate(s7::Rate::from_hz(1));
    schema.add_element("source", [] { return std::make_unique<s7::Constant>(2.0); });
    schema.add_element("left", [&] { return std::make_unique<RecordedGain>(trace, "left", 3.0); });
    schema.add_element("right", [&] { return std::make_unique<RecordedGain>(trace, "right", 4.0); });
    schema.add_element("join", [] { return std::make_unique<s7::Adder>(); });
    schema.connect("source", 0, "left", 0);
    schema.connect("source", 0, "right", 0);
    schema.connect("left", 0, "join", 0);
    schema.connect("right", 0, "join", 1);
    return schema;
}

void check_records(const s7::ExecutableGraph& graph) {
    const auto check = [&graph](const auto& ids, const auto& records) {
        require(ids.size() == records.size(), "Execution record count differs from ID order");
        for (std::size_t i = 0; i < ids.size(); ++i)
            require(graph.elements.at(ids[i]).get() == records[i], "Execution record points at wrong element");
    };
    check(graph.causal_order, graph.causal_execution);
    check(graph.memory_order, graph.memory_execution);
    check(graph.trapezoid_rhs, graph.trapezoid_rhs_execution);
}

void graph_moves_and_replacement() {
    std::vector<std::string> trace;
    const auto schema = split_join(trace);
    for (int repetition = 0; repetition < 40; ++repetition) {
        auto graph = s7::Compiler::compile(schema);
        check_records(graph);
        auto moved = std::move(graph);
        check_records(moved);
        auto replacement = s7::Compiler::compile(schema);
        replacement = std::move(moved);
        check_records(replacement);
        s7::NodeRuntime runtime(std::move(replacement));
        const auto begin = trace.size();
        runtime.step();
        require(runtime.real_output_of("join") == 14.0, "Moved split/join graph produced wrong output");
        require(runtime.step_count() == 1, "Step count changed");
        // The compiler's topology is deterministic; the trace records exact
        // direct-element order, including the split before the join.
        require(trace.size() == begin + 2 && trace[begin] == "left" && trace[begin + 1] == "right",
                "Split/join execution order changed");
    }
}

void memory_publication() {
    s7::Schema schema("two_memories");
    schema.set_rate(s7::Rate::from_hz(1));
    schema.add_element("source", [] { return std::make_unique<s7::Constant>(1.0); });
    schema.add_element("sum", [] { return std::make_unique<s7::Adder>(); });
    schema.add_element("first", [] { return std::make_unique<s7::Delay>(2.0); });
    schema.add_element("second", [] { return std::make_unique<s7::Delay>(3.0); });
    schema.connect("source", 0, "sum", 0);
    schema.connect("second", 0, "sum", 1);
    schema.connect("sum", 0, "first", 0);
    schema.connect("first", 0, "second", 0);
    auto graph = s7::Compiler::compile(schema);
    check_records(graph);
    s7::NodeRuntime runtime(std::move(graph));
    runtime.step();
    require(runtime.real_output_of("first") == 2.0 && runtime.real_output_of("second") == 3.0,
            "Memory outputs published too early");
    runtime.step();
    require(runtime.real_output_of("first") == 4.0 && runtime.real_output_of("second") == 2.0,
            "Multiple memory elements did not stage from one coherent step");
    runtime.step();
    require(runtime.real_output_of("first") == 3.0 && runtime.real_output_of("second") == 4.0,
            "Memory publication order changed");
}

void trapezoid_records() {
    s7::Schema schema("trapezoid_records");
    schema.add_element("x", [] { return std::make_unique<s7::TrapezoidIntegrator>(0.1, 1.0); });
    schema.add_element("negative", [] { return std::make_unique<s7::Gain>(-1.0); });
    schema.connect("x", 0, "negative", 0);
    schema.connect("negative", 0, "x", 0);
    auto graph = s7::Compiler::compile(schema);
    check_records(graph);
    require(graph.trapezoid_rhs.size() == 1 && graph.trapezoid_rhs.front() == "negative",
            "Trapezoid RHS schedule changed");
    s7::NodeRuntime runtime(std::move(graph));
    runtime.step();
    runtime.step();
    require(std::abs(runtime.real_output_of("x") - 0.95 / 1.05) < 1e-14,
            "Coupled trial produced wrong output");
}
} // namespace

int main() try {
    graph_moves_and_replacement();
    memory_publication();
    trapezoid_records();
    std::cout << "Indexed execution records, moves, replacement, ordering, memory and trials passed\n";
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
}
