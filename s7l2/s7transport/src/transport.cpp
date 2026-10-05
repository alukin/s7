#include "s7/transport.hpp"

#include <bit>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cmath>
#include <cstring>
#include <functional>
#include <iterator>
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

#include <openssl/evp.h>
#include <openssl/objects.h>
#include <openssl/pem.h>
#include <openssl/ssl.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>

namespace s7::transport {
namespace {

using Tcp = boost::asio::ip::tcp;
namespace websocket = boost::beast::websocket;
namespace http = boost::beast::http;
namespace ssl = boost::asio::ssl;
using boost::beast::flat_buffer;
using PlainWs = websocket::stream<Tcp::socket>;
using TlsWs = websocket::stream<boost::beast::ssl_stream<Tcp::socket>>;

void interrupt_socket_reads(const std::shared_ptr<PlainWs>& socket)
{
    if (!socket) {
        return;
    }
    boost::system::error_code ec;
    static_cast<void>(socket->next_layer().cancel(ec));
    static_cast<void>(socket->next_layer().shutdown(Tcp::socket::shutdown_both, ec));
    static_cast<void>(socket->next_layer().close(ec));
}

void interrupt_socket_reads(const std::shared_ptr<TlsWs>& socket)
{
    if (!socket) {
        return;
    }
    boost::system::error_code ec;
    static_cast<void>(socket->next_layer().next_layer().cancel(ec));
    static_cast<void>(socket->next_layer().next_layer().shutdown(Tcp::socket::shutdown_both, ec));
    static_cast<void>(socket->next_layer().next_layer().close(ec));
}

constexpr std::uint16_t kProtocolVersion = 1;
constexpr std::size_t kFixedHeaderBytes = 54;
constexpr std::size_t kMaxPayloadBytes = 256u * 1024u;
constexpr std::uint16_t kFlagHasSystemTimestamp = 0x0001u;
constexpr std::size_t kF64Bytes = 8;

void validate_payload_shape(const std::uint16_t data_type, const std::uint32_t elem_count, const std::size_t payload_size)
{
    if (data_type != kPrototypeDataTypeF64) {
        throw std::invalid_argument("Unsupported packet data type: " + std::to_string(data_type));
    }
    if (static_cast<std::uint64_t>(elem_count) * kF64Bytes != payload_size) {
        throw std::invalid_argument("Packet payload size " + std::to_string(payload_size) +
                                    " does not match element count " + std::to_string(elem_count) + ".");
    }
}

void append_u16_be(std::vector<std::uint8_t>& out, const std::uint16_t value)
{
    out.push_back(static_cast<std::uint8_t>((value >> 8) & 0xFFu));
    out.push_back(static_cast<std::uint8_t>(value & 0xFFu));
}

void append_u32_be(std::vector<std::uint8_t>& out, const std::uint32_t value)
{
    for (std::uint32_t shift = 24; shift > 0; shift -= 8) {
        out.push_back(static_cast<std::uint8_t>((value >> shift) & 0xFFu));
    }
    out.push_back(static_cast<std::uint8_t>(value & 0xFFu));
}

void append_u64_be(std::vector<std::uint8_t>& out, const std::uint64_t value)
{
    for (std::uint64_t shift = 56; shift > 0; shift -= 8) {
        out.push_back(static_cast<std::uint8_t>((value >> shift) & 0xFFu));
    }
    out.push_back(static_cast<std::uint8_t>(value & 0xFFu));
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
    for (std::size_t index = 0; index < 4; ++index) {
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
    for (std::size_t index = 0; index < 8; ++index) {
        value = (value << 8) | static_cast<std::uint64_t>(in[offset + index]);
    }
    offset += 8;
    return value;
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

[[nodiscard]] auto peer_certificate_from_x509(X509* certificate) -> PeerCertificate
{
    PeerCertificate info;

    unsigned char digest[EVP_MAX_MD_SIZE];
    unsigned int digest_len = 0;
    if (X509_digest(certificate, EVP_sha256(), digest, &digest_len) != 1) {
        throw std::runtime_error("Failed to compute certificate fingerprint.");
    }
    static constexpr char hex[] = "0123456789abcdef";
    for (unsigned int index = 0; index < digest_len; ++index) {
        info.fingerprint_sha256.push_back(hex[digest[index] >> 4]);
        info.fingerprint_sha256.push_back(hex[digest[index] & 0x0F]);
    }

    const auto names_deleter = [](GENERAL_NAMES* names) { GENERAL_NAMES_free(names); };
    std::unique_ptr<GENERAL_NAMES, decltype(names_deleter)> names(
        static_cast<GENERAL_NAMES*>(X509_get_ext_d2i(certificate, NID_subject_alt_name, nullptr, nullptr)),
        names_deleter);
    for (int index = 0; names && index < sk_GENERAL_NAME_num(names.get()); ++index) {
        const GENERAL_NAME* name = sk_GENERAL_NAME_value(names.get(), index);
        if (name->type == GEN_URI) {
            const ASN1_IA5STRING* uri = name->d.uniformResourceIdentifier;
            info.uri_sans.emplace_back(reinterpret_cast<const char*>(ASN1_STRING_get0_data(uri)),
                                       static_cast<std::size_t>(ASN1_STRING_length(uri)));
        }
    }

    const auto usages_deleter = [](EXTENDED_KEY_USAGE* usages) { EXTENDED_KEY_USAGE_free(usages); };
    std::unique_ptr<EXTENDED_KEY_USAGE, decltype(usages_deleter)> usages(
        static_cast<EXTENDED_KEY_USAGE*>(X509_get_ext_d2i(certificate, NID_ext_key_usage, nullptr, nullptr)),
        usages_deleter);
    for (int index = 0; usages && index < sk_ASN1_OBJECT_num(usages.get()); ++index) {
        char oid[128];
        const int length = OBJ_obj2txt(oid, sizeof(oid), sk_ASN1_OBJECT_value(usages.get(), index), 1);
        if (length > 0 && static_cast<std::size_t>(length) < sizeof(oid)) {
            info.key_purpose_oids.emplace_back(oid, static_cast<std::size_t>(length));
        }
    }
    return info;
}

// The verified client certificate of a TLS session, if any.
[[nodiscard]] auto verified_peer_certificate(SSL* ssl) -> std::optional<PeerCertificate>
{
    std::unique_ptr<X509, decltype(&X509_free)> certificate(SSL_get1_peer_certificate(ssl), X509_free);
    if (!certificate || SSL_get_verify_result(ssl) != X509_V_OK) {
        return std::nullopt;
    }
    return peer_certificate_from_x509(certificate.get());
}

} // namespace

auto inspect_certificate_file(const std::string& pem_path) -> PeerCertificate
{
    std::unique_ptr<BIO, decltype(&BIO_free)> bio(BIO_new_file(pem_path.c_str(), "r"), BIO_free);
    if (!bio) {
        throw std::runtime_error("Could not open certificate: '" + pem_path + "'.");
    }
    std::unique_ptr<X509, decltype(&X509_free)> certificate(PEM_read_bio_X509(bio.get(), nullptr, nullptr, nullptr), X509_free);
    if (!certificate) {
        throw std::runtime_error("Could not parse PEM certificate: '" + pem_path + "'.");
    }
    return peer_certificate_from_x509(certificate.get());
}

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

auto encode_packet_prototype(const DataPacket& packet) -> std::vector<std::uint8_t>
{
    if ((packet.flags & static_cast<std::uint16_t>(~kFlagHasSystemTimestamp)) != 0u) {
        throw std::invalid_argument("Packet contains unsupported prototype flags.");
    }
    if (packet.payload.size() > kMaxPayloadBytes) {
        throw std::invalid_argument("Payload exceeds prototype packet size limit.");
    }
    if (packet.stream_name.size() > 0xFFFFu) {
        throw std::invalid_argument("Stream name exceeds prototype extension encoding limit.");
    }

    const auto extension_bytes = static_cast<std::size_t>(2u + packet.stream_name.size() + packet.extensions.size());
    if (extension_bytes > 0xFFFFu) {
        throw std::invalid_argument("Extension area exceeds prototype format limit.");
    }

    if (!packet.t_model_ns) {
        throw std::invalid_argument("Packet has no model timestamp.");
    }
    const auto t_model_ns = *packet.t_model_ns;

    const auto has_system_timestamp = packet.t_system_ns.has_value();
    if ((packet.flags & kFlagHasSystemTimestamp) != 0u && !has_system_timestamp) {
        throw std::invalid_argument("Packet sets the system timestamp flag without a system timestamp.");
    }
    const auto flags = static_cast<std::uint16_t>(packet.flags | (has_system_timestamp ? kFlagHasSystemTimestamp : 0u));
    const auto elem_count = packet.elem_count != 0
        ? packet.elem_count
        : static_cast<std::uint32_t>(packet.payload.size() / kF64Bytes);
    validate_payload_shape(packet.data_type, elem_count, packet.payload.size());

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

auto decode_packet_prototype(const std::vector<std::uint8_t>& frame) -> DataPacket
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
    if ((packet.flags & static_cast<std::uint16_t>(~kFlagHasSystemTimestamp)) != 0u) {
        throw std::invalid_argument("Packet contains unsupported prototype flags.");
    }
    if (reserved != 0) {
        throw std::invalid_argument("Packet reserved field must be zero in the prototype format.");
    }
    if ((packet.flags & kFlagHasSystemTimestamp) == 0u && t_system_ns != 0u) {
        throw std::invalid_argument("Packet has a system timestamp value without the presence flag.");
    }

    if (payload_size > kMaxPayloadBytes) {
        throw std::invalid_argument("Packet payload exceeds prototype packet size limit.");
    }

    if (offset > frame.size()) {
        throw std::invalid_argument("Packet decode offset exceeds frame size.");
    }
    const auto remaining = frame.size() - offset;
    if (static_cast<std::size_t>(ext_len) > remaining) {
        throw std::invalid_argument("Packet extension length exceeds frame size.");
    }
    if (static_cast<std::size_t>(payload_size) > (remaining - static_cast<std::size_t>(ext_len))) {
        throw std::invalid_argument("Packet payload/extension lengths exceed frame size.");
    }
    if (static_cast<std::size_t>(ext_len) + static_cast<std::size_t>(payload_size) != remaining) {
        throw std::invalid_argument("Packet frame contains trailing bytes after the declared payload.");
    }
    validate_payload_shape(packet.data_type, packet.elem_count, payload_size);
    if (ext_len == 1) {
        throw std::invalid_argument("Packet extension area is too short for the stream-name length.");
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
    // Round-trip through the codec so in-memory delivery applies the same
    // validation as a network peer.
    try {
        packet_to_queue = decode_packet_prototype(encode_packet_prototype(packet_to_queue));
    } catch (const std::exception& ex) {
        return SendResult{false, std::string{"invalid data packet: "} + ex.what(), 0};
    }

    receive_queues_.push(std::move(packet_to_queue));
    return SendResult{true, {}, 0};
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

    const auto has_packet = receive_queues_.pop(endpoint_it->second.stream_name, packet);
    return ReceiveResult{true, has_packet, {}};
}

auto InMemoryRuntimeNetEndpoint::stats() const -> TransportStats
{
    std::lock_guard<std::mutex> lock(mutex_);
    TransportStats stats;
    receive_queues_.add_to(stats);
    return stats;
}

void ReceiveQueues::push(DataPacket packet)
{
    ++received_;
    auto it = queues_.find(packet.stream_name);
    if (it == queues_.end()) {
        // Drop emptied queues before refusing a new stream.
        std::erase_if(queues_, [](const auto& entry) { return entry.second.empty(); });
        if (queues_.size() >= limits_->max_receive_streams) {
            ++dropped_stream_limit_;
            return;
        }
        it = queues_.emplace(packet.stream_name, std::deque<DataPacket>{}).first;
    }
    auto& queue = it->second;
    if (queue.size() >= limits_->max_receive_queue_packets) {
        queue.pop_front();
        ++dropped_queue_full_;
    }
    queue.push_back(std::move(packet));
}

auto ReceiveQueues::pop(const std::string& stream_name, DataPacket& packet) -> bool
{
    const auto it = queues_.find(stream_name);
    if (it == queues_.end() || it->second.empty()) {
        return false;
    }
    packet = std::move(it->second.front());
    it->second.pop_front();
    return true;
}

void ReceiveQueues::add_to(TransportStats& stats) const
{
    stats.packets_received += received_;
    stats.receive_dropped_queue_full += dropped_queue_full_;
    stats.receive_dropped_stream_limit += dropped_stream_limit_;
}

// One WebSocket connection (inbound or outbound, control or data). A data
// connection owns a bounded send queue drained by its writer thread; its
// reader thread queues received packets. An inbound control connection's
// reader answers requests. Threads never join themselves: closed connections
// are reaped by the accept thread, configure_client(), or the destructor.
struct BeastRuntimeNetEndpoint::Connection {
    bool is_data{false};
    bool outbound{false};
    ControlPeer peer;
    std::shared_ptr<PlainWs> plain;
    std::shared_ptr<TlsWs> tls;

    std::mutex write_mutex; // one writer at a time (writer thread or control replies)
    std::mutex queue_mutex;
    std::condition_variable queue_cv;
    std::deque<std::shared_ptr<const std::vector<std::uint8_t>>> send_queue;
    std::atomic<bool> closed{false};
    std::thread reader;
    std::thread writer;

    void close()
    {
        if (closed.exchange(true)) {
            queue_cv.notify_all();
            return;
        }
        if (tls) {
            interrupt_socket_reads(tls);
        } else {
            interrupt_socket_reads(plain);
        }
        queue_cv.notify_all();
    }

    void write(const std::string_view bytes, const bool binary, boost::system::error_code& ec)
    {
        std::lock_guard<std::mutex> lock(write_mutex);
        if (tls) {
            tls->binary(binary);
            tls->write(boost::asio::buffer(bytes.data(), bytes.size()), ec);
        } else {
            plain->binary(binary);
            plain->write(boost::asio::buffer(bytes.data(), bytes.size()), ec);
        }
    }

    auto read(boost::system::error_code& ec) -> std::string
    {
        flat_buffer buffer;
        if (tls) {
            tls->read(buffer, ec);
        } else {
            plain->read(buffer, ec);
        }
        return ec ? std::string{} : boost::beast::buffers_to_string(buffer.data());
    }

    void join()
    {
        if (reader.joinable()) {
            reader.join();
        }
        if (writer.joinable()) {
            writer.join();
        }
    }
};

struct BeastRuntimeNetEndpoint::Impl {
    std::shared_ptr<boost::asio::io_context> io_context = std::make_shared<boost::asio::io_context>();
    std::shared_ptr<ssl::context> server_ssl_context;
    std::shared_ptr<ssl::context> client_ssl_context;
    bool server_tls_enabled{false};
    std::mutex lifecycle_mutex;
    std::unique_ptr<Tcp::acceptor> acceptor;
    std::thread accept_thread;
    std::atomic<bool> stop{false};
    std::optional<std::uint16_t> server_port;

    std::mutex control_handler_mutex;
    ControlRequestHandler control_request_handler;
    std::mutex control_request_mutex; // one outbound request/response at a time

    std::mutex connections_mutex;
    std::vector<std::shared_ptr<Connection>> connections;

    std::atomic<std::uint64_t> connections_rejected{0};
    std::atomic<std::uint64_t> packets_sent{0};
    std::atomic<std::uint64_t> send_dropped_no_peer{0};
    std::atomic<std::uint64_t> send_dropped_queue_full{0};

    // Removes closed connections and returns them for joining outside the lock.
    auto take_closed_connections() -> std::vector<std::shared_ptr<Connection>>
    {
        std::vector<std::shared_ptr<Connection>> closed;
        std::lock_guard<std::mutex> lock(connections_mutex);
        std::erase_if(connections, [&closed](const std::shared_ptr<Connection>& connection) {
            if (connection->closed.load()) {
                closed.push_back(connection);
                return true;
            }
            return false;
        });
        return closed;
    }

    void reap_closed_connections()
    {
        for (const auto& connection : take_closed_connections()) {
            connection->join();
        }
    }

    auto open_outbound(const bool is_data) -> std::shared_ptr<Connection>
    {
        std::lock_guard<std::mutex> lock(connections_mutex);
        for (const auto& connection : connections) {
            if (connection->outbound && connection->is_data == is_data && !connection->closed.load()) {
                return connection;
            }
        }
        return nullptr;
    }
};

BeastRuntimeNetEndpoint::BeastRuntimeNetEndpoint(TransportLimits limits)
    : limits_(limits)
    , impl_(std::make_unique<Impl>())
{
}

BeastRuntimeNetEndpoint::~BeastRuntimeNetEndpoint()
{
    impl_->stop = true;
    {
        std::lock_guard<std::mutex> lock(impl_->lifecycle_mutex);
        if (impl_->acceptor) {
            boost::system::error_code ec;
            impl_->acceptor->close(ec);
        }
    }
    if (impl_->accept_thread.joinable()) {
        impl_->accept_thread.join();
    }

    std::vector<std::shared_ptr<Connection>> connections;
    {
        std::lock_guard<std::mutex> lock(impl_->connections_mutex);
        connections.swap(impl_->connections);
    }
    for (const auto& connection : connections) {
        connection->close();
    }
    for (const auto& connection : connections) {
        connection->join();
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

void BeastRuntimeNetEndpoint::handle_data_frame(const std::string& bytes, Connection& connection)
{
    const std::vector<std::uint8_t> frame(bytes.begin(), bytes.end());
    DataPacket packet;
    try {
        packet = decode_packet_prototype(frame);
    } catch (const std::exception& ex) {
        // A malformed frame closes only this peer's connection.
        record_transport_error(std::string{"invalid data packet: "} + ex.what());
        connection.close();
        return;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    receive_queues_.push(std::move(packet));
}

void BeastRuntimeNetEndpoint::start_connection(const std::shared_ptr<Connection>& connection)
{
    auto* conn = connection.get();
    if (connection->is_data) {
        connection->reader = std::thread([this, conn] {
            while (!impl_->stop.load() && !conn->closed.load()) {
                boost::system::error_code ec;
                const auto bytes = conn->read(ec);
                if (ec) {
                    break;
                }
                handle_data_frame(bytes, *conn);
            }
            conn->close();
        });
        connection->writer = std::thread([this, conn] {
            while (true) {
                std::shared_ptr<const std::vector<std::uint8_t>> frame;
                {
                    std::unique_lock<std::mutex> lock(conn->queue_mutex);
                    conn->queue_cv.wait(lock, [conn] { return conn->closed.load() || !conn->send_queue.empty(); });
                    if (conn->closed.load()) {
                        return;
                    }
                    frame = std::move(conn->send_queue.front());
                    conn->send_queue.pop_front();
                }
                boost::system::error_code ec;
                conn->write({reinterpret_cast<const char*>(frame->data()), frame->size()}, true, ec);
                if (ec) {
                    conn->close();
                    return;
                }
                impl_->packets_sent.fetch_add(1);
            }
        });
    } else if (!connection->outbound) {
        connection->reader = std::thread([this, conn] {
            while (!impl_->stop.load() && !conn->closed.load()) {
                boost::system::error_code ec;
                const auto request_json = conn->read(ec);
                if (ec) {
                    break;
                }
                ControlRequestHandler handler;
                {
                    std::lock_guard<std::mutex> lock(impl_->control_handler_mutex);
                    handler = impl_->control_request_handler;
                }
                std::string response_json;
                try {
                    response_json = handler
                        ? handler(conn->peer, request_json)
                        : std::string{R"json({"type":"ERROR","protocol_version":1,"code":"INTERNAL_ERROR","message":"No control handler registered"})json"};
                } catch (...) {
                    response_json = R"json({"type":"ERROR","protocol_version":1,"code":"INTERNAL_ERROR","message":"Control handler failed"})json";
                }
                conn->write(response_json, false, ec);
                if (ec) {
                    break;
                }
            }
            conn->close();
        });
    }

    std::lock_guard<std::mutex> lock(impl_->connections_mutex);
    impl_->connections.push_back(connection);
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
    if (impl_->accept_thread.joinable()) {
        throw std::logic_error("BeastRuntimeNetEndpoint server is already listening on another port.");
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
            impl_->reap_closed_connections();

            Tcp::socket socket(*impl_->io_context);
            boost::system::error_code ec;
            impl_->acceptor->accept(socket, ec);
            if (ec == boost::asio::error::would_block || ec == boost::asio::error::try_again) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                continue;
            }
            if (ec == boost::asio::error::operation_aborted || ec == boost::asio::error::bad_descriptor) {
                break;
            }
            if (ec) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                continue;
            }
            socket.non_blocking(false, ec);

            std::size_t inbound = 0;
            {
                std::lock_guard<std::mutex> lock(impl_->connections_mutex);
                inbound = static_cast<std::size_t>(std::count_if(
                    impl_->connections.begin(), impl_->connections.end(),
                    [](const auto& connection) { return !connection->outbound && !connection->closed.load(); }));
            }
            if (inbound >= limits_.max_inbound_connections) {
                impl_->connections_rejected.fetch_add(1);
                socket.close(ec);
                continue;
            }

            auto connection = std::make_shared<Connection>();
            std::string target;
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
                target = std::string(request.target());
                connection->peer = ControlPeer{.tls = true,
                                               .certificate = verified_peer_certificate(ws->next_layer().native_handle())};
                connection->tls = std::move(ws);
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
                target = std::string(request.target());
                connection->plain = std::move(ws);
            }

            const auto is_data = target.ends_with("/data");
            if (!is_data && !target.ends_with("/control")) {
                boost::system::error_code close_ec;
                if (connection->tls) {
                    connection->tls->close(websocket::close_code::policy_error, close_ec);
                } else {
                    connection->plain->close(websocket::close_code::policy_error, close_ec);
                }
                continue;
            }
            connection->is_data = is_data;
            start_connection(connection);
        }
    });
}

void BeastRuntimeNetEndpoint::configure_client(const std::string& url, const TlsConfig& tls)
{
    const auto parsed = parse_ws_url(url);
    std::lock_guard<std::mutex> lifecycle_lock(impl_->lifecycle_mutex);

    const auto tls_enabled = parsed.secure || tls.enabled;
    if (tls_enabled) {
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

    // Reconnect: close the previous outbound connections first.
    {
        std::lock_guard<std::mutex> lock(impl_->connections_mutex);
        for (const auto& connection : impl_->connections) {
            if (connection->outbound) {
                connection->close();
            }
        }
    }
    impl_->reap_closed_connections();

    auto connect = [&](const std::string_view suffix, const bool is_data) {
        Tcp::resolver resolver(*impl_->io_context);
        const auto results = resolver.resolve(parsed.host, parsed.port);
        const auto target = ws_target_for_path(parsed, suffix);
        auto connection = std::make_shared<Connection>();
        connection->is_data = is_data;
        connection->outbound = true;
        if (tls_enabled) {
            auto ws = std::make_shared<TlsWs>(*impl_->io_context, *impl_->client_ssl_context);
            boost::asio::connect(ws->next_layer().next_layer(), results.begin(), results.end());
            ws->next_layer().handshake(ssl::stream_base::client);
            ws->handshake(parsed.host + ":" + parsed.port, target);
            connection->tls = std::move(ws);
        } else {
            auto ws = std::make_shared<PlainWs>(*impl_->io_context);
            boost::asio::connect(ws->next_layer(), results.begin(), results.end());
            ws->handshake(parsed.host + ":" + parsed.port, target);
            connection->plain = std::move(ws);
        }
        start_connection(connection);
    };
    connect("/control", false);
    connect("/data", true);
}

auto BeastRuntimeNetEndpoint::request_control_json(const std::string& request_json) -> std::string
{
    const auto connection = impl_->open_outbound(false);
    if (!connection) {
        throw std::runtime_error("control channel is not connected");
    }

    std::lock_guard<std::mutex> call_lock(impl_->control_request_mutex);
    const websocket::stream_base::timeout timeout{std::chrono::seconds(5), std::chrono::seconds(5), false};
    if (connection->tls) {
        connection->tls->set_option(timeout);
    } else {
        connection->plain->set_option(timeout);
    }
    boost::system::error_code ec;
    connection->write(request_json, false, ec);
    if (ec) {
        connection->close();
        throw std::runtime_error("control write failed: " + ec.message());
    }
    auto response = connection->read(ec);
    if (ec) {
        connection->close();
        throw std::runtime_error("control read failed: " + ec.message());
    }
    return response;
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

// Queues the packet for every connected data peer without blocking. Streams
// are not yet routed per peer: that needs the stream descriptors of the
// committed configuration (wire draft section 12.1); receivers keep only the
// streams they queue within their limits.
auto BeastRuntimeNetEndpoint::publish(const EndpointHandle& handle, const DataPacket& packet) -> SendResult
{
    if (!handle.valid()) {
        return SendResult{false, "invalid handle", 0};
    }

    std::string stream_name;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto endpoint_it = endpoints_.find(handle.value);
        if (endpoint_it == endpoints_.end()) {
            return SendResult{false, "unknown endpoint handle", 0};
        }
        if (!endpoint_it->second.allow_publish) {
            return SendResult{false, "endpoint is not publish-enabled", 0};
        }
        stream_name = endpoint_it->second.stream_name;
    }

    auto packet_to_send = packet;
    if (packet_to_send.stream_name.empty()) {
        packet_to_send.stream_name = stream_name;
    }
    std::shared_ptr<const std::vector<std::uint8_t>> frame;
    try {
        frame = std::make_shared<const std::vector<std::uint8_t>>(encode_packet_prototype(packet_to_send));
    } catch (const std::exception& ex) {
        return SendResult{false, std::string{"invalid data packet: "} + ex.what(), 0};
    }

    std::vector<std::shared_ptr<Connection>> peers;
    {
        std::lock_guard<std::mutex> lock(impl_->connections_mutex);
        for (const auto& connection : impl_->connections) {
            if (connection->is_data && !connection->closed.load()) {
                peers.push_back(connection);
            }
        }
    }
    if (peers.empty()) {
        impl_->send_dropped_no_peer.fetch_add(1);
        return SendResult{true, {}, 0, false};
    }

    std::size_t deepest = 0;
    for (const auto& peer : peers) {
        {
            std::lock_guard<std::mutex> lock(peer->queue_mutex);
            if (peer->send_queue.size() >= limits_.max_send_queue_packets) {
                peer->send_queue.pop_front();
                impl_->send_dropped_queue_full.fetch_add(1);
            }
            peer->send_queue.push_back(frame);
            deepest = std::max(deepest, peer->send_queue.size());
        }
        peer->queue_cv.notify_one();
    }
    return SendResult{true, {}, deepest, true};
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
    const auto has_packet = receive_queues_.pop(endpoint_it->second.stream_name, packet);
    return ReceiveResult{true, has_packet, {}};
}

auto BeastRuntimeNetEndpoint::stats() const -> TransportStats
{
    TransportStats stats;
    {
        std::lock_guard<std::mutex> lock(impl_->connections_mutex);
        for (const auto& connection : impl_->connections) {
            if (connection->closed.load()) {
                continue;
            }
            if (connection->is_data) {
                ++stats.data_connections;
            } else {
                ++stats.control_connections;
            }
        }
    }
    stats.connections_rejected = impl_->connections_rejected.load();
    stats.packets_sent = impl_->packets_sent.load();
    stats.send_dropped_no_peer = impl_->send_dropped_no_peer.load();
    stats.send_dropped_queue_full = impl_->send_dropped_queue_full.load();
    std::lock_guard<std::mutex> lock(mutex_);
    receive_queues_.add_to(stats);
    return stats;
}

void BeastRuntimeNetEndpoint::record_transport_error(std::string error)
{
    constexpr std::size_t max_recorded_errors = 32;
    std::lock_guard<std::mutex> lock(mutex_);
    if (transport_errors_.size() == max_recorded_errors) {
        transport_errors_.pop_front();
    }
    transport_errors_.push_back(std::move(error));
}

auto BeastRuntimeNetEndpoint::take_transport_errors() -> std::vector<std::string>
{
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<std::string> errors(std::make_move_iterator(transport_errors_.begin()),
                                    std::make_move_iterator(transport_errors_.end()));
    transport_errors_.clear();
    return errors;
}

} // namespace s7::transport
