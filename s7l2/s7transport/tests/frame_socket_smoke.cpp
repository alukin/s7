// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: LGPL-3.0-or-later
// Frame services over actual WebSocket / mutual-TLS sockets. RuntimeService's
// deployment and capability path is deliberately not part of this fixture.
#include <s7/frame_transport.hpp>
#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/beast.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/beast/websocket/ssl.hpp>
#include <cstring>
#include <future>
#include <iostream>

namespace f = s7::frames;
namespace w = s7::wire;
namespace asio = boost::asio;
namespace beast = boost::beast;
namespace ws = beast::websocket;
using tcp = asio::ip::tcp;
namespace {
void check(bool value, const char* error) { if (!value) throw std::runtime_error(error); }
template<class Socket> void exchange(Socket& socket, f::FrameSink& sink) {
    socket.binary(true); socket.read_message_max(f::kMaxMessageBytes);
    for (int block = 0; block < 12; ++block) {
        std::vector<float> input(256);
        for (std::size_t i = 0; i < input.size(); ++i) input[i] = float(block * 256 + i) / 4096;
        check(sink.publish_copy(input.data(), 128).frames == 128, "socket publish failed");
        auto message = sink.next_message("peer"); check(bool(message), "no socket message");
        socket.write(asio::buffer(message.bytes().data(), message.bytes().size()));
        beast::flat_buffer buffer; socket.read(buffer);
        const auto data = buffer.cdata();
        const auto ack = w::decode_control_message(std::span(static_cast<const std::uint8_t*>(data.data()), data.size()));
        check(ack.kind == w::ControlKind::Ack && ack.next_expected == std::uint64_t(block + 1), "invalid socket ACK");
        sink.acknowledge("peer", ack.next_expected);
        check(sink.log_depth() == 0, "socket consumption did not release log");
    }
    sink.fault(f::Status::SourceOverflow, 7);
    auto marker = sink.next_message("peer"); socket.write(asio::buffer(marker.bytes().data(), marker.bytes().size()));
    beast::flat_buffer buffer; socket.read(buffer); const auto data = buffer.cdata();
    const auto ack = w::decode_control_message(std::span(static_cast<const std::uint8_t*>(data.data()), data.size()));
    sink.acknowledge("peer", ack.next_expected);
    check(sink.state() == f::SourceState::Faulted, "socket terminal not consumed");
}
template<class Socket> void ingest(Socket& socket, f::FrameSource& source) {
    socket.binary(true); socket.read_message_max(f::kMaxMessageBytes);
    for (int block = 0; block < 13; ++block) {
        beast::flat_buffer buffer; socket.read(buffer); const auto data = buffer.cdata();
        check(source.on_message(std::span(static_cast<const std::uint8_t*>(data.data()), data.size())), "socket frame rejected");
        check(source.consumed_up_to() == std::uint64_t(block), "socket receipt advanced ACK");
        std::vector<float> output(256);
        if (block < 12) {
            auto first = source.receive_copy(output.data(), 47);
            auto rest = source.receive_copy(output.data() + 94, 81);
            check(first.frames == 47 && rest.frames == 81 && rest.status == f::Status::Ok, "socket partial read failed");
            for (std::size_t i = 0; i < output.size(); ++i) check(output[i] == float(block * 256 + i) / 4096, "socket sample mismatch");
        } else check(source.consume(0).status == f::Status::SourceOverflow, "socket loss marker missing");
        const auto ack = w::encode_control_message({w::ControlKind::Ack, 1, source.consumed_up_to(), {}});
        socket.write(asio::buffer(ack));
    }
}
void run(bool tls, f::Encoding encoding) {
    f::Descriptor d; d.channels = {"L", "R"}; d.max_frames_per_packet = 128;
    d.window_packets = 2; d.window_frames = 256; d.encoding = encoding;
    auto send = std::make_shared<f::BufferPool>(2, f::kMaxMessageBytes);
    auto receive = std::make_shared<f::BufferPool>(2, w::kMaxPayloadBytes);
    s7::stream::MemorySequenceStore sequences;
    f::FrameSink sink(d, sequences, send); f::FrameSource source(d, receive); sink.resume("peer", 0);
    asio::io_context io; tcp::acceptor acceptor(io, {asio::ip::address_v4::loopback(), 0});
    asio::ssl::context context(asio::ssl::context::tls);
    if (tls) {
        const std::string root = S7_TEST_CERT_DIR;
        context.use_certificate_chain_file(root + "/s7_runtime_node.cert.pem");
        context.use_private_key_file(root + "/s7_runtime_node.key.pem", asio::ssl::context::pem);
        context.load_verify_file(root + "/s7_root_ca.cert.pem");
        context.set_verify_mode(asio::ssl::verify_peer | asio::ssl::verify_fail_if_no_peer_cert);
    }
    auto server = std::async(std::launch::async, [&] {
        if (tls) {
            ws::stream<beast::ssl_stream<tcp::socket>> socket(io, context);
            acceptor.accept(beast::get_lowest_layer(socket)); socket.next_layer().handshake(asio::ssl::stream_base::server);
            socket.accept(); ingest(socket, source);
            boost::system::error_code ec; beast::get_lowest_layer(socket).shutdown(tcp::socket::shutdown_both, ec);
        } else {
            ws::stream<tcp::socket> socket(io); acceptor.accept(socket.next_layer()); socket.accept(); ingest(socket, source);
            boost::system::error_code ec; socket.next_layer().shutdown(tcp::socket::shutdown_both, ec);
        }
    });
    if (tls) {
        ws::stream<beast::ssl_stream<tcp::socket>> socket(io, context);
        beast::get_lowest_layer(socket).connect(acceptor.local_endpoint());
        socket.next_layer().handshake(asio::ssl::stream_base::client); socket.handshake("localhost", "/frames"); exchange(socket, sink);
    } else {
        ws::stream<tcp::socket> socket(io); socket.next_layer().connect(acceptor.local_endpoint());
        socket.handshake("localhost", "/frames"); exchange(socket, sink);
    }
    server.get();
}
}
int main() {
    try {
        for (auto encoding : {f::Encoding::Raw, f::Encoding::Lz4, f::Encoding::Zstd}) if (f::encoding_available(encoding)) {
            run(false, encoding); run(true, encoding);
        }
        std::cout << "frame WebSocket/TLS transfer, consumption ACK and fault checks passed\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
