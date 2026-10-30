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

s7::Schema nested_graph() {
    s7::Schema schema("nested");
    schema.set_rate(s7::Rate::from_hz(1000));
    schema.add_element("source", [] { return std::make_unique<s7::Constant>(1.0); });
    for (int i = 0; i < 16; ++i) {
        const auto id = "nested" + std::to_string(i);
        schema.add_element(id, [] {
            auto inner = std::make_unique<s7::Schema>("one_gain");
            inner->set_rate(s7::Rate::from_hz(1000));
            inner->add_element("gain", [] { return std::make_unique<s7::Gain>(1.00001); });
            inner->expose_input("in", s7::SignalType::F64, "gain", 0);
            inner->expose_output("out", s7::SignalType::F64, "gain", 0);
            return inner;
        });
        schema.connect(i ? "nested" + std::to_string(i - 1) : "source", 0, id, 0);
    }
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
    if (name == "nested") return nested_graph();
    if (name == "trapezoid") return trapezoid_graph();
    if (name == "pendulum") return pendulum_graph();
    throw std::invalid_argument("Unknown benchmark case: " + name);
}

std::string output_id(const std::string& name) {
    if (name == "gain64") return "gain63";
    if (name == "gain512" || name == "wide512") return "gain511";
    if (name == "feedback") return "gain";
    if (name == "nested") return "nested15";
    if (name == "trapezoid") return "x";
    return "theta1";
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
    const auto* output = static_cast<const double*>(runtime.output_binding_of(output_id(name), 0));
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
