// Splitting one diagram across nodes: placement, stream assignment and the
// part each node runs.
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

#include "s7/diagram_deployment.hpp"
#include "s7/net.hpp"

namespace {

namespace json = boost::json;
namespace deployment = s7::deployment;
using s7::SignalType;

void expect(const bool condition, const std::string& what)
{
    if (!condition) {
        throw std::runtime_error("Check failed: " + what);
    }
}

constexpr const char* kA = "aaaaaaaa-0000-4000-8000-000000000001";
constexpr const char* kB = "bbbbbbbb-0000-4000-8000-000000000002";
constexpr const char* kC = "cccccccc-0000-4000-8000-000000000003";

// x (I16 constant, node A) feeds a Gain on A, B and C; B's gain feeds another Gain on B.
json::object make_diagram()
{
    return json::parse(R"json({
        "document_kind": "s7.diagram",
        "runtime_nodes": [
            { "node_uuid": "aaaaaaaa-0000-4000-8000-000000000001", "name": "a", "endpoint": "ws://127.0.0.1:9101" },
            { "node_uuid": "bbbbbbbb-0000-4000-8000-000000000002", "name": "b", "endpoint": "ws://127.0.0.1:9102" },
            { "node_uuid": "cccccccc-0000-4000-8000-000000000003", "name": "c", "endpoint": "ws://127.0.0.1:9103" }
        ],
        "schema": {
            "name": "split",
            "elements": [
                { "id": "x", "type": "Constant", "params": { "type": "I16", "value": -1234 }, "node": "aaaaaaaa-0000-4000-8000-000000000001" },
                { "id": "local", "type": "Gain", "params": { "factor": 1 }, "node": "aaaaaaaa-0000-4000-8000-000000000001" },
                { "id": "g", "type": "Gain", "params": { "factor": 2 }, "node": "bbbbbbbb-0000-4000-8000-000000000002" },
                { "id": "out", "type": "Gain", "params": { "factor": 1 }, "node": "bbbbbbbb-0000-4000-8000-000000000002" },
                { "id": "h", "type": "Gain", "params": { "factor": 3 }, "node": "cccccccc-0000-4000-8000-000000000003" }
            ],
            "connections": [
                { "from_element": "x", "from_port": 0, "to_element": "local", "to_port": 0 },
                { "from_element": "x", "from_port": 0, "to_element": "g", "to_port": 0 },
                { "from_element": "x", "from_port": 0, "to_element": "h", "to_port": 0 },
                { "from_element": "g", "from_port": 0, "to_element": "out", "to_port": 0 }
            ]
        },
        "diagram": { "version": 1, "nodes": [], "wires": [] }
    })json").as_object();
}

s7::Schema build(const json::object& schema_json)
{
    auto& registry = s7::ElementRegistry::instance();
    s7::Schema schema{std::string(schema_json.at("name").as_string())};
    for (const auto& value : schema_json.at("elements").as_array()) {
        const auto& element = value.as_object();
        const auto* descriptor = registry.find(std::string(element.at("type").as_string()));
        expect(descriptor != nullptr, "registered type");
        const auto* params = element.if_contains("params");
        schema.add_element(s7::make_element_spec(std::string(element.at("id").as_string()), *descriptor,
                                                 params ? params->as_object() : json::object{}));
    }
    for (const auto& value : schema_json.at("connections").as_array()) {
        const auto& c = value.as_object();
        schema.connect(std::string(c.at("from_element").as_string()), c.at("from_port").to_number<std::size_t>(),
                       std::string(c.at("to_element").as_string()), c.at("to_port").to_number<std::size_t>());
    }
    return schema;
}

// Output port types from inference over the whole diagram (what Studio does).
deployment::OutputTypeLookup infer(const json::object& diagram)
{
    const auto schema = build(deployment::diagram_schema(diagram));
    auto resolved = s7::TypeResolver::resolve(schema);
    expect(resolved.ok(), "diagram types resolve");
    auto types = std::make_shared<std::unordered_map<std::string, std::vector<SignalType>>>();
    for (std::size_t i = 0; i < schema.elements().size(); ++i) {
        for (const auto& port : resolved.factories[i]()->output_ports()) {
            (*types)[schema.elements()[i].id].push_back(port.type);
        }
    }
    return [types](const std::string& element, const std::size_t port) -> std::optional<SignalType> {
        const auto it = types->find(element);
        if (it == types->end() || port >= it->second.size()) {
            return std::nullopt;
        }
        return it->second[port];
    };
}

bool has_element(const json::object& schema, const std::string& id)
{
    for (const auto& element : schema.at("elements").as_array()) {
        if (element.as_object().at("id").as_string() == id) {
            return true;
        }
    }
    return false;
}

void test_assignment()
{
    auto diagram = make_diagram();
    expect(deployment::placed_nodes(diagram) == std::vector<std::string>{kA, kB, kC}, "placed nodes in order");
    expect(deployment::assign_streams(diagram, infer(diagram)).empty(), "streams assigned");
    auto streams = deployment::parse_streams(diagram);
    expect(streams.size() == 1, "one stream per producing port (fan-out to B and C)");
    const auto& stream = streams.front();
    expect(stream.producer == deployment::StreamEndpoint{kA, "x", 0}, "producer endpoint");
    expect(stream.consumers.size() == 2 && stream.consumers[0] == deployment::StreamEndpoint{kB, "g", 0} &&
               stream.consumers[1] == deployment::StreamEndpoint{kC, "h", 0},
           "consumer endpoints");
    expect(stream.data_type == SignalType::I16, "the stream has the producing port's inferred type");

    // Redeploying keeps the stream id and its settings.
    const auto id = stream.stream_id;
    diagram["streams"].as_array()[0].as_object()["max_batch_latency_ms"] = 3;
    expect(deployment::assign_streams(diagram, infer(diagram)).empty(), "reassigned");
    streams = deployment::parse_streams(diagram);
    expect(streams.front().stream_id == id && streams.front().max_batch_latency_ms == 3, "id and settings kept");

    // Another type, or the producer on another node, is a new stream.
    diagram["schema"].as_object()["elements"].as_array()[0].as_object()["params"].as_object()["type"] = "I32";
    expect(deployment::assign_streams(diagram, infer(diagram)).empty(), "reassigned after a type change");
    const auto retyped = deployment::parse_streams(diagram).front();
    expect(retyped.stream_id != id && retyped.data_type == SignalType::I32, "type change: new stream");
    auto& elements = diagram["schema"].as_object()["elements"].as_array();
    elements[0].as_object()["node"] = kB;
    expect(deployment::assign_streams(diagram, infer(diagram)).empty(), "reassigned after moving the producer");
    const auto moved = deployment::parse_streams(diagram);
    expect(!moved.empty() && moved.front().stream_id != retyped.stream_id, "moved producer: new stream");

    // Placement problems are reported and leave the diagram unchanged.
    elements[1].as_object().erase("node");
    elements[2].as_object()["node"] = "dddddddd-0000-4000-8000-000000000004";
    const auto before = json::serialize(diagram);
    const auto errors = deployment::assign_streams(diagram, infer(diagram));
    expect(errors.size() == 2, "unplaced element and unknown node reported");
    expect(json::serialize(diagram) == before, "diagram unchanged on error");
}

void test_node_parts()
{
    auto diagram = make_diagram();
    bool rejected = false;
    try {
        static_cast<void>(deployment::extract_node_schema(diagram, kB));
    } catch (const std::invalid_argument& ex) {
        rejected = std::string(ex.what()).find("without a stream") != std::string::npos;
    }
    expect(rejected, "a connection between nodes needs a stream");

    expect(deployment::assign_streams(diagram, infer(diagram)).empty(), "streams assigned");
    const auto stream = deployment::parse_streams(diagram).front();
    const auto sink = "__stream_" + s7::wire::to_string(stream.stream_id) + "_out";
    const auto source = "__stream_" + s7::wire::to_string(stream.stream_id) + "_in";

    const auto a = deployment::extract_node_schema(diagram, kA);
    expect(a.at("elements").as_array().size() == 3 && has_element(a, "x") && has_element(a, "local") && has_element(a, sink),
           "A runs x, local and a NetSink: " + json::serialize(a));
    expect(!a.at("elements").as_array()[0].as_object().contains("node"), "placement is not part of the runtime schema");
    const auto b = deployment::extract_node_schema(diagram, kB);
    expect(b.at("elements").as_array().size() == 3 && has_element(b, "g") && has_element(b, "out") && has_element(b, source),
           "B runs g, out and a NetSource: " + json::serialize(b));
    expect(deployment::producer_endpoints(diagram, kB) == std::vector<std::string>{"ws://127.0.0.1:9101"}, "B connects to A");
    expect(deployment::producer_endpoints(diagram, kA).empty(), "A consumes nothing");

    // Run A and B in one process: B's graph computes from A's values.
    auto endpoint = std::make_shared<s7::transport::InMemoryRuntimeNetEndpoint>();
    s7::RuntimeNetContext::set_endpoint(endpoint);
    s7::NodeRuntime node_a{s7::Compiler::compile(build(a))};
    s7::NodeRuntime node_b{s7::Compiler::compile(build(b))};
    for (int step = 0; step < 3; ++step) {
        node_a.step();
        node_b.step();
        expect(node_b.output_of<SignalType::I16>("out") == -2468, "B computes from A's stream");
    }
    expect(node_b.output_type_of(source) == SignalType::I16, "NetSource pinned to the stream type");
    s7::RuntimeNetContext::set_endpoint(nullptr);
}

} // namespace

int main()
try {
    s7::register_builtin_elements(s7::ElementRegistry::instance());
    s7::register_net_elements(s7::ElementRegistry::instance());
    test_assignment();
    test_node_parts();
    return 0;
} catch (const std::exception& ex) {
    std::cerr << "s7net diagram deployment smoke: " << ex.what() << '\n';
    return 1;
}
