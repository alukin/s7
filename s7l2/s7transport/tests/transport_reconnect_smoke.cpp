#include <cassert>
#include <chrono>
#include <cstdint>
#include <thread>

#include "s7/transport.hpp"

int main()
{
    constexpr std::uint16_t port = 38299;
    constexpr int reconnect_rounds = 10;
    constexpr int poll_attempts = 120;

    s7::transport::BeastRuntimeNetEndpoint server;
    s7::transport::BeastRuntimeNetEndpoint client;

    server.configure_server(port);
    std::this_thread::sleep_for(std::chrono::milliseconds(60));
    client.configure_client("ws://127.0.0.1:38299");

    const auto rx = server.open_endpoint({
        .endpoint_name = "server-rx",
        .stream_name = "reconnect.test",
        .allow_publish = false,
        .allow_receive = true,
    });
    assert(rx.ok);

    const auto tx = client.open_endpoint({
        .endpoint_name = "client-tx",
        .stream_name = "reconnect.test",
        .allow_publish = true,
        .allow_receive = false,
    });
    assert(tx.ok);

    auto send_and_expect = [&](const std::uint64_t sequence, const double value) {
        s7::transport::DataPacket outgoing;
        outgoing.stream_name = "reconnect.test";
        outgoing.sequence = sequence;
        outgoing.stream_id = 100 + sequence;
        outgoing.channel_id = static_cast<std::uint32_t>(sequence);
        outgoing.data_type = s7::transport::kPrototypeDataTypeF64;
        outgoing.elem_count = 1;
        outgoing.t_model_ns = 1'000'000 + (sequence * 1'000);
        outgoing.payload = s7::transport::encode_f64_payload(value);

        const auto send = client.publish(tx.handle, outgoing);
        assert(send.ok);

        s7::transport::DataPacket incoming;
        bool received = false;
        for (int attempt = 0; attempt < poll_attempts; ++attempt) {
            const auto receive = server.receive(rx.handle, incoming);
            assert(receive.ok);
            if (receive.has_packet) {
                received = true;
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }

        assert(received);
        assert(incoming.sequence == sequence);
        assert(s7::transport::decode_f64_payload(incoming.payload) == value);
    };

    send_and_expect(1, 1.0);

    for (int round = 0; round < reconnect_rounds; ++round) {
        client.configure_client("ws://127.0.0.1:38299");
        std::this_thread::sleep_for(std::chrono::milliseconds(80));

        const auto sequence = static_cast<std::uint64_t>(round + 2);
        send_and_expect(sequence, static_cast<double>(sequence));
    }

    return 0;
}


