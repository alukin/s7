#include <chrono>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "s7/net.hpp"

namespace {

using s7::SignalType;
namespace json = boost::json;

void expect(const bool condition, const std::string& what)
{
    if (!condition) {
        throw std::runtime_error("Check failed: " + what);
    }
}

void add(s7::Schema& schema, const std::string& id, const std::string& type, json::object params = {})
{
    const auto* descriptor = s7::ElementRegistry::instance().find(type);
    expect(descriptor != nullptr, "registered: " + type);
    schema.add_element(s7::make_element_spec(id, *descriptor, std::move(params)));
}

constexpr const char* kStream = "8a5e7c3b-1d2f-4a6b-9c8d-7e6f5a4b3c2d";

} // namespace

int main()
try {
    auto& registry = s7::ElementRegistry::instance();
    s7::register_builtin_elements(registry);
    s7::register_net_elements(registry);
    expect(registry.find("NetSink")->metadata().category == "network", "network category");
    expect(!registry.find("NetSource")->metadata().type_variables.empty(), "network elements are generic");

    {
        // NetSink publishes every step's value to the stream.
        auto endpoint = std::make_shared<s7::transport::InMemoryRuntimeNetEndpoint>();
        s7::RuntimeNetContext::set_endpoint(endpoint);
        const auto config = s7::net_stream_config(kStream);
        const auto consumer = endpoint->open_consumer(config);

        s7::Schema schema{"sink"};
        schema.add_element("source", [] { return std::make_unique<s7::Constant>(7.25); });
        schema.add_element("sink", [config] { return std::make_unique<s7::NetSink>(config); });
        schema.connect("source", 0, "sink", 0);
        s7::NodeRuntime runtime{s7::Compiler::compile(schema)};
        for (int n = 0; n < 3; ++n) {
            runtime.step();
        }
        std::vector<double> values(3);
        const auto result = endpoint->receive(consumer, values.data(), values.size());
        expect(result.received == 3 && values == std::vector<double>{7.25, 7.25, 7.25}, "NetSink values arrive");
        s7::RuntimeNetContext::set_endpoint(nullptr);
    }

    {
        // Typed graph to graph: the stream type is inferred on both ends (I16).
        auto endpoint = std::make_shared<s7::transport::InMemoryRuntimeNetEndpoint>();
        s7::RuntimeNetContext::set_endpoint(endpoint);

        s7::Schema producer{"producer"};
        add(producer, "x", "Constant", {{"type", "I16"}, {"value", -1234}});
        add(producer, "out", "NetSink", {{"stream_id", kStream}, {"max_elements_per_packet", 4}});
        producer.connect("x", 0, "out", 0);

        s7::Schema consumer{"consumer"};
        add(consumer, "in", "NetSource", {{"stream_id", kStream}});
        add(consumer, "double_it", "Gain", {{"type", "I16"}, {"factor", 2}});
        consumer.connect("in", 0, "double_it", 0);

        s7::NodeRuntime sending{s7::Compiler::compile(producer)};
        s7::NodeRuntime receiving{s7::Compiler::compile(consumer)};
        for (int n = 0; n < 5; ++n) {
            sending.step();
            receiving.step(); // waits for the partial batch, which is flushed for it
            expect(receiving.output_of<SignalType::I16>("in") == -1234, "I16 value received");
            expect(receiving.output_of<SignalType::I16>("double_it") == -2468, "received value used downstream");
        }
        expect(receiving.output_type_of("in") == SignalType::I16, "NetSource type inferred from downstream");
        s7::RuntimeNetContext::set_endpoint(nullptr);
    }

    {
        // A consumer waits for the producer, which publishes later from another thread.
        auto endpoint = std::make_shared<s7::transport::InMemoryRuntimeNetEndpoint>();
        s7::RuntimeNetContext::set_endpoint(endpoint);
        auto config = s7::net_stream_config(kStream);
        const auto producer = endpoint->open_producer(config);
        s7::Schema schema{"waiting"};
        schema.add_element("in", [config] { return std::make_unique<s7::NetSource>(config); });
        s7::NodeRuntime runtime{s7::Compiler::compile(schema)};
        std::thread later([&] {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            const double value = 42.0;
            static_cast<void>(endpoint->publish(producer, &value, 1));
        });
        try {
            runtime.step();
        } catch (...) {
            later.join();
            throw;
        }
        later.join();
        expect(runtime.real_output_of("in") == 42.0, "NetSource waits for data instead of inventing a value");
        s7::RuntimeNetContext::set_endpoint(nullptr);
    }

    {
        // No data within max_wait: the stream faults and the step throws (no invented value).
        auto endpoint = std::make_shared<s7::transport::InMemoryRuntimeNetEndpoint>();
        s7::RuntimeNetContext::set_endpoint(endpoint);
        s7::Schema schema{"starved"};
        add(schema, "in", "NetSource", {{"stream_id", kStream}, {"max_wait_ms", 30}});
        s7::NodeRuntime runtime{s7::Compiler::compile(schema)};
        std::string error;
        try {
            runtime.step();
        } catch (const std::runtime_error& ex) {
            error = ex.what();
        }
        expect(error.find("UNDERRUN") != std::string::npos, "underrun faults the step: " + error);
        expect(endpoint->stream_statuses().front().state == "faulted", "stream status shows the fault");
        s7::RuntimeNetContext::set_endpoint(nullptr);
    }

    {
        s7::Schema schema{"no_id"};
        add(schema, "out", "NetSink");
        add(schema, "x", "Constant");
        schema.connect("x", 0, "out", 0);
        const auto errors = s7::Validator::validate(schema).errors;
        expect(!errors.empty() && errors.front().find("stream_id") != std::string::npos, "stream_id is required");
    }
    return 0;
} catch (const std::exception& ex) {
    std::cerr << "s7net smoke: " << ex.what() << '\n';
    return 1;
}
