// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Isolated research: the production Compiler, NodeRuntime and stream endpoint
// are unchanged. Only this executable reorders its own compiled dispatch records.
// Sources/assumptions: docs/CROSS_NODE_CAUSALITY_RESEARCH.md (L1/L3/L7).
#include <s7/core.hpp>
#include <s7/deployment_client.hpp>
#include <s7/net.hpp>
#include <s7/schema_library.hpp>

#include <algorithm>
#include <array>
#include <barrier>
#include <chrono>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {
namespace json = boost::json;
constexpr const char* kA = "aaaaaaaa-0000-4000-8000-000000000001";
constexpr const char* kB = "bbbbbbbb-0000-4000-8000-000000000002";
constexpr std::size_t kSteps = 32;

void require(bool ok, const std::string& message)
{
    if (!ok) throw std::runtime_error(message);
}

auto element(const char* id, const char* type, const char* node, json::object params = {}) -> json::object
{
    return {{"id", id}, {"type", type}, {"node", node}, {"params", std::move(params)}};
}

auto connection(const char* from, const char* to, unsigned port = 0) -> json::object
{
    return {{"from_element", from}, {"from_port", 0}, {"to_element", to}, {"to_port", port}};
}

auto diagram(bool memory_on_b, bool nested, bool independent, bool no_memory = false) -> json::object
{
    json::array elements, connections;
    if (independent) {
        elements = {element("a", "Constant", kA, {{"value", 2}}),
                    element("b", "Constant", kB, {{"value", 3}}),
                    element("on_a", "Gain", kA, {{"factor", 5}}),
                    element("on_b", "Gain", kB, {{"factor", 7}})};
        connections = {connection("b", "on_a"), connection("a", "on_b")};
    } else {
        const auto* owner = memory_on_b ? kB : kA;
        const auto* other = memory_on_b ? kA : kB;
        auto state = element("state", no_memory ? "Gain" : "Delay", owner);
        if (nested) {
            state = element("state", "Schema", owner);
            state["schema"] = json::object{
                {"name", "memory_box"}, {"dt", 1.0},
                {"elements", json::array{json::object{{"id", "d"}, {"type", "Delay"}, {"params", json::object{}}}}},
                {"connections", json::array{}},
                {"exposed_inputs", json::array{json::object{{"name", "u"}, {"type", "F64"}, {"element", "d"}, {"port", 0}}}},
                {"exposed_outputs", json::array{json::object{{"name", "y"}, {"type", "F64"}, {"element", "d"}, {"port", 0}}}}};
        }
        elements = {state, element("bridge", "Gain", owner, {{"factor", 1}}),
                    element("one", "Constant", other, {{"value", 1}}), element("sum", "Adder", other)};
        connections = {connection("state", "bridge"), connection("bridge", "sum"),
                       connection("one", "sum", 1), connection("sum", "state")};
    }
    return {{"dt", 1.0},
            {"runtime_nodes", json::array{json::object{{"node_uuid", kA}, {"endpoint", "ws://127.0.0.1:9101"}},
                                           json::object{{"node_uuid", kB}, {"endpoint", "ws://127.0.0.1:9102"}}}},
            {"schema", json::object{{"name", "scheduling_spike"}, {"dt", 1.0},
                                     {"elements", std::move(elements)}, {"connections", std::move(connections)}}}};
}

auto compile(const json::object& schema) -> s7::ExecutableGraph
{
    auto model = s7::SchemaLibraryLoader::build_schema(schema, s7::ElementRegistry::instance());
    return s7::Compiler::compile(model);
}

// For these single-output, single-rate fixtures, expand the serial reference's
// same-step order with send/receive events immediately after each producer.
// This is a spike, not a general distributed compiler (no coupled regions).
void project(s7::ExecutableGraph& part, const s7::ExecutableGraph& whole,
             const std::vector<s7::deployment::StreamDescriptor>& streams)
{
    require(whole.coupled_regions.empty() && whole.trapezoid_rhs.empty(), "unsupported numerical fixture");
    require(part.domains.size() == 1, "single-rate fixtures only");
    std::map<std::string, int> ranks;
    for (std::size_t i = 0; i < whole.causal_order.size(); ++i)
        ranks[whole.causal_order[i]] = static_cast<int>(3 * i);
    for (const auto& id : whole.memory_order) ranks[id] = -3;
    auto rank = [&](const std::string& id) {
        auto* stream = dynamic_cast<s7::NetStreamElement*>(part.elements.at(id).get());
        if (!stream) return ranks.at(id);
        const auto descriptor = std::ranges::find_if(streams, [&](const auto& s) {
            return s.stream_id == stream->stream_config().stream_id;
        });
        require(descriptor != streams.end(), "missing stream descriptor");
        require(descriptor->producer.port == 0, "single-output fixture only");
        return ranks.at(descriptor->producer.element) + (stream->type_name() == "NetSink" ? 1 : 2);
    };
    std::ranges::stable_sort(part.causal_order, {}, rank);
    part.causal_execution.clear();
    for (const auto& id : part.causal_order) part.causal_execution.push_back(part.elements.at(id).get());
    part.domains[0].causal_execution = part.causal_execution;
}

auto run(const std::string& name, bool memory_on_b, bool nested, bool independent,
         bool projected, unsigned window, unsigned slow_ms) -> json::object
{
    auto input = diagram(memory_on_b, nested, independent);
    const auto types = s7::deployment::infer_output_types(input);
    require(s7::deployment::assign_streams(input, types).empty(), "stream assignment");
    for (auto& value : input.at("streams").as_array()) {
        auto& stream = value.as_object();
        stream["max_elements_per_packet"] = 1;
        stream["max_wait_ms"] = 1000;
        stream["send_log"] = json::object{{"max_packets", window}, {"max_block_ms", 1000}};
    }
    auto whole = compile(s7::deployment::diagram_schema(input));
    auto part_a = s7::deployment::extract_node_schema(input, kA);
    auto part_b = s7::deployment::extract_node_schema(input, kB);
    part_a["dt"] = part_b["dt"] = 1.0;
    std::array<s7::ExecutableGraph, 2> graphs{compile(part_a), compile(part_b)};
    const auto streams = s7::deployment::parse_streams(input);
    json::array orders;
    for (auto& graph : graphs) {
        if (projected) project(graph, whole, streams);
        json::array order;
        for (const auto& id : graph.causal_order) order.emplace_back(id);
        orders.emplace_back(std::move(order));
    }
    const std::vector<std::string> observed = independent ? std::vector<std::string>{"on_a", "on_b"}
                                                         : std::vector<std::string>{"sum"};
    std::map<std::string, std::vector<double>> reference;
    {
        s7::NodeRuntime serial(std::move(whole));
        for (std::size_t n = 0; n < kSteps; ++n) {
            serial.step();
            for (const auto& id : observed) {
                const double value = serial.real_output_of(id);
                const double expected = independent ? (id == "on_a" ? 15.0 : 14.0) : static_cast<double>(n + 1);
                require(value == expected, "unpartitioned reference disagrees with independent recurrence");
                reference[id].push_back(value);
            }
        }
    }
    auto endpoint = std::make_shared<s7::transport::InMemoryRuntimeNetEndpoint>();
    s7::RuntimeNetContext::set_endpoint(endpoint);
    json::array outcomes;
    json::array stream_states;
    bool completed = true;
    {
        std::array<std::vector<std::string>, 2> reads;
        for (std::size_t i = 0; i < graphs.size(); ++i)
            for (const auto& id : observed)
                if (graphs[i].elements.contains(id)) reads[i].push_back(id);
        std::array<s7::NodeRuntime, 2> runtimes{s7::NodeRuntime(std::move(graphs[0])), s7::NodeRuntime(std::move(graphs[1]))};
        std::array<std::map<std::string, std::vector<double>>, 2> values;
        std::array<std::string, 2> errors;
        std::array<unsigned, 2> counts{};
        std::barrier start(2);
        auto work = [&](std::size_t node) {
            start.arrive_and_wait();
            try {
                for (std::size_t n = 0; n < kSteps; ++n) {
                    if (slow_ms && node == 1) std::this_thread::sleep_for(std::chrono::milliseconds(slow_ms));
                    runtimes[node].step();
                    ++counts[node];
                    for (const auto& id : reads[node]) values[node][id].push_back(runtimes[node].real_output_of(id));
                }
            } catch (const std::exception& ex) { errors[node] = ex.what(); }
        };
        std::jthread a(work, 0), b(work, 1);
        a.join(); b.join();
        for (std::size_t i = 0; i < 2; ++i) {
            completed = completed && counts[i] == kSteps && errors[i].empty();
            json::object samples;
            for (const auto& id : reads[i]) {
                const auto& actual = values[i][id];
                require(std::equal(actual.begin(), actual.end(), reference[id].begin()), "partitioned output mismatch");
                json::array trace;
                for (const auto v : actual) trace.emplace_back(v);
                samples[id] = std::move(trace);
            }
            outcomes.emplace_back(json::object{{"node", i == 0 ? "A" : "B"}, {"steps", counts[i]},
                                               {"error", errors[i]}, {"samples", std::move(samples)}});
        }
        for (const auto& status : endpoint->stream_statuses())
            stream_states.emplace_back(json::object{{"stream_id", s7::wire::to_string(status.stream_id)},
                {"role", status.role == s7::transport::StreamRole::Producer ? "producer" : "consumer"},
                {"state", status.state}, {"fault", s7::stream::to_string(status.fault)},
                {"send_log_depth", status.send_log_depth}, {"buffered_elements", status.buffered_elements},
                {"elements", status.elements}});
    }
    s7::RuntimeNetContext::set_endpoint(nullptr);
    return {{"case", name}, {"approach", projected ? "projected" : "fixed"},
            {"send_log_packets", window}, {"node_b_delay_ms", slow_ms}, {"completed", completed},
            {"causal_orders", std::move(orders)}, {"nodes", std::move(outcomes)},
            {"stream_states", std::move(stream_states)}};
}

// No graph or blocking consumer: publish then immediately consume each value.
// This distinguishes ACK timer gating from the node scheduler/starvation path.
auto credit_probe(bool paced) -> json::object
{
    s7::transport::InMemoryRuntimeNetEndpoint endpoint;
    auto config = s7::net_stream_config("11111111-2222-4333-8444-555555555555");
    config.max_elements_per_packet = config.send_log_max_packets = 1;
    config.max_block = std::chrono::milliseconds(100);
    const auto producer = endpoint.open_producer(config);
    const auto consumer = endpoint.open_consumer(config);
    json::array records;
    const auto start = std::chrono::steady_clock::now();
    for (unsigned n = 1; n <= 3; ++n) {
        if (paced && n > 1) std::this_thread::sleep_for(s7::stream::kAckInterval + std::chrono::milliseconds(5));
        const double input = n;
        const auto result = endpoint.publish(producer, &input, 1);
        if (result.fault != s7::stream::FaultCode::None) {
            records.emplace_back(json::object{{"value", n}, {"publish_fault", s7::stream::to_string(result.fault)}});
            break;
        }
        double output = 0;
        const auto received = endpoint.receive(consumer, &output, 1);
        require(received.received == 1 && output == input && received.fault == s7::stream::FaultCode::None,
                "credit probe value mismatch");
        const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - start);
        for (const auto& status : endpoint.stream_statuses())
            if (status.role == s7::transport::StreamRole::Producer)
                records.emplace_back(json::object{{"value", n}, {"producer_log_after_consumption", status.send_log_depth},
                                                   {"elapsed_us", elapsed.count()}});
    }
    return {{"paced", paced}, {"ack_interval_ms", s7::stream::kAckInterval.count()}, {"records", std::move(records)}};
}
} // namespace

int main()
try {
    s7::register_builtin_elements(s7::ElementRegistry::instance());
    s7::register_net_elements(s7::ElementRegistry::instance());
    bool refused = false;
    try { static_cast<void>(compile(s7::deployment::diagram_schema(diagram(false, false, false, true)))); }
    catch (const std::invalid_argument&) { refused = true; }
    require(refused, "unpartitioned delay-free cycle must be refused");
    json::array results;
    // One negative baseline per shape; sweep resources/speed on candidates.
    for (unsigned fixture = 0; fixture < 4; ++fixture) {
        const std::string name = std::array{"memory_A", "memory_B", "nested_memory", "independent_bidirectional"}[fixture];
        results.emplace_back(run(name, fixture == 1, fixture == 2, fixture == 3, false, 1, false));
        for (const unsigned window : {1U, 8U, 64U})
            for (const bool slow : {false, true})
                results.emplace_back(run(name, fixture == 1, fixture == 2, fixture == 3, true, window, slow));
    }
    // Timing control for the credit-progress finding, not a proposed fix.
    for (const unsigned window : {1U, 8U})
        results.emplace_back(run("memory_A_paced_ack_control", false, false, false, true, window, 55));
    json::array credits{credit_probe(false), credit_probe(true)};
    std::cout << json::serialize(json::object{{"engine", "production_runtime_in_memory_transport"},
        {"steps", kSteps}, {"delay_free_reference_rejected", refused}, {"credit_probes", std::move(credits)},
        {"runs", std::move(results)}}) << '\n';
    return 0;
} catch (const std::exception& ex) {
    std::cerr << "cross-node scheduling spike: " << ex.what() << '\n';
    return 1;
}
