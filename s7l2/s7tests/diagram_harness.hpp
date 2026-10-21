#pragma once

// Headless .s7diagram + probe-sidecar runner (../docs/VISUAL_NUMERICAL_TESTS_PLAN.md
// section 2): the same case a human can open in Studio, run here without Qt
// for pass/fail checks and (optionally) a trace dump for the separate
// gnuplot visualization pipeline (section 3, not part of this header).
//
// A diagram's schema is built from its JSON the same way
// s7core/tests/type_inference_smoke.cpp builds schemas from JSON-
// parameterized elements (ElementRegistry::find + make_element_spec), not by
// hand-constructing C++ objects: this is what lets the same .s7diagram file
// run here unmodified whether it came from Studio or was hand-written.
//
// dt/rate: the diagram's top-level "dt" seeds Schema::set_rate() (section
// 1.3); it is not rewritten into every element's own "dt" parameter (that
// old hand-propagation approach is superseded -- RateResolver, mandatory in
// Validator::validate()/Compiler::compile(), now proves every element's own
// rate agrees with it, or reports exactly which one does not).
//
// Plain functions, not a framework, matching numerical_harness.hpp: tests
// still use assert()/throw in a hand-rolled main().

#include <boost/json.hpp>

#include "s7/core.hpp"
#include "s7/observe.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace s7tests {

// One named probe from a <case>.probes.json sidecar's "probes" array
// (VISUAL_NUMERICAL_TESTS_PLAN.md section 1.2). The sidecar's "views" array
// is for the separate visualization driver (section 3); nothing here parses
// it.
struct DiagramProbe {
    std::string name;
    s7::ProbePoint point;
    std::string unit; // display only; empty if the sidecar omits it
};

inline boost::json::object read_json_object(const std::filesystem::path& path)
{
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        throw std::runtime_error("Cannot open '" + path.string() + "'.");
    }
    std::ostringstream buffer;
    buffer << file.rdbuf();
    auto value = boost::json::parse(buffer.str());
    if (!value.is_object()) {
        throw std::runtime_error("'" + path.string() + "' is not a JSON object.");
    }
    return value.as_object();
}

inline std::vector<DiagramProbe> load_probes(const std::filesystem::path& probes_path)
{
    const auto root = read_json_object(probes_path);
    const auto* probes_array = root.if_contains("probes");
    if (!probes_array || !probes_array->is_array()) {
        throw std::runtime_error("'" + probes_path.string() + "' has no 'probes' array.");
    }
    std::vector<DiagramProbe> probes;
    for (const auto& entry : probes_array->as_array()) {
        const auto& object = entry.as_object();
        DiagramProbe probe;
        probe.name = std::string(object.at("name").as_string());
        probe.point.element_id = std::string(object.at("element").as_string());
        probe.point.port = object.at("port").to_number<std::size_t>();
        if (const auto* unit = object.if_contains("unit")) {
            probe.unit = std::string(unit->as_string());
        }
        probes.push_back(std::move(probe));
    }
    return probes;
}

// Builds the s7::Schema from diagram["schema"]; seeds its rate from the
// diagram's own top-level "dt" (seconds). Every element in the diagram's
// "schema"."elements" array must be a type ElementRegistry knows -- a
// Studio-only element such as "Scope" (OPEN_PROBLEMS.md, ADR-011/012) is not
// usable here, by design: probes are the sidecar, not a graph element.
inline s7::Schema build_schema(const boost::json::object& diagram)
{
    const auto& schema_json = diagram.at("schema").as_object();
    s7::Schema schema{std::string(schema_json.at("name").as_string())};
    schema.set_rate(s7::Rate::from_seconds(diagram.at("dt").to_number<double>()));

    auto& registry = s7::ElementRegistry::instance();
    for (const auto& value : schema_json.at("elements").as_array()) {
        const auto& element = value.as_object();
        const auto id = std::string(element.at("id").as_string());
        const auto type = std::string(element.at("type").as_string());
        const auto* descriptor = registry.find(type);
        if (!descriptor) {
            throw std::invalid_argument("Diagram element '" + id + "': unregistered type '" + type + "'.");
        }
        const auto* params = element.if_contains("params");
        schema.add_element(s7::make_element_spec(id, *descriptor, params ? params->as_object() : boost::json::object{}));
    }
    for (const auto& value : schema_json.at("connections").as_array()) {
        const auto& connection = value.as_object();
        schema.connect(std::string(connection.at("from_element").as_string()), connection.at("from_port").to_number<std::size_t>(),
                       std::string(connection.at("to_element").as_string()), connection.at("to_port").to_number<std::size_t>());
    }
    return schema;
}

// Loads a <case>.s7diagram + <case>.probes.json pair and compiles a runtime
// from it: Validator::validate() / Compiler::compile() run as usual (and,
// through them, the mandatory RateResolver check), and the runtime's model
// time uses the rational rate from the diagram's own "dt", so each
// ObservationSnapshot carries absolute model time without cumulative ns rounding.
struct DiagramCase {
    boost::json::object diagram;
    std::vector<DiagramProbe> probes;
    // NodeRuntime holds a std::atomic member (its stepping-thread check), so
    // it is neither copyable nor movable; own it through a pointer so
    // DiagramCase itself can still be returned by value.
    std::unique_ptr<s7::NodeRuntime> runtime;
};

inline DiagramCase load_and_compile(const std::filesystem::path& diagram_path, const std::filesystem::path& probes_path)
{
    auto diagram = read_json_object(diagram_path);
    auto probes = load_probes(probes_path);
    auto schema = build_schema(diagram);

    const auto validation = s7::Validator::validate(schema);
    if (!validation.ok()) {
        std::string message = "'" + diagram_path.string() + "' failed validation:";
        for (const auto& error : validation.errors) {
            message += "\n - " + error;
        }
        throw std::runtime_error(message);
    }

    auto runtime = std::make_unique<s7::NodeRuntime>(s7::Compiler::compile(schema));
    const auto dt_seconds = diagram.at("dt").to_number<double>();
    runtime->set_model_time_rate(s7::Rate::from_seconds(dt_seconds));

    return DiagramCase{std::move(diagram), std::move(probes), std::move(runtime)};
}

// Builds the probe list ObservationService::snapshot() needs, in the same
// order as `case_.probes` (so a trace row's samples[i] is probes[i]'s value).
inline std::vector<s7::ProbePoint> probe_points(const std::vector<DiagramProbe>& probes)
{
    std::vector<s7::ProbePoint> points;
    points.reserve(probes.size());
    for (const auto& probe : probes) {
        points.push_back(probe.point);
    }
    return points;
}

// Steps `case_.runtime` `steps` times, sampling every `stride`-th step
// (section 1.4's observation-side decimation; stride=1 samples every step)
// through `case_.probes`. One ObservationSnapshot per sampled step.
inline std::vector<s7::ObservationSnapshot> run_and_sample(DiagramCase& case_, const std::size_t steps, const std::size_t stride = 1)
{
    s7::ObservationService observer{*case_.runtime};
    const auto points = probe_points(case_.probes);
    std::vector<s7::ObservationSnapshot> trace;
    trace.reserve(steps / std::max<std::size_t>(stride, 1) + 1);
    for (std::size_t step = 1; step <= steps; ++step) {
        case_.runtime->step();
        if (step % stride == 0) {
            trace.push_back(observer.snapshot(points));
        }
    }
    return trace;
}

// Writes a plain-text table (section 2, step 7): "step model_time_s <name>
// ...", one row per sampled step. A complex-typed probe contributes
// "<name>.re"/"<name>.im" columns instead of one (section 1.6) -- never
// ObservationSample::to_display_double(), which deliberately throws on a
// complex value. Off by default in tests (call only when a trace dump was
// actually asked for); the separate visualization driver (section 3) reads
// this file's format.
inline void write_trace(const std::filesystem::path& path, const std::vector<DiagramProbe>& probes,
                        const std::vector<s7::ObservationSnapshot>& trace)
{
    std::ofstream file(path, std::ios::binary);
    if (!file) {
        throw std::runtime_error("Cannot write '" + path.string() + "'.");
    }
    file << "step model_time_s";
    for (std::size_t column = 0; column < probes.size(); ++column) {
        const bool complex = !trace.empty() && trace.front().samples[column].is_complex();
        if (complex) {
            file << ' ' << probes[column].name << ".re" << ' ' << probes[column].name << ".im";
        } else {
            file << ' ' << probes[column].name;
        }
    }
    file << '\n';
    for (const auto& snapshot : trace) {
        file << snapshot.step << ' ' << snapshot.model_time_seconds.value_or(0.0);
        for (const auto& sample : snapshot.samples) {
            if (sample.is_complex()) {
                const auto value = sample.as_complex();
                file << ' ' << value.real() << ' ' << value.imag();
            } else {
                file << ' ' << sample.to_display_double();
            }
        }
        file << '\n';
    }
}

} // namespace s7tests
