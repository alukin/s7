#include <cassert>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include <boost/asio/connect.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/beast/websocket/stream.hpp>

#include "s7/transport.hpp"

namespace {

namespace transport = s7::transport;
using Clock = std::chrono::steady_clock;

void expect(const bool condition, const std::string& what)
{
    if (!condition) {
        throw std::runtime_error("Check failed: " + what);
    }
}

template <typename Predicate>
bool wait_until(Predicate predicate, const std::chrono::milliseconds timeout = std::chrono::milliseconds(2000))
{
    const auto deadline = Clock::now() + timeout;
    while (Clock::now() < deadline) {
        if (predicate()) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return predicate();
}

transport::DataPacket make_packet(const std::string& stream, const std::uint64_t sequence, const std::size_t elements = 1)
{
    transport::DataPacket packet;
    packet.stream_name = stream;
    packet.sequence = sequence;
    packet.t_model_ns = sequence;
    packet.elem_count = static_cast<std::uint32_t>(elements);
    packet.payload.assign(elements * 8, 0);
    const auto value = transport::encode_f64_payload(static_cast<double>(sequence));
    std::copy(value.begin(), value.end(), packet.payload.begin());
    return packet;
}

transport::EndpointHandle open(transport::IRuntimeNetEndpoint& endpoint, const std::string& stream, const bool publish)
{
    const auto result = endpoint.open_endpoint({.endpoint_name = stream, .stream_name = stream, .allow_publish = publish, .allow_receive = !publish});
    expect(result.ok, "open endpoint " + stream);
    return result.handle;
}

std::vector<std::uint64_t> drain(transport::IRuntimeNetEndpoint& endpoint, const transport::EndpointHandle& handle)
{
    std::vector<std::uint64_t> sequences;
    transport::DataPacket packet;
    while (true) {
        const auto result = endpoint.receive(handle, packet);
        expect(result.ok, "receive");
        if (!result.has_packet) {
            return sequences;
        }
        sequences.push_back(packet.sequence);
    }
}

std::string url(const std::uint16_t port)
{
    return "ws://127.0.0.1:" + std::to_string(port);
}

void test_two_peers_coexist()
{
    constexpr std::uint16_t port = 38421;
    transport::BeastRuntimeNetEndpoint server;
    server.configure_server(port);
    std::this_thread::sleep_for(std::chrono::milliseconds(30));

    transport::BeastRuntimeNetEndpoint client_a;
    transport::BeastRuntimeNetEndpoint client_b;
    client_a.configure_client(url(port));
    client_b.configure_client(url(port));
    expect(wait_until([&] { return server.stats().data_connections == 2 && server.stats().control_connections == 2; }),
           "server keeps both peers");

    // Both peers deliver to the server.
    const auto server_rx = open(server, "up", false);
    expect(client_a.publish(open(client_a, "up", true), make_packet("up", 1)).delivered, "peer A publishes");
    expect(client_b.publish(open(client_b, "up", true), make_packet("up", 2)).delivered, "peer B publishes");
    std::vector<std::uint64_t> received;
    expect(wait_until([&] {
               for (const auto sequence : drain(server, server_rx)) {
                   received.push_back(sequence);
               }
               return received.size() == 2;
           }),
           "server receives from both peers");

    // The server fans a published packet out to every connected peer.
    const auto rx_a = open(client_a, "down", false);
    const auto rx_b = open(client_b, "down", false);
    const auto sent = server.publish(open(server, "down", true), make_packet("down", 7));
    expect(sent.ok && sent.delivered, "server publishes");
    expect(wait_until([&] { return server.stats().packets_sent == 2; }), "packet written to both peers");
    expect(wait_until([&] { return drain(client_a, rx_a) == std::vector<std::uint64_t>{7}; }), "peer A receives");
    expect(wait_until([&] { return drain(client_b, rx_b) == std::vector<std::uint64_t>{7}; }), "peer B receives");

    // Both peers still answer control requests.
    server.set_control_request_handler([](const transport::ControlPeer&, const std::string&) { return std::string{"{\"type\":\"PONG\"}"}; });
    expect(client_a.request_control_json("{}") == "{\"type\":\"PONG\"}", "control on peer A");
    expect(client_b.request_control_json("{}") == "{\"type\":\"PONG\"}", "control on peer B");
}

void test_inbound_connection_limit()
{
    constexpr std::uint16_t port = 38422;
    transport::BeastRuntimeNetEndpoint server({.max_inbound_connections = 2});
    server.configure_server(port);
    std::this_thread::sleep_for(std::chrono::milliseconds(30));

    transport::BeastRuntimeNetEndpoint first;
    first.configure_client(url(port)); // control + data: uses both slots
    expect(wait_until([&] { return server.stats().data_connections == 1; }), "first peer accepted");

    bool refused = false;
    try {
        transport::BeastRuntimeNetEndpoint second;
        second.configure_client(url(port));
    } catch (const std::exception&) {
        refused = true;
    }
    expect(refused, "connection beyond the limit is refused");
    expect(server.stats().connections_rejected >= 1, "rejection is counted");
    expect(server.stats().data_connections == 1, "existing peer is kept");
}

void test_receive_limits()
{
    constexpr std::uint16_t port = 38423;
    transport::BeastRuntimeNetEndpoint server({.max_receive_queue_packets = 8, .max_receive_streams = 2});
    server.configure_server(port);
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    transport::BeastRuntimeNetEndpoint client;
    client.configure_client(url(port));

    // A producer outrunning the consumer keeps only the newest packets per stream.
    const auto tx = open(client, "fast", true);
    for (std::uint64_t sequence = 0; sequence < 100; ++sequence) {
        expect(client.publish(tx, make_packet("fast", sequence)).delivered, "publish fast");
    }
    expect(wait_until([&] { return server.stats().packets_received == 100; }), "all packets arrive");
    const auto stats = server.stats();
    expect(stats.receive_dropped_queue_full == 92, "92 oldest packets dropped, got " + std::to_string(stats.receive_dropped_queue_full));
    std::vector<std::uint64_t> newest;
    for (std::uint64_t sequence = 92; sequence < 100; ++sequence) {
        newest.push_back(sequence);
    }
    expect(drain(server, open(server, "fast", false)) == newest, "the newest 8 packets remain, in order");

    // The number of distinct queued streams is bounded too.
    for (const auto* stream : {"s1", "s2", "s3"}) {
        expect(client.publish(open(client, stream, true), make_packet(stream, 1)).delivered, "publish stream");
    }
    expect(wait_until([&] { return server.stats().packets_received == 103; }), "stream packets arrive");
    expect(server.stats().receive_dropped_stream_limit == 1, "third stream refused at the stream limit");
}

void test_slow_peer_does_not_block()
{
    namespace websocket = boost::beast::websocket;
    using Tcp = boost::asio::ip::tcp;

    constexpr std::uint16_t port = 38424;
    transport::BeastRuntimeNetEndpoint server({.max_send_queue_packets = 4});
    server.configure_server(port);
    std::this_thread::sleep_for(std::chrono::milliseconds(30));

    // A peer that connects to /data and never reads.
    boost::asio::io_context io;
    Tcp::resolver resolver(io);
    websocket::stream<Tcp::socket> stalled(io);
    boost::asio::connect(stalled.next_layer(), resolver.resolve("127.0.0.1", std::to_string(port)));
    stalled.handshake("127.0.0.1:" + std::to_string(port), "/data");
    expect(wait_until([&] { return server.stats().data_connections == 1; }), "stalled peer connected");

    // 200 packets of 256 KiB: far more than the socket buffers hold.
    const auto tx = open(server, "bulk", true);
    const auto packet = make_packet("bulk", 0, 32768);
    const auto started = Clock::now();
    for (std::uint64_t sequence = 0; sequence < 200; ++sequence) {
        auto copy = packet;
        copy.sequence = sequence;
        const auto result = server.publish(tx, copy);
        expect(result.ok && result.delivered, "publish to stalled peer");
        expect(result.queue_depth <= 4, "send queue stays bounded");
    }
    const auto elapsed = Clock::now() - started;
    expect(elapsed < std::chrono::seconds(2), "publish does not block on a stalled peer");
    expect(server.stats().send_dropped_queue_full > 0, "overflow is counted");
}

void test_no_peer()
{
    transport::BeastRuntimeNetEndpoint lonely;
    const auto result = lonely.publish(open(lonely, "nobody", true), make_packet("nobody", 0));
    expect(result.ok && !result.delivered, "publishing without a peer drops the packet");
    expect(lonely.stats().send_dropped_no_peer == 1, "drop without peer is counted");
}

void test_in_memory_limits()
{
    transport::InMemoryRuntimeNetEndpoint endpoint;
    const auto tx = open(endpoint, "mem", true);
    for (std::uint64_t sequence = 0; sequence < 1100; ++sequence) {
        expect(endpoint.publish(tx, make_packet("mem", sequence)).ok, "in-memory publish");
    }
    expect(endpoint.stats().receive_dropped_queue_full == 76, "in-memory queue bounded at 1024");
}

} // namespace

int main()
try {
    test_two_peers_coexist();
    test_inbound_connection_limit();
    test_receive_limits();
    test_slow_peer_does_not_block();
    test_no_peer();
    test_in_memory_limits();
    return 0;
} catch (const std::exception& ex) {
    std::cerr << "s7transport multipeer smoke: " << ex.what() << '\n';
    return 1;
}
