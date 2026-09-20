#include "s7/transport.hpp"

#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstring>
#include <functional>
#include <stdexcept>
#include <thread>
#include <type_traits>

#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl/context.hpp>
#include <boost/asio/ssl/stream.hpp>
#include <boost/asio/connect.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/beast/core/buffers_to_string.hpp>
#include <boost/beast/core/flat_buffer.hpp>
#include <boost/beast/http/read.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/beast/http/string_body.hpp>
#include <boost/beast/websocket/stream.hpp>
#include <boost/system/error_code.hpp>

namespace s7::transport {
namespace {

using Tcp = boost::asio::ip::tcp;
namespace websocket = boost::beast::websocket;
namespace http = boost::beast::http;
namespace ssl = boost::asio::ssl;
using boost::beast::flat_buffer;
using PlainWs = websocket::stream<Tcp::socket>;
using TlsWs = websocket::stream<boost::beast::ssl_stream<Tcp::socket>>;

constexpr std::uint16_t kProtocolVersion = 1;
constexpr std::size_t kFixedHeaderBytes = 54;
constexpr std::size_t kMaxPayloadBytes = 256u * 1024u;
constexpr std::uint16_t kFlagHasSystemTimestamp = 0x0001u;

void append_u16_be(std::vector<std::uint8_t>& out, const std::uint16_t value)
{
    out.push_back(static_cast<std::uint8_t>((value >> 8) & 0xFFu));
    out.push_back(static_cast<std::uint8_t>(value & 0xFFu));
}

void append_u32_be(std::vector<std::uint8_t>& out, const std::uint32_t value)
{
    for (int shift = 24; shift >= 0; shift -= 8) {
        out.push_back(static_cast<std::uint8_t>((value >> shift) & 0xFFu));
    }
}

void append_u64_be(std::vector<std::uint8_t>& out, const std::uint64_t value)
{
    for (int shift = 56; shift >= 0; shift -= 8) {
        out.push_back(static_cast<std::uint8_t>((value >> shift) & 0xFFu));
    }
}

[[nodiscard]] auto read_u16_be(const std::vector<std::uint8_t>& in, std::size_t& offset) -> std::uint16_t
{
    if (offset + 2 > in.size()) {
        throw std::invalid_argument("Packet underflow while reading u16.");
    }
    const auto value = static_cast<std::uint16_t>((static_cast<std::uint16_t>(in[offset]) << 8) |
                                                  static_cast<std::uint16_t>(in[offset + 1]));
    offset += 2;
    return value;
}

[[nodiscard]] auto read_u32_be(const std::vector<std::uint8_t>& in, std::size_t& offset) -> std::uint32_t
{
    if (offset + 4 > in.size()) {
        throw std::invalid_argument("Packet underflow while reading u32.");
    }

    std::uint32_t value = 0;
    for (int index = 0; index < 4; ++index) {
        value = static_cast<std::uint32_t>((value << 8) | in[offset + index]);
    }
    offset += 4;
    return value;
}

[[nodiscard]] auto read_u64_be(const std::vector<std::uint8_t>& in, std::size_t& offset) -> std::uint64_t
{
    if (offset + 8 > in.size()) {
        throw std::invalid_argument("Packet underflow while reading u64.");
    }

    std::uint64_t value = 0;
    for (int index = 0; index < 8; ++index) {
        value = (value << 8) | static_cast<std::uint64_t>(in[offset + index]);
    }
    offset += 8;
    return value;
}

[[nodiscard]] auto seconds_to_ns(const double seconds) -> std::uint64_t
{
    if (seconds <= 0.0) {
        return 0;
    }
    return static_cast<std::uint64_t>(std::llround(seconds * 1'000'000'000.0));
}

[[nodiscard]] auto ns_to_seconds(const std::uint64_t ns) -> double
{
    return static_cast<double>(ns) / 1'000'000'000.0;
}

[[nodiscard]] auto normalize_ws_base_target(std::string target) -> std::string
{
    if (target.empty() || target == "/") {
        return {};
    }
    if (target.ends_with("/control")) {
        target.resize(target.size() - std::strlen("/control"));
    } else if (target.ends_with("/data")) {
        target.resize(target.size() - std::strlen("/data"));
    }
    if (target == "/") {
        return {};
    }
    return target;
}

[[nodiscard]] auto ws_target_for_path(const ParsedWebSocketUrl& parsed, const std::string_view suffix) -> std::string
{
    const auto base = normalize_ws_base_target(parsed.target);
    if (base.empty()) {
        return std::string(suffix);
    }
    return base + std::string(suffix);
}

} // namespace

auto parse_ws_url(const std::string& url) -> ParsedWebSocketUrl
{
    ParsedWebSocketUrl parsed;
    std::size_t host_start = 0;
    if (url.rfind("ws://", 0) == 0) {
        parsed.secure = false;
        host_start = std::strlen("ws://");
    } else if (url.rfind("wss://", 0) == 0) {
        parsed.secure = true;
        host_start = std::strlen("wss://");
    } else {
        throw std::invalid_argument("Only ws:// and wss:// URLs are supported: '" + url + "'.");
    }
    const auto path_pos = url.find('/', host_start);
    const auto host_port = path_pos == std::string::npos ? url.substr(host_start) : url.substr(host_start, path_pos - host_start);

    const auto colon_pos = host_port.rfind(':');
    if (colon_pos == std::string::npos || colon_pos == 0 || colon_pos + 1 >= host_port.size()) {
        throw std::invalid_argument("WebSocket URL must include host:port: '" + url + "'.");
    }

    parsed.host = host_port.substr(0, colon_pos);
    parsed.port = host_port.substr(colon_pos + 1);
    parsed.target = path_pos == std::string::npos ? "/" : url.substr(path_pos);
    return parsed;
}

auto encode_f64_payload(const double value) -> std::vector<std::uint8_t>
{
    const auto raw = std::bit_cast<std::uint64_t>(value);
    std::vector<std::uint8_t> payload;
    payload.reserve(8);
    append_u64_be(payload, raw);
    return payload;
}

auto decode_f64_payload(const std::vector<std::uint8_t>& payload) -> double
{
    if (payload.size() != 8) {
        throw std::invalid_argument("Expected 8-byte F64 payload.");
    }

    std::size_t offset = 0;
    const auto raw = read_u64_be(payload, offset);
    return std::bit_cast<double>(raw);
}

auto encode_packet_v1(const DataPacket& packet) -> std::vector<std::uint8_t>
{
    if (packet.payload.size() > kMaxPayloadBytes) {
        throw std::invalid_argument("Payload exceeds v1 max packet size.");
    }
    if (packet.stream_name.size() > 0xFFFFu) {
        throw std::invalid_argument("Stream name exceeds v1 extension encoding limit.");
    }

    const auto extension_bytes = static_cast<std::size_t>(2u + packet.stream_name.size() + packet.extensions.size());
    if (extension_bytes > 0xFFFFu) {
        throw std::invalid_argument("Extension area exceeds v1 max length.");
    }

    const auto t_model_ns = packet.t_model_ns != 0 ? packet.t_model_ns : seconds_to_ns(packet.t_model);
    if (t_model_ns == 0) {
        throw std::invalid_argument("DataPacket requires non-zero t_model_ns or t_model.");
    }

    const auto has_system_timestamp = packet.t_system_ns.has_value();
    const auto flags = static_cast<std::uint16_t>(packet.flags | (has_system_timestamp ? kFlagHasSystemTimestamp : 0u));
    const auto elem_count = packet.elem_count != 0
        ? packet.elem_count
        : static_cast<std::uint32_t>(packet.payload.size() / (packet.data_type == 0x000Cu ? 8u : 1u));

    std::vector<std::uint8_t> frame;
    frame.reserve(kFixedHeaderBytes + extension_bytes + packet.payload.size());
    append_u16_be(frame, kProtocolVersion);
    append_u16_be(frame, flags);
    append_u64_be(frame, packet.sequence);
    append_u64_be(frame, packet.stream_id);
    append_u32_be(frame, packet.channel_id);
    append_u16_be(frame, packet.data_type);
    append_u32_be(frame, elem_count);
    append_u32_be(frame, static_cast<std::uint32_t>(packet.payload.size()));
    append_u64_be(frame, t_model_ns);
    append_u64_be(frame, packet.t_system_ns.value_or(0));
    append_u16_be(frame, static_cast<std::uint16_t>(extension_bytes));
    append_u16_be(frame, 0u);
    append_u16_be(frame, static_cast<std::uint16_t>(packet.stream_name.size()));
    frame.insert(frame.end(), packet.stream_name.begin(), packet.stream_name.end());
    frame.insert(frame.end(), packet.extensions.begin(), packet.extensions.end());
    frame.insert(frame.end(), packet.payload.begin(), packet.payload.end());
    return frame;
}

auto decode_packet_v1(const std::vector<std::uint8_t>& frame) -> DataPacket
{
    if (frame.size() < kFixedHeaderBytes) {
        throw std::invalid_argument("Packet frame shorter than fixed header.");
    }

    std::size_t offset = 0;
    const auto version = read_u16_be(frame, offset);
    if (version != kProtocolVersion) {
        throw std::invalid_argument("Unsupported packet version: " + std::to_string(version));
    }

    DataPacket packet;
    packet.flags = read_u16_be(frame, offset);
    packet.sequence = read_u64_be(frame, offset);
    packet.stream_id = read_u64_be(frame, offset);
    packet.channel_id = read_u32_be(frame, offset);
    packet.data_type = read_u16_be(frame, offset);
    packet.elem_count = read_u32_be(frame, offset);
    const auto payload_size = read_u32_be(frame, offset);
    packet.t_model_ns = read_u64_be(frame, offset);
    const auto t_system_ns = read_u64_be(frame, offset);
    const auto ext_len = read_u16_be(frame, offset);
    const auto reserved = read_u16_be(frame, offset);
    if (reserved != 0) {
        throw std::invalid_argument("Packet reserved field must be zero in v1.");
    }

    if (offset + ext_len + payload_size > frame.size()) {
        throw std::invalid_argument("Packet payload/extension lengths exceed frame size.");
    }

    const auto extension_end = offset + ext_len;
    if (ext_len >= 2) {
        const auto stream_name_len = read_u16_be(frame, offset);
        if (offset + stream_name_len > extension_end) {
            throw std::invalid_argument("Packet stream-name extension exceeds extension area.");
        }
        packet.stream_name.assign(frame.begin() + static_cast<std::ptrdiff_t>(offset),
                                  frame.begin() + static_cast<std::ptrdiff_t>(offset + stream_name_len));
        offset += stream_name_len;
    }
    packet.extensions.assign(frame.begin() + static_cast<std::ptrdiff_t>(offset),
                             frame.begin() + static_cast<std::ptrdiff_t>(extension_end));
    offset = extension_end;
    packet.payload.assign(frame.begin() + static_cast<std::ptrdiff_t>(offset),
                          frame.begin() + static_cast<std::ptrdiff_t>(offset + payload_size));
    packet.t_model = ns_to_seconds(packet.t_model_ns);
    if ((packet.flags & kFlagHasSystemTimestamp) != 0u) {
        packet.t_system_ns = t_system_ns;
    }
    return packet;
}

auto InMemoryRuntimeNetEndpoint::backend_name() const -> std::string
{
    return "inmemory";
}

auto InMemoryRuntimeNetEndpoint::open_endpoint(const EndpointOpenRequest& request) -> EndpointOpenResult
{
    if (request.stream_name.empty()) {
        return EndpointOpenResult{EndpointHandle{}, false, "stream_name must not be empty"};
    }

    std::lock_guard<std::mutex> lock(mutex_);

    const EndpointHandle handle{next_handle_++};
    endpoints_.emplace(handle.value, EndpointState{
        .stream_name = request.stream_name,
        .allow_publish = request.allow_publish,
        .allow_receive = request.allow_receive,
    });

    return EndpointOpenResult{handle, true, {}};
}

void InMemoryRuntimeNetEndpoint::close_endpoint(const EndpointHandle& handle)
{
    if (!handle.valid()) {
        return;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    endpoints_.erase(handle.value);
}

auto InMemoryRuntimeNetEndpoint::publish(const EndpointHandle& handle, const DataPacket& packet) -> SendResult
{
    if (!handle.valid()) {
        return SendResult{false, "invalid handle", 0};
    }

    std::lock_guard<std::mutex> lock(mutex_);

    const auto endpoint_it = endpoints_.find(handle.value);
    if (endpoint_it == endpoints_.end()) {
        return SendResult{false, "unknown endpoint handle", 0};
    }
    if (!endpoint_it->second.allow_publish) {
        return SendResult{false, "endpoint is not publish-enabled", 0};
    }

    auto packet_to_queue = packet;
    if (packet_to_queue.stream_name.empty()) {
        packet_to_queue.stream_name = endpoint_it->second.stream_name;
    }

    auto& queue = stream_queues_[packet_to_queue.stream_name];
    queue.push_back(std::move(packet_to_queue));

    return SendResult{true, {}, queue.size()};
}

auto InMemoryRuntimeNetEndpoint::receive(const EndpointHandle& handle, DataPacket& packet) -> ReceiveResult
{
    if (!handle.valid()) {
        return ReceiveResult{false, false, "invalid handle"};
    }

    std::lock_guard<std::mutex> lock(mutex_);

    const auto endpoint_it = endpoints_.find(handle.value);
    if (endpoint_it == endpoints_.end()) {
        return ReceiveResult{false, false, "unknown endpoint handle"};
    }
    if (!endpoint_it->second.allow_receive) {
        return ReceiveResult{false, false, "endpoint is not receive-enabled"};
    }

    auto queue_it = stream_queues_.find(endpoint_it->second.stream_name);
    if (queue_it == stream_queues_.end() || queue_it->second.empty()) {
        return ReceiveResult{true, false, {}};
    }

    packet = std::move(queue_it->second.front());
    queue_it->second.pop_front();
    return ReceiveResult{true, true, {}};
}

struct BeastRuntimeNetEndpoint::Impl {
    std::shared_ptr<boost::asio::io_context> io_context = std::make_shared<boost::asio::io_context>();
    std::shared_ptr<ssl::context> server_ssl_context;
    std::shared_ptr<ssl::context> client_ssl_context;
    bool server_tls_enabled{false};
    bool client_tls_enabled{false};
    std::mutex socket_mutex;
    std::mutex lifecycle_mutex;
    std::shared_ptr<PlainWs> plain_data_socket;
    std::shared_ptr<PlainWs> plain_control_socket;
    std::shared_ptr<TlsWs> tls_data_socket;
    std::shared_ptr<TlsWs> tls_control_socket;
    std::unique_ptr<Tcp::acceptor> acceptor;
    std::thread accept_thread;
    std::thread data_read_thread;
    std::thread control_read_thread;
    std::atomic<bool> stop{false};
    std::mutex control_handler_mutex;
    std::mutex control_request_mutex;
    ControlRequestHandler control_request_handler;
    std::optional<std::uint16_t> server_port;
    std::optional<ParsedWebSocketUrl> client_url;
};

BeastRuntimeNetEndpoint::BeastRuntimeNetEndpoint()
    : impl_(std::make_unique<Impl>())
{
}

BeastRuntimeNetEndpoint::~BeastRuntimeNetEndpoint()
{
    impl_->stop = true;
    {
        std::lock_guard<std::mutex> lock(impl_->socket_mutex);
        if (impl_->acceptor) {
            boost::system::error_code ec;
            impl_->acceptor->close(ec);
        }
        if (impl_->plain_data_socket) {
            boost::system::error_code ec;
            impl_->plain_data_socket->close(websocket::close_code::normal, ec);
        }
        if (impl_->plain_control_socket) {
            boost::system::error_code ec;
            impl_->plain_control_socket->close(websocket::close_code::normal, ec);
        }
        if (impl_->tls_data_socket) {
            boost::system::error_code ec;
            impl_->tls_data_socket->close(websocket::close_code::normal, ec);
        }
        if (impl_->tls_control_socket) {
            boost::system::error_code ec;
            impl_->tls_control_socket->close(websocket::close_code::normal, ec);
        }
    }
    if (impl_->accept_thread.joinable()) {
        impl_->accept_thread.join();
    }
    if (impl_->data_read_thread.joinable()) {
        impl_->data_read_thread.join();
    }
    if (impl_->control_read_thread.joinable()) {
        impl_->control_read_thread.join();
    }
}

auto BeastRuntimeNetEndpoint::backend_name() const -> std::string
{
    return "beast";
}

void BeastRuntimeNetEndpoint::set_control_request_handler(ControlRequestHandler handler)
{
    std::lock_guard<std::mutex> lock(impl_->control_handler_mutex);
    impl_->control_request_handler = std::move(handler);
}

void BeastRuntimeNetEndpoint::configure_server(const std::uint16_t port, const TlsConfig& tls)
{
    if (port == 0) {
        throw std::invalid_argument("BeastRuntimeNetEndpoint server port must not be 0.");
    }

    std::lock_guard<std::mutex> lifecycle_lock(impl_->lifecycle_mutex);
    if (impl_->server_port == port && impl_->accept_thread.joinable()) {
        return;
    }

    impl_->server_port = port;
    impl_->server_tls_enabled = tls.enabled;
    if (tls.enabled) {
        if (tls.cert_file.empty() || tls.key_file.empty() || tls.ca_file.empty()) {
            throw std::invalid_argument("TLS server requires cert_file, key_file, and ca_file.");
        }
        impl_->server_ssl_context = std::make_shared<ssl::context>(ssl::context::tls_server);
        impl_->server_ssl_context->set_options(ssl::context::default_workarounds | ssl::context::no_sslv2 | ssl::context::single_dh_use);
        impl_->server_ssl_context->use_certificate_chain_file(tls.cert_file);
        impl_->server_ssl_context->use_private_key_file(tls.key_file, ssl::context::pem);
        impl_->server_ssl_context->load_verify_file(tls.ca_file);
        impl_->server_ssl_context->set_verify_mode(tls.require_client_cert
            ? (ssl::verify_peer | ssl::verify_fail_if_no_peer_cert)
            : ssl::verify_peer);
    }
    impl_->acceptor = std::make_unique<Tcp::acceptor>(*impl_->io_context, Tcp::endpoint(Tcp::v4(), port));
    impl_->acceptor->non_blocking(true);
    impl_->accept_thread = std::thread([this] {
        while (!impl_->stop.load()) {
            Tcp::socket socket(*impl_->io_context);
            boost::system::error_code ec;
            impl_->acceptor->accept(socket, ec);
            if (ec == boost::asio::error::would_block || ec == boost::asio::error::try_again) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                continue;
            }
            if (ec == boost::asio::error::operation_aborted || !impl_->acceptor) {
                break;
            }
            if (ec) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                continue;
            }

            auto bind_and_start_reader = [this](const auto& ws, const bool is_data) {
                std::lock_guard<std::mutex> socket_lock(impl_->socket_mutex);
                auto& reader = is_data ? impl_->data_read_thread : impl_->control_read_thread;
                if (is_data) {
                    if constexpr (std::is_same_v<std::decay_t<decltype(*ws)>, PlainWs>) {
                        if (impl_->plain_data_socket) {
                            boost::system::error_code close_ec;
                            impl_->plain_data_socket->close(websocket::close_code::normal, close_ec);
                        }
                        impl_->plain_data_socket = ws;
                    } else {
                        if (impl_->tls_data_socket) {
                            boost::system::error_code close_ec;
                            impl_->tls_data_socket->close(websocket::close_code::normal, close_ec);
                        }
                        impl_->tls_data_socket = ws;
                    }
                } else {
                    if constexpr (std::is_same_v<std::decay_t<decltype(*ws)>, PlainWs>) {
                        if (impl_->plain_control_socket) {
                            boost::system::error_code close_ec;
                            impl_->plain_control_socket->close(websocket::close_code::normal, close_ec);
                        }
                        impl_->plain_control_socket = ws;
                    } else {
                        if (impl_->tls_control_socket) {
                            boost::system::error_code close_ec;
                            impl_->tls_control_socket->close(websocket::close_code::normal, close_ec);
                        }
                        impl_->tls_control_socket = ws;
                    }
                }
                if (reader.joinable()) {
                    reader.join();
                }

                reader = std::thread([this, ws, is_data] {
                    while (!impl_->stop.load()) {
                        flat_buffer read_buffer;
                        boost::system::error_code read_ec;
                        ws->read(read_buffer, read_ec);
                        if (read_ec) {
                            break;
                        }

                        if (!is_data) {
                            const auto request_json = boost::beast::buffers_to_string(read_buffer.data());
                            ControlRequestHandler handler;
                            {
                                std::lock_guard<std::mutex> lock(impl_->control_handler_mutex);
                                handler = impl_->control_request_handler;
                            }
                            const auto response_json = handler
                                ? handler(request_json)
                                : std::string{"{\"type\":\"ERROR\",\"protocol_version\":1,\"code\":\"INTERNAL_ERROR\",\"message\":\"No control handler registered\"}"};
                            ws->text(true);
                            boost::system::error_code write_ec;
                            ws->write(boost::asio::buffer(response_json), write_ec);
                            if (write_ec) {
                                break;
                            }
                            continue;
                        }

                        const auto bytes = boost::beast::buffers_to_string(read_buffer.data());
                        std::vector<std::uint8_t> frame(bytes.begin(), bytes.end());
                        auto packet = decode_packet_v1(frame);
                        std::lock_guard<std::mutex> queue_lock(mutex_);
                        stream_queues_[packet.stream_name].push_back(std::move(packet));
                    }
                });
            };

            if (impl_->server_tls_enabled) {
                auto ws = std::make_shared<TlsWs>(std::move(socket), *impl_->server_ssl_context);
                ws->next_layer().handshake(ssl::stream_base::server, ec);
                if (ec) {
                    continue;
                }

                flat_buffer buffer;
                http::request<http::string_body> request;
                http::read(ws->next_layer(), buffer, request, ec);
                if (ec) {
                    continue;
                }
                ws->accept(request, ec);
                if (ec) {
                    continue;
                }

                const auto target = std::string(request.target());
                const auto is_data = target.ends_with("/data");
                const auto is_control = target.ends_with("/control");
                if (!is_data && !is_control) {
                    boost::system::error_code close_ec;
                    ws->close(websocket::close_code::policy_error, close_ec);
                    continue;
                }
                bind_and_start_reader(ws, is_data);
            } else {
                flat_buffer buffer;
                http::request<http::string_body> request;
                http::read(socket, buffer, request, ec);
                if (ec) {
                    continue;
                }
                auto ws = std::make_shared<PlainWs>(std::move(socket));
                ws->accept(request, ec);
                if (ec) {
                    continue;
                }

                const auto target = std::string(request.target());
                const auto is_data = target.ends_with("/data");
                const auto is_control = target.ends_with("/control");
                if (!is_data && !is_control) {
                    boost::system::error_code close_ec;
                    ws->close(websocket::close_code::policy_error, close_ec);
                    continue;
                }
                bind_and_start_reader(ws, is_data);
            }
        }
    });
}

void BeastRuntimeNetEndpoint::configure_client(const std::string& url, const TlsConfig& tls)
{
    const auto parsed = parse_ws_url(url);
    {
        std::lock_guard<std::mutex> lifecycle_lock(impl_->lifecycle_mutex);
        impl_->client_url = parsed;
        impl_->client_tls_enabled = parsed.secure || tls.enabled;
        if (impl_->client_tls_enabled) {
            if (tls.cert_file.empty() || tls.key_file.empty() || tls.ca_file.empty()) {
                throw std::invalid_argument("TLS client requires cert_file, key_file, and ca_file.");
            }
            impl_->client_ssl_context = std::make_shared<ssl::context>(ssl::context::tls_client);
            impl_->client_ssl_context->set_default_verify_paths();
            impl_->client_ssl_context->use_certificate_chain_file(tls.cert_file);
            impl_->client_ssl_context->use_private_key_file(tls.key_file, ssl::context::pem);
            impl_->client_ssl_context->load_verify_file(tls.ca_file);
            impl_->client_ssl_context->set_verify_mode(ssl::verify_peer);
        }
    }

    auto connect_plain = [this, &parsed](const std::string_view suffix, std::shared_ptr<PlainWs>& slot, std::thread& reader, const bool is_data) {
        Tcp::resolver resolver(*impl_->io_context);
        auto results = resolver.resolve(parsed.host, parsed.port);
        auto ws = std::make_shared<PlainWs>(*impl_->io_context);
        boost::asio::connect(ws->next_layer(), results.begin(), results.end());
        const auto target = ws_target_for_path(parsed, suffix);
        ws->handshake(parsed.host + ":" + parsed.port, target);

        std::lock_guard<std::mutex> socket_lock(impl_->socket_mutex);
        if (slot) {
            boost::system::error_code close_ec;
            slot->close(websocket::close_code::normal, close_ec);
        }
        if (reader.joinable()) {
            reader.join();
        }
        slot = ws;
        if (!is_data) {
            return;
        }
        reader = std::thread([this, ws] {
            while (!impl_->stop.load()) {
                flat_buffer read_buffer;
                boost::system::error_code read_ec;
                ws->read(read_buffer, read_ec);
                if (read_ec) {
                    break;
                }

                const auto bytes = boost::beast::buffers_to_string(read_buffer.data());
                std::vector<std::uint8_t> frame(bytes.begin(), bytes.end());
                auto packet = decode_packet_v1(frame);
                std::lock_guard<std::mutex> queue_lock(mutex_);
                stream_queues_[packet.stream_name].push_back(std::move(packet));
            }
        });
    };

    auto connect_tls = [this, &parsed](const std::string_view suffix, std::shared_ptr<TlsWs>& slot, std::thread& reader, const bool is_data) {
        Tcp::resolver resolver(*impl_->io_context);
        auto results = resolver.resolve(parsed.host, parsed.port);
        auto ws = std::make_shared<TlsWs>(*impl_->io_context, *impl_->client_ssl_context);
        boost::asio::connect(ws->next_layer().next_layer(), results.begin(), results.end());
        ws->next_layer().handshake(ssl::stream_base::client);
        const auto target = ws_target_for_path(parsed, suffix);
        ws->handshake(parsed.host + ":" + parsed.port, target);

        std::lock_guard<std::mutex> socket_lock(impl_->socket_mutex);
        if (slot) {
            boost::system::error_code close_ec;
            slot->close(websocket::close_code::normal, close_ec);
        }
        if (reader.joinable()) {
            reader.join();
        }
        slot = ws;
        if (!is_data) {
            return;
        }
        reader = std::thread([this, ws] {
            while (!impl_->stop.load()) {
                flat_buffer read_buffer;
                boost::system::error_code read_ec;
                ws->read(read_buffer, read_ec);
                if (read_ec) {
                    break;
                }

                const auto bytes = boost::beast::buffers_to_string(read_buffer.data());
                std::vector<std::uint8_t> frame(bytes.begin(), bytes.end());
                auto packet = decode_packet_v1(frame);
                std::lock_guard<std::mutex> queue_lock(mutex_);
                stream_queues_[packet.stream_name].push_back(std::move(packet));
            }
        });
    };

    if (impl_->client_tls_enabled) {
        connect_tls("/control", impl_->tls_control_socket, impl_->control_read_thread, false);
        connect_tls("/data", impl_->tls_data_socket, impl_->data_read_thread, true);
    } else {
        connect_plain("/control", impl_->plain_control_socket, impl_->control_read_thread, false);
        connect_plain("/data", impl_->plain_data_socket, impl_->data_read_thread, true);
    }
}

auto BeastRuntimeNetEndpoint::request_control_json(const std::string& request_json) -> std::string
{
    std::shared_ptr<PlainWs> plain_control_socket;
    std::shared_ptr<TlsWs> tls_control_socket;
    {
        std::lock_guard<std::mutex> socket_lock(impl_->socket_mutex);
        plain_control_socket = impl_->plain_control_socket;
        tls_control_socket = impl_->tls_control_socket;
    }
    if (!plain_control_socket && !tls_control_socket) {
        throw std::runtime_error("control channel is not connected");
    }

    std::lock_guard<std::mutex> call_lock(impl_->control_request_mutex);
    if (tls_control_socket) {
        tls_control_socket->text(true);
        boost::system::error_code write_ec;
        tls_control_socket->write(boost::asio::buffer(request_json), write_ec);
        if (write_ec) {
            throw std::runtime_error("control write failed: " + write_ec.message());
        }
        flat_buffer read_buffer;
        boost::system::error_code read_ec;
        tls_control_socket->read(read_buffer, read_ec);
        if (read_ec) {
            throw std::runtime_error("control read failed: " + read_ec.message());
        }
        return boost::beast::buffers_to_string(read_buffer.data());
    }

    plain_control_socket->text(true);
    boost::system::error_code write_ec;
    plain_control_socket->write(boost::asio::buffer(request_json), write_ec);
    if (write_ec) {
        throw std::runtime_error("control write failed: " + write_ec.message());
    }
    flat_buffer read_buffer;
    boost::system::error_code read_ec;
    plain_control_socket->read(read_buffer, read_ec);
    if (read_ec) {
        throw std::runtime_error("control read failed: " + read_ec.message());
    }
    return boost::beast::buffers_to_string(read_buffer.data());
}

auto BeastRuntimeNetEndpoint::open_endpoint(const EndpointOpenRequest& request) -> EndpointOpenResult
{
    if (request.stream_name.empty()) {
        return EndpointOpenResult{EndpointHandle{}, false, "stream_name must not be empty"};
    }

    std::lock_guard<std::mutex> lock(mutex_);
    const EndpointHandle handle{next_handle_++};
    endpoints_.emplace(handle.value, EndpointState{
        .stream_name = request.stream_name,
        .allow_publish = request.allow_publish,
        .allow_receive = request.allow_receive,
    });
    return EndpointOpenResult{handle, true, {}};
}

void BeastRuntimeNetEndpoint::close_endpoint(const EndpointHandle& handle)
{
    if (!handle.valid()) {
        return;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    endpoints_.erase(handle.value);
}

auto BeastRuntimeNetEndpoint::publish(const EndpointHandle& handle, const DataPacket& packet) -> SendResult
{
    if (!handle.valid()) {
        return SendResult{false, "invalid handle", 0};
    }

    EndpointState state;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto endpoint_it = endpoints_.find(handle.value);
        if (endpoint_it == endpoints_.end()) {
            return SendResult{false, "unknown endpoint handle", 0};
        }
        if (!endpoint_it->second.allow_publish) {
            return SendResult{false, "endpoint is not publish-enabled", 0};
        }
        state = endpoint_it->second;
    }

    auto packet_to_send = packet;
    if (packet_to_send.stream_name.empty()) {
        packet_to_send.stream_name = state.stream_name;
    }

    const auto frame = encode_packet_v1(packet_to_send);
    std::shared_ptr<PlainWs> plain_data_socket;
    std::shared_ptr<TlsWs> tls_data_socket;
    {
        std::lock_guard<std::mutex> socket_lock(impl_->socket_mutex);
        plain_data_socket = impl_->plain_data_socket;
        tls_data_socket = impl_->tls_data_socket;
    }
    if (!plain_data_socket && !tls_data_socket) {
        return SendResult{false, "data channel is not connected", 0};
    }

    if (tls_data_socket) {
        boost::system::error_code ec;
        tls_data_socket->binary(true);
        tls_data_socket->write(boost::asio::buffer(frame), ec);
        if (ec) {
            return SendResult{false, ec.message(), 0};
        }
        return SendResult{true, {}, 0};
    }

    boost::system::error_code ec;
    plain_data_socket->binary(true);
    plain_data_socket->write(boost::asio::buffer(frame), ec);
    if (ec) {
        return SendResult{false, ec.message(), 0};
    }

    return SendResult{true, {}, 0};
}

auto BeastRuntimeNetEndpoint::receive(const EndpointHandle& handle, DataPacket& packet) -> ReceiveResult
{
    if (!handle.valid()) {
        return ReceiveResult{false, false, "invalid handle"};
    }

    std::lock_guard<std::mutex> lock(mutex_);

    const auto endpoint_it = endpoints_.find(handle.value);
    if (endpoint_it == endpoints_.end()) {
        return ReceiveResult{false, false, "unknown endpoint handle"};
    }
    if (!endpoint_it->second.allow_receive) {
        return ReceiveResult{false, false, "endpoint is not receive-enabled"};
    }

    auto queue_it = stream_queues_.find(endpoint_it->second.stream_name);
    if (queue_it == stream_queues_.end() || queue_it->second.empty()) {
        return ReceiveResult{true, false, {}};
    }

    packet = std::move(queue_it->second.front());
    queue_it->second.pop_front();
    return ReceiveResult{true, true, {}};
}

} // namespace s7::transport


