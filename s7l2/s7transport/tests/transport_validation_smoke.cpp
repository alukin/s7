#include <cassert>
#include <chrono>
#include <cstdint>
#include <functional>
#include <iostream>
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

// Byte offsets in the 54-byte prototype header.
constexpr std::size_t kVersionOffset = 0;
constexpr std::size_t kFlagsOffset = 2;
constexpr std::size_t kDataTypeOffset = 24;
constexpr std::size_t kElemCountOffset = 26;
constexpr std::size_t kPayloadSizeOffset = 30;
constexpr std::size_t kSystemTimeOffset = 42;
constexpr std::size_t kExtLenOffset = 50;
constexpr std::size_t kReservedOffset = 52;
constexpr std::size_t kFixedHeaderBytes = 54;

void put_u16(std::vector<std::uint8_t>& frame, const std::size_t offset, const std::uint16_t value)
{
    frame[offset] = static_cast<std::uint8_t>(value >> 8);
    frame[offset + 1] = static_cast<std::uint8_t>(value & 0xFFu);
}

void put_u32(std::vector<std::uint8_t>& frame, const std::size_t offset, const std::uint32_t value)
{
    for (std::size_t index = 0; index < 4; ++index) {
        frame[offset + index] = static_cast<std::uint8_t>(value >> (24 - 8 * index));
    }
}

transport::DataPacket make_packet(const std::string& stream, const std::uint64_t sequence, const double value)
{
    transport::DataPacket packet;
    packet.stream_name = stream;
    packet.sequence = sequence;
    packet.elem_count = 1;
    packet.t_model_ns = sequence * 1'000'000ULL;
    packet.payload = transport::encode_f64_payload(value);
    return packet;
}

void expect_decode_rejected(const std::string& label, const std::vector<std::uint8_t>& frame)
{
    try {
        static_cast<void>(transport::decode_packet_prototype(frame));
    } catch (const std::invalid_argument&) {
        return;
    }
    throw std::runtime_error("Malformed frame was accepted: " + label);
}

void expect_encode_rejected(const std::string& label, const transport::DataPacket& packet)
{
    try {
        static_cast<void>(transport::encode_packet_prototype(packet));
    } catch (const std::invalid_argument&) {
        return;
    }
    throw std::runtime_error("Invalid packet was encoded: " + label);
}

bool poll_receive(transport::IRuntimeNetEndpoint& endpoint,
                  const transport::EndpointHandle& handle,
                  transport::DataPacket& packet)
{
    for (int attempt = 0; attempt < 100; ++attempt) {
        const auto result = endpoint.receive(handle, packet);
        assert(result.ok);
        if (result.has_packet) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return false;
}

std::vector<std::string> poll_errors(transport::IRuntimeNetEndpoint& endpoint)
{
    for (int attempt = 0; attempt < 100; ++attempt) {
        auto errors = endpoint.take_transport_errors();
        if (!errors.empty()) {
            return errors;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return {};
}

void test_model_time_zero()
{
    // A present model timestamp of zero round-trips intact.
    auto packet = make_packet("t0", 0, 1.5);
    packet.t_model_ns = 0;
    const auto decoded = transport::decode_packet_prototype(transport::encode_packet_prototype(packet));
    assert(decoded.t_model_ns.has_value());
    assert(*decoded.t_model_ns == 0);
    assert(!decoded.t_system_ns.has_value());

    // A system timestamp of zero is distinct from an absent one.
    packet.t_system_ns = 0;
    const auto with_system = transport::decode_packet_prototype(transport::encode_packet_prototype(packet));
    assert(with_system.t_system_ns.has_value());
    assert(*with_system.t_system_ns == 0);

    // A missing required model timestamp is rejected explicitly.
    auto missing = make_packet("t0", 0, 1.5);
    missing.t_model_ns.reset();
    expect_encode_rejected("missing model timestamp", missing);

    // The same rule applies to in-memory delivery.
    transport::InMemoryRuntimeNetEndpoint endpoint;
    const auto tx = endpoint.open_endpoint({.endpoint_name = "tx", .stream_name = "t0", .allow_publish = true, .allow_receive = false});
    assert(tx.ok);
    const auto rejected = endpoint.publish(tx.handle, missing);
    assert(!rejected.ok);
    assert(rejected.error.find("model timestamp") != std::string::npos);
}

void test_encoder_rejects_invalid_packets()
{
    auto unsupported_type = make_packet("enc", 1, 1.0);
    unsupported_type.data_type = 0x0001;
    expect_encode_rejected("unsupported data type", unsupported_type);

    auto count_mismatch = make_packet("enc", 1, 1.0);
    count_mismatch.elem_count = 2;
    expect_encode_rejected("element count mismatch", count_mismatch);

    auto partial_element = make_packet("enc", 1, 1.0);
    partial_element.elem_count = 0;
    partial_element.payload.push_back(0);
    expect_encode_rejected("payload not a whole number of elements", partial_element);

    auto unsupported_flags = make_packet("enc", 1, 1.0);
    unsupported_flags.flags = 0x0002;
    expect_encode_rejected("unsupported flags", unsupported_flags);

    auto flag_without_time = make_packet("enc", 1, 1.0);
    flag_without_time.flags = 0x0001;
    expect_encode_rejected("system-time flag without value", flag_without_time);

    auto oversized = make_packet("enc", 1, 1.0);
    oversized.payload.assign(256u * 1024u + 8u, 0);
    oversized.elem_count = static_cast<std::uint32_t>(oversized.payload.size() / 8);
    expect_encode_rejected("oversized payload", oversized);
}

void test_decoder_rejects_malformed_frames()
{
    const auto valid = transport::encode_packet_prototype(make_packet("dec", 3, 2.5));
    assert(transport::decode_packet_prototype(valid).sequence == 3);

    using Mutation = std::function<void(std::vector<std::uint8_t>&)>;
    const std::vector<std::pair<std::string, Mutation>> cases{
        {"empty frame", [](auto& f) { f.clear(); }},
        {"truncated header", [](auto& f) { f.resize(kFixedHeaderBytes - 1); }},
        {"truncated payload", [](auto& f) { f.pop_back(); }},
        {"trailing byte", [](auto& f) { f.push_back(0); }},
        {"unsupported version", [](auto& f) { put_u16(f, kVersionOffset, 2); }},
        {"unsupported flags", [](auto& f) { put_u16(f, kFlagsOffset, 0x0002); }},
        {"system time without flag", [](auto& f) { put_u16(f, kSystemTimeOffset + 6, 1); }},
        {"reserved field set", [](auto& f) { put_u16(f, kReservedOffset, 1); }},
        {"payload size beyond frame", [](auto& f) { put_u32(f, kPayloadSizeOffset, 0xFFFFFFFFu); }},
        {"payload size over limit", [](auto& f) { put_u32(f, kPayloadSizeOffset, 256u * 1024u + 8u); }},
        {"payload size smaller than frame", [](auto& f) { put_u32(f, kPayloadSizeOffset, 0); }},
        {"extension length beyond frame", [](auto& f) { put_u16(f, kExtLenOffset, 0xFFFF); }},
        {"extension length one byte", [](auto& f) {
             // Keep the total length consistent: ext_len 1 and payload 8 + (old ext - 1) bytes.
             put_u16(f, kExtLenOffset, 1);
             put_u32(f, kPayloadSizeOffset, static_cast<std::uint32_t>(f.size() - kFixedHeaderBytes - 1));
         }},
        {"stream name beyond extension area", [](auto& f) { put_u16(f, kFixedHeaderBytes, 0x00FF); }},
        {"unsupported data type", [](auto& f) { put_u16(f, kDataTypeOffset, 0x0001); }},
        {"element count mismatch", [](auto& f) { put_u32(f, kElemCountOffset, 2); }},
    };
    for (const auto& [label, mutate] : cases) {
        auto frame = valid;
        mutate(frame);
        expect_decode_rejected(label, frame);
    }
}

void send_raw_frame(const std::uint16_t port, const std::vector<std::uint8_t>& frame)
{
    namespace websocket = boost::beast::websocket;
    using Tcp = boost::asio::ip::tcp;

    boost::asio::io_context io;
    Tcp::resolver resolver(io);
    websocket::stream<Tcp::socket> ws(io);
    boost::asio::connect(ws.next_layer(), resolver.resolve("127.0.0.1", std::to_string(port)));
    ws.handshake("127.0.0.1:" + std::to_string(port), "/data");
    ws.binary(true);
    ws.write(boost::asio::buffer(frame));

    // The server closes the connection after a malformed frame.
    boost::beast::flat_buffer buffer;
    boost::system::error_code ec;
    ws.read(buffer, ec);
    assert(ec);
}

void test_receive_failure_handling()
{
    constexpr std::uint16_t port = 38295;
    transport::BeastRuntimeNetEndpoint server;
    server.configure_server(port);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    const auto rx = server.open_endpoint({.endpoint_name = "rx", .stream_name = "recv.test", .allow_publish = false, .allow_receive = true});
    assert(rx.ok);

    // A malformed frame is reported through the transport error channel, closes
    // that connection, and does not surface as a receive() failure on the stream.
    auto malformed = transport::encode_packet_prototype(make_packet("recv.test", 1, 1.0));
    malformed.push_back(0);
    send_raw_frame(port, malformed);

    const auto errors = poll_errors(server);
    assert(errors.size() == 1);
    assert(errors[0].find("invalid data packet") != std::string::npos);
    assert(server.take_transport_errors().empty());

    transport::DataPacket packet;
    const auto after_error = server.receive(rx.handle, packet);
    assert(after_error.ok);
    assert(!after_error.has_packet);

    // The server keeps serving: a new peer can connect and deliver valid packets.
    transport::BeastRuntimeNetEndpoint client;
    client.configure_client("ws://127.0.0.1:" + std::to_string(port));
    const auto tx = client.open_endpoint({.endpoint_name = "tx", .stream_name = "recv.test", .allow_publish = true, .allow_receive = false});
    assert(tx.ok);

    // The first packet on a new stream, at model time zero, is delivered.
    auto first = make_packet("recv.test", 0, 4.25);
    assert(*first.t_model_ns == 0);
    const auto sent = client.publish(tx.handle, first);
    assert(sent.ok && sent.delivered);
    if (!poll_receive(server, rx.handle, packet)) {
        throw std::runtime_error("Valid packet after a malformed frame was not received.");
    }
    assert(packet.sequence == 0);
    assert(packet.t_model_ns.has_value() && *packet.t_model_ns == 0);
    assert(transport::decode_f64_payload(packet.payload) == 4.25);
    assert(server.take_transport_errors().empty());
}

} // namespace

int main()
try {
    test_model_time_zero();
    test_encoder_rejects_invalid_packets();
    test_decoder_rejects_malformed_frames();
    test_receive_failure_handling();
    return 0;
} catch (const std::exception& ex) {
    std::cerr << "s7transport validation smoke: " << ex.what() << '\n';
    return 1;
}
