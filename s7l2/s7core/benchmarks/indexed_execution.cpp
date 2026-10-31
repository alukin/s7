// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: LGPL-3.0-or-later

#include <s7/core.hpp>
#include <s7/schema_library.hpp>

#include <boost/json.hpp>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#if defined(__linux__)
#include <unistd.h>
#endif

namespace {
using Clock = std::chrono::steady_clock;

std::size_t resident_bytes() {
#if defined(__linux__)
    std::ifstream statm("/proc/self/statm");
    std::size_t pages{}, resident{};
    if (statm >> pages >> resident) return resident * static_cast<std::size_t>(sysconf(_SC_PAGESIZE));
#endif
    return 0;
}

s7::Schema gain_graph(std::size_t count, bool chain) {
    s7::Schema schema(chain ? "gain_chain" : "independent_gains");
    schema.set_rate(s7::Rate::from_hz(1000));
    schema.add_element("source", [] { return std::make_unique<s7::Constant>(1.0); });
    for (std::size_t i = 0; i < count; ++i) {
        const auto id = "gain" + std::to_string(i);
        schema.add_element(id, [] { return std::make_unique<s7::Gain>(1.00001); });
        schema.connect(chain && i ? "gain" + std::to_string(i - 1) : "source", 0, id, 0);
    }
    return schema;
}

s7::Schema feedback_graph() {
    s7::Schema schema("feedback");
    schema.set_rate(s7::Rate::from_hz(1000));
    schema.add_element("source", [] { return std::make_unique<s7::Constant>(1.0); });
    schema.add_element("sum", [] { return std::make_unique<s7::Adder>(); });
    schema.add_element("gain", [] { return std::make_unique<s7::Gain>(0.5); });
    schema.add_element("delay", [] { return std::make_unique<s7::Delay>(0.0); });
    schema.connect("source", 0, "sum", 0);
    schema.connect("delay", 0, "sum", 1);
    schema.connect("sum", 0, "gain", 0);
    schema.connect("gain", 0, "delay", 0);
    return schema;
}

std::unique_ptr<s7::Schema> one_gain_schema() {
    auto inner = std::make_unique<s7::Schema>("one_gain");
    inner->set_rate(s7::Rate::from_hz(1000));
    inner->add_element("gain", [] { return std::make_unique<s7::Gain>(1.00001); });
    inner->expose_input("in", s7::SignalType::F64, "gain", 0);
    inner->expose_output("out", s7::SignalType::F64, "gain", 0);
    return inner;
}

// count chained schema-backed elements made by make_inner, fed by a constant.
template <typename MakeInner>
s7::Schema nested_chain(const std::string& name, std::size_t count, MakeInner make_inner) {
    s7::Schema schema(name);
    schema.set_rate(s7::Rate::from_hz(1000));
    schema.add_element("source", [] { return std::make_unique<s7::Constant>(1.0); });
    for (std::size_t i = 0; i < count; ++i) {
        const auto id = "nested" + std::to_string(i);
        schema.add_element(id, make_inner);
        schema.connect(i ? "nested" + std::to_string(i - 1) : "source", 0, id, 0);
    }
    return schema;
}

// One schema with a 64-Gain internal chain.
std::unique_ptr<s7::Schema> large_inner_schema() {
    auto inner = std::make_unique<s7::Schema>("gain_chain64");
    inner->set_rate(s7::Rate::from_hz(1000));
    for (int i = 0; i < 64; ++i) {
        const auto id = "gain" + std::to_string(i);
        inner->add_element(id, [] { return std::make_unique<s7::Gain>(1.00001); });
        if (i) inner->connect("gain" + std::to_string(i - 1), 0, id, 0);
    }
    inner->expose_input("in", s7::SignalType::F64, "gain0", 0);
    inner->expose_output("out", s7::SignalType::F64, "gain63", 0);
    return inner;
}

// One input fanned out to eight Gains, each exposed as its own output.
std::unique_ptr<s7::Schema> multi_output_schema() {
    auto inner = std::make_unique<s7::Schema>("eight_outputs");
    inner->set_rate(s7::Rate::from_hz(1000));
    inner->add_element("split", [] { return std::make_unique<s7::Gain>(1.0); });
    inner->expose_input("in", s7::SignalType::F64, "split", 0);
    for (int i = 0; i < 8; ++i) {
        const auto id = "gain" + std::to_string(i);
        inner->add_element(id, [i] { return std::make_unique<s7::Gain>(1.0 + 0.001 * i); });
        inner->connect("split", 0, id, 0);
        inner->expose_output("out" + std::to_string(i), s7::SignalType::F64, id, 0);
    }
    return inner;
}

// Two nesting levels: four chained schemas, each holding four one-Gain schemas.
std::unique_ptr<s7::Schema> two_level_schema() {
    auto middle = std::make_unique<s7::Schema>("four_nested");
    middle->set_rate(s7::Rate::from_hz(1000));
    for (int i = 0; i < 4; ++i) {
        const auto id = "leaf" + std::to_string(i);
        middle->add_element(id, [] { return one_gain_schema(); });
        if (i) middle->connect("leaf" + std::to_string(i - 1), 0, id, 0);
    }
    middle->expose_input("in", s7::SignalType::F64, "leaf0", 0);
    middle->expose_output("out", s7::SignalType::F64, "leaf3", 0);
    return middle;
}

// Stateful nested model: y = 0.5 * (u + y[n-1]) through a Delay.
std::unique_ptr<s7::Schema> feedback_schema() {
    auto inner = std::make_unique<s7::Schema>("feedback_cell");
    inner->set_rate(s7::Rate::from_hz(1000));
    inner->add_element("sum", [] { return std::make_unique<s7::Adder>(); });
    inner->add_element("gain", [] { return std::make_unique<s7::Gain>(0.5); });
    inner->add_element("delay", [] { return std::make_unique<s7::Delay>(0.0); });
    inner->connect("delay", 0, "sum", 1);
    inner->connect("sum", 0, "gain", 0);
    inner->connect("gain", 0, "delay", 0);
    inner->expose_input("in", s7::SignalType::F64, "sum", 0);
    inner->expose_output("out", s7::SignalType::F64, "gain", 0);
    return inner;
}

// Coupled trapezoid oscillator inside a schema, exposing position and velocity.
s7::Schema nested_trapezoid_graph() {
    s7::Schema schema("nested_trapezoid");
    schema.add_element("oscillator", [] {
        auto inner = std::make_unique<s7::Schema>("oscillator");
        inner->set_rate(s7::Rate::from_hz(100));
        inner->add_element("x", [] { return std::make_unique<s7::TrapezoidIntegrator>(0.01, 1.0); });
        inner->add_element("v", [] { return std::make_unique<s7::TrapezoidIntegrator>(0.01, 0.0); });
        inner->add_element("minus", [] { return std::make_unique<s7::Gain>(-1.0); });
        inner->connect("v", 0, "x", 0);
        inner->connect("x", 0, "minus", 0);
        inner->connect("minus", 0, "v", 0);
        inner->expose_output("x", s7::SignalType::F64, "x", 0);
        inner->expose_output("v", s7::SignalType::F64, "v", 0);
        return inner;
    });
    return schema;
}

s7::Schema trapezoid_graph() {
    s7::Schema schema("coupled_trapezoid");
    schema.add_element("x", [] { return std::make_unique<s7::TrapezoidIntegrator>(0.01, 1.0); });
    schema.add_element("v", [] { return std::make_unique<s7::TrapezoidIntegrator>(0.01, 0.0); });
    schema.add_element("minus", [] { return std::make_unique<s7::Gain>(-1.0); });
    schema.connect("v", 0, "x", 0);
    schema.connect("x", 0, "minus", 0);
    schema.connect("minus", 0, "v", 0);
    return schema;
}

s7::Schema pendulum_graph() {
    auto& registry = s7::ElementRegistry::instance();
    s7::register_builtin_elements(registry);
    std::ifstream input(S7_PENDULUM_DIAGRAM);
    if (!input) throw std::runtime_error("Cannot open double-pendulum diagram");
    const std::string contents(std::istreambuf_iterator<char>{input}, {});
    const auto root = boost::json::parse(contents).as_object();
    auto definition = root.at("schema").as_object();
    definition["dt"] = root.at("dt");
    return s7::SchemaLibraryLoader::build_schema(definition, registry);
}

s7::Schema make_graph(const std::string& name) {
    if (name == "gain64") return gain_graph(64, true);
    if (name == "gain512") return gain_graph(512, true);
    if (name == "wide512") return gain_graph(512, false);
    if (name == "feedback") return feedback_graph();
    if (name == "nested1") return nested_chain("nested1", 1, one_gain_schema);
    if (name == "nested") return nested_chain("nested", 16, one_gain_schema);
    if (name == "nested64") return nested_chain("nested64", 64, one_gain_schema);
    if (name == "nested_large") return nested_chain("nested_large", 1, large_inner_schema);
    if (name == "nested_multi") return nested_chain("nested_multi", 1, multi_output_schema);
    if (name == "nested_levels") return nested_chain("nested_levels", 4, two_level_schema);
    if (name == "nested_feedback") return nested_chain("nested_feedback", 16, feedback_schema);
    if (name == "nested_trapezoid") return nested_trapezoid_graph();
    if (name == "trapezoid") return trapezoid_graph();
    if (name == "pendulum") return pendulum_graph();
    throw std::invalid_argument("Unknown benchmark case: " + name);
}

std::string output_id(const std::string& name) {
    if (name == "gain64") return "gain63";
    if (name == "gain512" || name == "wide512") return "gain511";
    if (name == "feedback") return "gain";
    if (name == "nested1" || name == "nested_large" || name == "nested_multi") return "nested0";
    if (name == "nested") return "nested15";
    if (name == "nested64") return "nested63";
    if (name == "nested_levels") return "nested3";
    if (name == "nested_feedback") return "nested15";
    if (name == "nested_trapezoid") return "oscillator";
    if (name == "trapezoid") return "x";
    return "theta1";
}

std::size_t output_port(const std::string& name) {
    return name == "nested_multi" ? 7 : 0;
}

double percentile(std::vector<double>& values, double fraction) {
    std::sort(values.begin(), values.end());
    return values[static_cast<std::size_t>((values.size() - 1) * fraction)];
}
} // namespace

int main(int argc, char** argv) try {
    if (argc < 2 || argc > 3) throw std::invalid_argument("Usage: s7core_indexed_benchmark CASE [STEPS]");
    const std::string name = argv[1];
    const std::size_t steps = argc == 3 ? std::stoull(argv[2]) : 20000;
    if (steps < 1000) throw std::invalid_argument("STEPS must be at least 1000");
    auto schema = make_graph(name);
    const auto rss_before = resident_bytes();
    const auto compile_start = Clock::now();
    auto graph = s7::Compiler::compile(schema);
    const auto compile_end = Clock::now();
    s7::NodeRuntime runtime{std::move(graph)};
    const auto init_end = Clock::now();
    const auto rss_after = resident_bytes();
    const auto* output = static_cast<const double*>(runtime.output_binding_of(output_id(name), output_port(name)));
    for (std::size_t i = 0; i < 1000; ++i) runtime.step();
    const auto start = Clock::now();
    for (std::size_t i = 0; i < steps; ++i) runtime.step();
    const auto end = Clock::now();
    const double average_ns = std::chrono::duration<double, std::nano>(end - start).count() / steps;
    std::vector<double> samples;
    samples.reserve(2000);
    for (int i = 0; i < 2000; ++i) {
        const auto a = Clock::now();
        runtime.step();
        const auto b = Clock::now();
        samples.push_back(std::chrono::duration<double, std::nano>(b - a).count());
    }
    std::cout << std::setprecision(17)
              << "case,compile_us,init_us,avg_ns,steps_per_sec,p50_ns,p99_ns,rss_delta_bytes,checksum\n"
              << name << ','
              << std::chrono::duration<double, std::micro>(compile_end - compile_start).count() << ','
              << std::chrono::duration<double, std::micro>(init_end - compile_end).count() << ','
              << average_ns << ',' << 1e9 / average_ns << ','
              << percentile(samples, 0.50) << ',' << percentile(samples, 0.99) << ','
              << (rss_after >= rss_before ? rss_after - rss_before : 0) << ','
              << *output << '\n';
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
}
