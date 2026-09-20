#include <cassert>
#include <chrono>
#include <complex>
#include <cstdint>
#include <exception>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <thread>

#include "s7/net.hpp"

int main() try
{
    {
        auto& registry = s7::ElementRegistry::instance();
        s7::register_net_elements(registry);

        const auto* netSinkDescriptor = registry.find("NetSink");
        const auto* netSourceDescriptor = registry.find("NetSource");
        if (!netSinkDescriptor || !netSourceDescriptor) {
            throw std::runtime_error("Network elements were not registered in the element registry.");
        }
        assert(netSinkDescriptor->metadata().category == "network");
        assert(netSourceDescriptor->metadata().category == "network");
    }

    {
        auto endpoint = std::make_shared<s7::transport::InMemoryRuntimeNetEndpoint>();
        s7::RuntimeNetContext::set_endpoint(endpoint);

        const auto rx = endpoint->open_endpoint({
            .endpoint_name = "net-smoke-rx",
            .stream_name = "demo.out",
            .allow_publish = false,
            .allow_receive = true,
        });
        if (!rx.ok) {
            throw std::runtime_error("Failed to open test receive endpoint: " + rx.error);
        }

        s7::Schema schema{"transport_net_sink"};
        schema.add_element("source", [] { return std::make_unique<s7::Constant>(7.25); });
        schema.add_element("sink", [] { return std::make_unique<s7::NetSink>("demo.out"); });
        schema.connect("source", 0, "sink", 0);

        s7::NodeRuntime runtime{s7::Compiler::compile(schema)};
        runtime.step();

        s7::transport::DataPacket packet;
        const auto receive = endpoint->receive(rx.handle, packet);
        if (!receive.ok || !receive.has_packet) {
            throw std::runtime_error("NetSink did not publish a packet through runtime endpoint.");
        }

        assert(packet.stream_name == "demo.out");
        assert(packet.sequence == 0);
        const auto decoded_value = s7::transport::decode_f64_payload(packet.payload);
        if (std::abs(decoded_value - 7.25) >= 1e-12) {
            throw std::runtime_error("NetSink payload value mismatch.");
        }

        s7::RuntimeNetContext::set_endpoint(nullptr);
    }

    {
        auto endpoint = std::make_shared<s7::transport::InMemoryRuntimeNetEndpoint>();
        s7::RuntimeNetContext::set_endpoint(endpoint);

        const auto tx = endpoint->open_endpoint({
            .endpoint_name = "net-smoke-tx",
            .stream_name = "demo.in",
            .allow_publish = true,
            .allow_receive = false,
        });
        if (!tx.ok) {
            throw std::runtime_error("Failed to open test publish endpoint: " + tx.error);
        }

        s7::transport::DataPacket packet;
        packet.stream_name = "demo.in";
        packet.sequence = 1;
        packet.data_type = 0x000C;
        packet.elem_count = 1;
        packet.t_model_ns = 1;
        packet.payload = s7::transport::encode_f64_payload(5.5);
        const auto send = endpoint->publish(tx.handle, packet);
        if (!send.ok) {
            throw std::runtime_error("Failed to seed NetSource packet: " + send.error);
        }

        s7::Schema schema{"transport_net_source"};
        schema.add_element("source", [] { return std::make_unique<s7::NetSource>("demo.in"); });

        s7::NodeRuntime runtime{s7::Compiler::compile(schema)};
        runtime.step();
        assert(std::abs(runtime.real_output_of("source") - 5.5) < 1e-12);

        s7::RuntimeNetContext::set_endpoint(nullptr);
    }

    {
        s7::Schema sink_schema{"net_sink_empty_topic"};
        sink_schema.add_element("source", [] { return std::make_unique<s7::Constant>(1.0); });
        sink_schema.add_element("sink", [] { return std::make_unique<s7::NetSink>(""); });
        sink_schema.connect("source", 0, "sink", 0);

        s7::NodeRuntime runtime{s7::Compiler::compile(sink_schema)};
        bool threw = false;
        try {
            runtime.step();
        } catch (const std::invalid_argument&) {
            threw = true;
        }
        if (!threw) {
            throw std::runtime_error("Expected NetSink with empty topic to fail fast.");
        }
    }

    {
        s7::Schema source_schema{"net_source_empty_topic"};
        source_schema.add_element("source", [] { return std::make_unique<s7::NetSource>(""); });

        s7::NodeRuntime runtime{s7::Compiler::compile(source_schema)};
        bool threw = false;
        try {
            runtime.step();
        } catch (const std::invalid_argument&) {
            threw = true;
        }
        if (!threw) {
            throw std::runtime_error("Expected NetSource with empty topic to fail fast.");
        }
    }

    return 0;
} catch (const std::exception& ex) {
    std::cerr << "s7net smoke: fatal error: " << ex.what() << '\n';
    return 1;
} catch (...) {
    std::cerr << "s7net smoke: unknown fatal error\n";
    return 1;
}

