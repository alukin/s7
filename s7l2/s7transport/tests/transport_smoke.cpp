#include <cassert>
#include <chrono>
#include <cstdint>
#include <stdexcept>
#include <thread>

#include "s7/transport.hpp"

int main()
{
    {
        s7::transport::InMemoryRuntimeNetEndpoint endpoint;

        const auto tx = endpoint.open_endpoint({
            .endpoint_name = "tx",
            .stream_name = "demo.stream",
            .allow_publish = true,
            .allow_receive = false,
        });
        assert(tx.ok);

        const auto rx = endpoint.open_endpoint({
            .endpoint_name = "rx",
            .stream_name = "demo.stream",
            .allow_publish = false,
            .allow_receive = true,
        });
        assert(rx.ok);

        s7::transport::DataPacket outgoing;
        outgoing.stream_name = "demo.stream";
        outgoing.sequence = 1;
        outgoing.stream_id = 42;
        outgoing.channel_id = 7;
        outgoing.data_type = 0x000C;
        outgoing.elem_count = 1;
        outgoing.t_model_ns = 1'000'000;
        outgoing.t_system_ns = 2'000'000;
        outgoing.extensions = {0xAA, 0x55};
        outgoing.payload = s7::transport::encode_f64_payload(12.5);

        const auto frame = s7::transport::encode_packet_v1(outgoing);
        const auto decoded = s7::transport::decode_packet_v1(frame);
        assert(decoded.sequence == outgoing.sequence);
        assert(decoded.stream_id == outgoing.stream_id);
        assert(decoded.channel_id == outgoing.channel_id);
        assert(decoded.data_type == outgoing.data_type);
        assert(decoded.elem_count == outgoing.elem_count);
        assert(decoded.t_model_ns == outgoing.t_model_ns);
        assert(decoded.t_system_ns.has_value());
        assert(decoded.t_system_ns.value() == outgoing.t_system_ns.value());
        assert(decoded.extensions.size() == outgoing.extensions.size());
        assert(s7::transport::decode_f64_payload(decoded.payload) == 12.5);

        auto malformed = frame;
        // Force payload_size in the v1 header to exceed the frame length.
        malformed[30] = 0xFF;
        malformed[31] = 0xFF;
        malformed[32] = 0xFF;
        malformed[33] = 0xFF;
        bool rejected_malformed = false;
        try {
            static_cast<void>(s7::transport::decode_packet_v1(malformed));
        } catch (const std::invalid_argument&) {
            rejected_malformed = true;
        }
        assert(rejected_malformed);

        const auto send = endpoint.publish(tx.handle, outgoing);
        assert(send.ok);

        s7::transport::DataPacket incoming;
        const auto receive = endpoint.receive(rx.handle, incoming);
        assert(receive.ok);
        assert(receive.has_packet);
        assert(incoming.sequence == 1);
        assert(incoming.payload.size() == 8);
        assert(s7::transport::decode_f64_payload(incoming.payload) == 12.5);
    }

    {
        constexpr std::uint16_t port = 38291;
        s7::transport::BeastRuntimeNetEndpoint server;
        s7::transport::BeastRuntimeNetEndpoint client;
        server.configure_server(port);
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        client.configure_client("ws://127.0.0.1:38291");

        const auto rx = server.open_endpoint({
            .endpoint_name = "server-rx",
            .stream_name = "beast.loopback",
            .allow_publish = false,
            .allow_receive = true,
        });
        assert(rx.ok);

        const auto tx = client.open_endpoint({
            .endpoint_name = "client-tx",
            .stream_name = "beast.loopback",
            .allow_publish = true,
            .allow_receive = false,
        });
        assert(tx.ok);

        s7::transport::DataPacket outgoing;
        outgoing.stream_name = "beast.loopback";
        outgoing.sequence = 5;
        outgoing.stream_id = 77;
        outgoing.channel_id = 3;
        outgoing.data_type = 0x000C;
        outgoing.elem_count = 1;
        outgoing.t_model_ns = 5'000'000;
        outgoing.payload = s7::transport::encode_f64_payload(9.75);

        const auto send = client.publish(tx.handle, outgoing);
        assert(send.ok);

        s7::transport::DataPacket incoming;
        bool received = false;
        for (int attempt = 0; attempt < 100; ++attempt) {
            const auto receive = server.receive(rx.handle, incoming);
            assert(receive.ok);
            if (receive.has_packet) {
                received = true;
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }

        if (!received) {
            return 2;
        }
        assert(incoming.sequence == 5);
        assert(incoming.stream_id == 77);
        assert(incoming.channel_id == 3);
        assert(s7::transport::decode_f64_payload(incoming.payload) == 9.75);
    }

    return 0;
}


