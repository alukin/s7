#ifndef S7TRANSPORT_INCLUDE_S7_TRANSPORT_HPP
#define S7TRANSPORT_INCLUDE_S7_TRANSPORT_HPP

// Runtime network endpoints: lossless typed data streams
// (S7_Network_Wire_Protocol_v1.md) and the control channel.
//
// A producer stream sends what the graph publishes to every peer that
// RESUMEs it (only configured consumer nodes, when the stream names them); a
// consumer stream RESUMEs on every data connection to its producer node (any
// data connection when it names none) and accepts packets only there. Publishing blocks while the send log is full
// (up to max_block, then the stream faults); receiving waits for data (up to
// max_wait, then the stream faults with UNDERRUN).

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "s7/stream_protocol.hpp"

namespace s7::transport {

// Identity attributes of an X.509 certificate.
struct PeerCertificate {
    std::string fingerprint_sha256; // lower-case hex of the DER encoding
    std::vector<std::string> uri_sans;
    std::vector<std::string> key_purpose_oids; // Extended Key Usage, dotted form
};

// The authenticated peer of a connection, as established by the transport.
// certificate is set only for a TLS client certificate that passed chain
// verification against the server's trust roots.
struct ControlPeer {
    bool tls{false};
    std::optional<PeerCertificate> certificate;
};

// Reads identity attributes from a PEM certificate file.
[[nodiscard]] auto inspect_certificate_file(const std::string& pem_path) -> PeerCertificate;

// The peer certificate of an OpenSSL connection (an `SSL*`), if the peer
// presented one and it passed chain verification.
[[nodiscard]] auto verified_peer_certificate_of(void* openssl_ssl) -> std::optional<PeerCertificate>;

// The node identity of a certificate: the UUID of its single SAN URI
// spiffe://s7/node/<uuid>; nothing if it has none or more than one.
[[nodiscard]] auto node_uuid_of(const PeerCertificate& certificate) -> std::optional<std::string>;

// How a connection's peer relates to the node it claims to be.
enum class NodeIdentity : std::uint8_t {
    Verified,   // verified certificate whose node identity is the claimed node_uuid
    Unverified, // no verified certificate (plain WebSocket)
    Mismatch,   // verified certificate of another node, or without a node identity
};
[[nodiscard]] auto check_node_identity(const std::optional<PeerCertificate>& certificate, std::string_view claimed_node_uuid)
    -> NodeIdentity;
[[nodiscard]] auto to_string(NodeIdentity identity) -> std::string; // "verified", "unverified", "identity_mismatch"

// An outbound data peer: a node this one consumes streams from.
struct DataPeer {
    std::string url;
    std::string node_uuid; // expected identity of its certificate (empty: not checked)

    bool operator==(const DataPeer&) const = default;
};

struct ParsedWebSocketUrl {
    std::string host;
    std::string port;
    std::string target;
    bool secure{false};
};

struct TlsConfig {
    bool enabled{false};
    std::string cert_file;
    std::string key_file;
    std::string ca_file;
    bool require_client_cert{true};
};

[[nodiscard]] auto parse_ws_url(const std::string& url) -> ParsedWebSocketUrl;

struct StreamHandle {
    std::uint64_t value{};

    [[nodiscard]] bool valid() const noexcept { return value != 0; }
};

enum class StreamRole : std::uint8_t { Producer, Consumer };

struct RecentFault {
    stream::FaultCode code{stream::FaultCode::None};
    std::string detail;
    std::chrono::system_clock::time_point at;
};
inline constexpr std::size_t kMaxRecentFaults = 16;

struct StreamStatus {
    wire::StreamId stream_id{};
    StreamRole role{StreamRole::Producer};
    SignalType type{SignalType::F64};
    std::string state; // "active", "blocked", "faulted", "closed" (no element owns it)
    stream::FaultCode fault{stream::FaultCode::None};
    std::uint64_t next_sequence{}; // producer: next to assign; consumer: next expected
    std::uint64_t packets{};
    std::uint64_t elements{};
    std::uint64_t replayed{};
    std::uint64_t lost_resumes{}; // producer: consumers that resumed from a lost position
    std::uint64_t duplicates_discarded{};
    std::uint64_t malformed{};
    // Refused peers: producer: RESUMEs from a node that is not a configured
    // consumer; consumer: packets from a connection it did not RESUME on.
    std::uint64_t unauthorized{};
    // The latest faults and refusals, oldest first (at most kMaxRecentFaults);
    // kept across RECOVER_STREAM.
    std::vector<RecentFault> recent_faults;
    std::size_t send_log_depth{};
    std::size_t buffered_elements{};
    std::chrono::milliseconds blocked_total{};
};

struct PublishResult {
    std::size_t accepted{};   // all, unless the stream faulted
    stream::FaultCode fault{stream::FaultCode::None};
};

struct ReceiveResult {
    std::size_t received{};   // elements written to the output
    stream::FaultCode fault{stream::FaultCode::None};
};

struct TransportLimits {
    std::size_t max_inbound_connections{64}; // control + data, accepted by a server
};

struct TransportStats {
    std::size_t data_connections{};
    std::size_t control_connections{};
    std::uint64_t connections_rejected{};
    std::uint64_t messages_sent{};
    std::uint64_t messages_received{};
    std::uint64_t malformed_messages{};
};

class IRuntimeNetEndpoint {
public:
    virtual ~IRuntimeNetEndpoint() = default;

    [[nodiscard]] virtual auto backend_name() const -> std::string = 0;

    // A stream id may have one producer and one consumer per endpoint, each
    // open at most once at a time. A closed stream keeps its state: opening
    // it again with the same type continues it without loss (a redeployed
    // graph takes over the stream); another type starts it anew.
    virtual auto open_producer(const stream::StreamConfig& config) -> StreamHandle = 0;
    virtual auto open_consumer(const stream::StreamConfig& config) -> StreamHandle = 0;
    virtual void close_stream(StreamHandle handle) = 0;

    // Elements are host storage of the stream's type. Blocks while the send
    // log is full; after max_block the stream faults (SEND_LOG_OVERFLOW).
    virtual auto publish(StreamHandle handle, const void* elements, std::size_t count) -> PublishResult = 0;
    // Waits until `count` elements are available, up to max_wait; then the
    // stream faults (UNDERRUN). A faulted stream returns immediately.
    virtual auto receive(StreamHandle handle, void* elements, std::size_t count) -> ReceiveResult = 0;

    [[nodiscard]] virtual auto stream_statuses() const -> std::vector<StreamStatus> = 0;
    // Clears a stream's fault (RECOVER_STREAM). A consumer accepts the next
    // packet it receives as the new start; a producer resumes sending.
    virtual auto recover_stream(const wire::StreamId& stream_id) -> bool = 0;

    // Connection-level failures (e.g. malformed messages); returns and clears them.
    virtual auto take_transport_errors() -> std::vector<std::string> { return {}; }
    [[nodiscard]] virtual auto stats() const -> TransportStats { return {}; }
};

using RuntimeNetEndpointPtr = std::shared_ptr<IRuntimeNetEndpoint>;

// Producer and consumer streams of one process, connected directly.
class InMemoryRuntimeNetEndpoint final : public IRuntimeNetEndpoint {
public:
    InMemoryRuntimeNetEndpoint();
    ~InMemoryRuntimeNetEndpoint() override;

    [[nodiscard]] auto backend_name() const -> std::string override;
    auto open_producer(const stream::StreamConfig& config) -> StreamHandle override;
    auto open_consumer(const stream::StreamConfig& config) -> StreamHandle override;
    void close_stream(StreamHandle handle) override;
    auto publish(StreamHandle handle, const void* elements, std::size_t count) -> PublishResult override;
    auto receive(StreamHandle handle, void* elements, std::size_t count) -> ReceiveResult override;
    [[nodiscard]] auto stream_statuses() const -> std::vector<StreamStatus> override;
    auto recover_stream(const wire::StreamId& stream_id) -> bool override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

class BeastRuntimeNetEndpoint final : public IRuntimeNetEndpoint {
public:
    using ControlRequestHandler = std::function<std::string(const ControlPeer& peer, const std::string& request_json)>;

    // state_dir: where producer sequence reservations are kept (empty: in memory).
    explicit BeastRuntimeNetEndpoint(TransportLimits limits = {}, std::string state_dir = {});
    ~BeastRuntimeNetEndpoint() override;

    [[nodiscard]] auto backend_name() const -> std::string override;

    // This node's UUID. It is sent in the WebSocket handshake (header
    // S7-Node, both directions). Over TLS a peer is known by its verified
    // certificate and the header is ignored; over plain WebSocket the header
    // is the peer's (unverified) identity: it keeps consumers on different
    // nodes apart and selects which streams a connection may carry. Set it
    // before connecting.
    void set_node_uuid(const std::string& node_uuid);

    // Accepts any number of peers (up to the inbound limit). Port 0 lets the
    // system choose; returns the port listened on. An empty bind_address
    // listens on all interfaces with TLS and on loopback (127.0.0.1) without:
    // a plain WebSocket node is reachable from other hosts only if asked.
    auto configure_server(std::uint16_t port, const TlsConfig& tls = {}, const std::string& bind_address = {}) -> std::uint16_t;
    // Connects this endpoint's outbound control and data connections (replacing
    // previous ones) and keeps them connected: a dropped connection is
    // re-established with backoff and its streams resume. Throws if the first
    // connection attempt fails.
    void configure_client(const std::string& url, const TlsConfig& tls = {});
    // Keeps a /data connection to each of these peers (in addition to the
    // client connection), connecting new ones and closing those no longer
    // listed. A peer that cannot be reached is retried with backoff; this
    // call does not wait for connections.
    // Over TLS, a peer whose certificate is not the expected node is refused
    // (reported through take_transport_errors and retried).
    void set_data_peers(const std::vector<DataPeer>& peers, const TlsConfig& tls = {});

    // The verified certificate of the server this endpoint's client
    // connection (configure_client) is connected to.
    [[nodiscard]] auto server_certificate() const -> std::optional<PeerCertificate>;

    void set_control_request_handler(ControlRequestHandler handler);
    [[nodiscard]] auto request_control_json(const std::string& request_json) -> std::string;

    auto open_producer(const stream::StreamConfig& config) -> StreamHandle override;
    auto open_consumer(const stream::StreamConfig& config) -> StreamHandle override;
    void close_stream(StreamHandle handle) override;
    auto publish(StreamHandle handle, const void* elements, std::size_t count) -> PublishResult override;
    auto receive(StreamHandle handle, void* elements, std::size_t count) -> ReceiveResult override;
    [[nodiscard]] auto stream_statuses() const -> std::vector<StreamStatus> override;
    auto recover_stream(const wire::StreamId& stream_id) -> bool override;
    auto take_transport_errors() -> std::vector<std::string> override;
    [[nodiscard]] auto stats() const -> TransportStats override;

private:
    struct Impl;
    std::shared_ptr<Impl> impl_;
};

} // namespace s7::transport

#endif // S7TRANSPORT_INCLUDE_S7_TRANSPORT_HPP
