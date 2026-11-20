// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "diagram_harness.hpp"
#include <s7/schema_library.hpp>
#include <functional>
#include <iomanip>
#include <iostream>
#include <limits>

namespace {
constexpr std::size_t steps = 20;
constexpr double rate = 10;
constexpr double tolerance = 1e-12; // Short F64 sums of binary-exact powers of 1/2.
using Document = boost::json::object;
using Row = std::vector<double>;
using Trace = std::vector<Row>;

void require(bool ok, const std::string& message)
{
    if (!ok) throw std::runtime_error("Foundations: " + message);
}

Document load(const std::string& name)
{
    return s7tests::read_json_object(std::filesystem::path(S7_FOUNDATIONS_DIR)/(name+".s7diagram"));
}

boost::json::object& element(Document& doc, const std::string& id)
{
    for (auto& entry : doc.at("schema").at("elements").as_array())
        if (entry.at("id").as_string() == id) return entry.as_object();
    throw std::runtime_error("Missing element: " + id);
}

auto schema(const Document& doc) -> s7::Schema
{
    require(doc.at("dt").to_number<double>() == .1, "saved model rate");
    require(doc.at("studio_run").at("runtime_seconds").to_number<double>() == 2, "saved duration");
    auto graph = s7::SchemaLibraryLoader::build_schema(doc.at("schema"));
    graph.set_rate(s7::Rate::from_hz(rate));
    return graph;
}

// Closed-form homogeneous solution + finite sum of input contributions.
// This is independent of S7's graph evaluation and never steps another model.
double reference(int n, double initial, const std::function<double(int)>& input, double a = .5)
{
    double result = std::pow(a, n+1)*initial;
    for (int j = 0; j <= n; ++j) result += std::pow(a, n-j)*input(j);
    return result;
}

double zero(int) { return 0; }
double unit_step(int n) { return n >= 5 ? 1 : 0; }
double unit_impulse(int n) { return n == 5 ? 1 : 0; }

Trace run(const Document& doc, const std::vector<std::string>& ids,
          const std::function<Row(int)>& expected, double budget = tolerance)
{
    auto graph = schema(doc);
    const auto valid = s7::Validator::validate(graph);
    require(valid.ok(), "saved graph validation");
    s7::NodeRuntime runtime(s7::Compiler::compile(graph));
    runtime.set_model_time_rate(s7::Rate::from_hz(rate));
    s7::ObservationService observation(runtime);
    std::vector<s7::ProbePoint> probes;
    for (const auto& id : ids) probes.push_back({id, 0});
    Trace trace;
    double worst = 0;
    for (std::size_t n = 0; n < steps; ++n) {
        runtime.step();
        const auto snapshot = observation.snapshot(probes);
        require(snapshot.step == n+1 && snapshot.model_time_seconds == double(n+1)/rate,
                "n=0 is step 1 at 0.1 s; coherent time and step");
        require(snapshot.model_time_ns == static_cast<std::int64_t>(n+1)*100'000'000,
                "absolute model timestamp");
        const auto oracle = expected(static_cast<int>(n));
        require(oracle.size() == ids.size(), "reference channel count");
        Row row;
        for (std::size_t i = 0; i < ids.size(); ++i) {
            const double value = snapshot.samples[i].to_display_double();
            require(std::isfinite(value), "finite sample");
            worst = std::max(worst, std::abs(value-oracle[i]));
            require(std::abs(value-oracle[i]) <= budget,
                    ids[i]+": reference mismatch at n="+std::to_string(n));
            row.push_back(value);
        }
        trace.push_back(std::move(row));
    }
    std::cout << doc.at("id").as_string() << ": " << steps << " samples, max error=" << worst << '\n';
    return trace;
}

void fn01()
{
    auto doc = load("fn01_unit_delay");
    const auto oracle = [](int n) -> Row {return {1, 2-std::pow(.5, n), n == 0 ? 0 : 2-std::pow(.5, n-1)};};
    const std::vector<std::string> ids{"input", "output", "output_memory"};
    const auto baseline = run(doc, ids, oracle);
    require(baseline == run(doc, ids, oracle), "FN01 repeat from fresh initial state");
    element(doc, "output_gain")["params"].as_object()["factor"] = 1.25;
    const auto unstable = run(doc, ids, [](int n) -> Row {
        const double output = (std::pow(1.25, n+1)-1)/.25;
        return {1, output, n == 0 ? 0 : (std::pow(1.25, n)-1)/.25};
    }, 1e-10);
    require(unstable.back()[1] > 300 && baseline.back()[1] < 2,
            "gain 1.25 must fail the settling-to-2 specification");
    // Bypassing memory is not a legal zero-latency version of this loop.
    doc = load("fn01_unit_delay");
    for (auto& entry : doc.at("schema").at("connections").as_array())
        if (entry.at("to_element").as_string() == "output_gain") entry.as_object()["from_element"] = "output";
    const auto invalid = s7::Validator::validate(schema(doc));
    require(!invalid.ok(), "bypassed delay must be rejected before execution");
    require(std::any_of(invalid.errors.begin(), invalid.errors.end(), [](const auto& message) {
        return message.find("Cycle detected without causal memory") != std::string::npos;
    }), "missing-memory rejection must identify the causal cycle");
    std::cout << "FN01: unstable-gain and missing-memory negative cases passed\n";
}

void fn02()
{
    auto doc = load("fn02_natural_forced");
    const std::vector<std::string> ids{"natural", "impulse_response", "step_response", "impulse", "step"};
    const auto oracle = [](int n) -> Row {return {reference(n, 2, zero), reference(n, 0, unit_impulse),
        reference(n, 0, unit_step), unit_impulse(n), unit_step(n)};};
    const auto baseline = run(doc, ids, oracle);
    require(baseline[0][0] == 1 && baseline[5][1] == 1 && baseline[5][2] == 1,
            "FN02 exact prediction-table events");
    require(baseline == run(doc, ids, oracle), "FN02 independent reset");
    element(doc, "step")["params"].as_object()["final_value"] = 2;
    const auto variation = run(doc, ids, [](int n) -> Row {return {reference(n, 2, zero), reference(n, 0, unit_impulse),
        2*reference(n, 0, unit_step), unit_impulse(n), 2*unit_step(n)};});
    require(variation.back()[0] == baseline.back()[0] && variation.back()[1] == baseline.back()[1],
            "FN02 changing step input must not couple independent states");
    doc = load("fn02_natural_forced");
    element(doc, "impulse")["params"].as_object()["area"] = 1;
    const auto wrong = run(doc, ids, [](int n) -> Row {return {reference(n, 2, zero), 10*reference(n, 0, unit_impulse),
        reference(n, 0, unit_step), 10*unit_impulse(n), unit_step(n)};});
    require(wrong[5][1] == 10 && std::abs(wrong[5][1]-baseline[5][1]) > 8,
            "FN02 wrong area must fail the unit-sample claim");
    std::cout << "FN02: doubled step and wrong-area teaching cases passed\n";
}

void fn03()
{
    auto doc = load("fn03_initial_conditions");
    const std::vector<std::string> ids{"total", "natural", "forced", "sum_parts", "residual", "step"};
    const auto oracle = [](int n) -> Row {
        const double natural = reference(n, 2, zero), forced = reference(n, 0, unit_step);
        return {reference(n, 2, unit_step), natural, forced, natural+forced, 0, unit_step(n)};
    };
    const auto baseline = run(doc, ids, oracle);
    require(baseline == run(doc, ids, oracle), "FN03 reset and instance-owned state");
    element(doc, "total_memory")["params"].as_object()["initial_value"] = 0;
    const auto wrong = run(doc, ids, [](int n) -> Row {
        const double natural = reference(n, 2, zero), forced = reference(n, 0, unit_step);
        return {forced, natural, forced, natural+forced, -natural, unit_step(n)};
    });
    require(wrong[0][4] == -1 && baseline[0][4] == 0, "FN03 omitted initial state breaks the decomposition");
    doc = load("fn03_initial_conditions");
    auto& graph = doc.at("schema").as_object();
    graph.at("elements").as_array().push_back(Document{{"id", "extra_delay"}, {"type", "Delay"}, {"params", {{"initial_value", 0}}}});
    for (auto& entry : graph.at("connections").as_array())
        if (entry.at("to_element").as_string() == "total" && entry.at("to_port").to_number<int>() == 0)
            entry.as_object()["from_element"] = "extra_delay";
    graph.at("connections").as_array().push_back(Document{{"from_element", "step"}, {"from_port", 0},
        {"to_element", "extra_delay"}, {"to_port", 0}});
    const auto delayed = run(doc, ids, [](int n) -> Row {
        const double natural = reference(n, 2, zero), forced = reference(n, 0, unit_step);
        const double delayed_forced = n == 0 ? 0 : reference(n-1, 0, unit_step);
        return {natural+delayed_forced, natural, forced, natural+forced, delayed_forced-forced, unit_step(n)};
    });
    require(delayed[5][4] == -1 && delayed[6][0] > delayed[5][0],
            "FN03 an added delay changes model sample timing");
    std::cout << "FN03: omitted-state and added-delay teaching cases passed\n";
}
} // namespace

int main() try
{
    s7::register_builtin_elements(s7::ElementRegistry::instance());
    fn01(); fn02(); fn03();
    return 0;
} catch (const std::exception& ex) {
    std::cerr << ex.what() << '\n'; return 1;
}
