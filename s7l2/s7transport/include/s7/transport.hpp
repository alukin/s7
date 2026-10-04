#ifndef S7TRANSPORT_INCLUDE_S7_TRANSPORT_HPP
#define S7TRANSPORT_INCLUDE_S7_TRANSPORT_HPP

#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace s7::transport {

struct EndpointHandle {
    std::uint64_t value{};

    [[nodiscard]] auto valid() const noexcept -> bool
    {
        return value != 0;
    }
};

struct EndpointOpenRequest {
    std::string endpoint_name;
    std::string stream_name;
    bool allow_publish{true};
    bool allow_receive{true};
};

struct EndpointOpenResult {
    EndpointHandle handle;
    bool ok{false};
    std::string error;
};

// Data type code of a scalar F64 payload in the prototype packet layout (the
// only payload type it supports). The candidate wire format uses other codes.
constexpr std::uint16_t kPrototypeDataTypeF64 = 0x000C;

struct DataPacket {
    std::string stream_name;
    std::uint64_t sequence{};
    std::uint64_t stream_id{};
    std::uint32_t channel_id{};
    std::uint16_t data_type{kPrototypeDataTypeF64};
    // 0 lets the encoder derive the count from the payload size.
    std::uint32_t elem_count{};
    std::uint16_t flags{};
    // Required. Zero is a valid model time; absence is an encoding error.
    std::optional<std::uint64_t> t_model_ns;
    std::optional<std::uint64_t> t_system_ns;
    std::vector<std::uint8_t> extensions;
    std::vector<std::uint8_t> payload;
};

// Identity attributes of an X.509 certificate.
struct PeerCertificate {
    std::string fingerprint_sha256; // lower-case hex of the DER encoding
    std::vector<std::string> uri_sans;
    std::vector<std::string> policy_oids; // Certificate Policies, dotted form
};

// The authenticated peer of a control connection, as established by the
// transport. certificate is set only for a TLS client certificate that
// passed chain verification against the server's trust roots.
struct ControlPeer {
    bool tls{false};
    std::optional<PeerCertificate> certificate;
};

// Reads identity attributes from a PEM certificate file.
[[nodiscard]] auto inspect_certificate_file(const std::string& pem_path) -> PeerCertificate;

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

[[nodiscard]] auto encode_packet_prototype(const DataPacket& packet) -> std::vector<std::uint8_t>;
[[nodiscard]] auto decode_packet_prototype(const std::vector<std::uint8_t>& frame) -> DataPacket;
[[nodiscard]] auto encode_f64_payload(double value) -> std::vector<std::uint8_t>;
[[nodiscard]] auto decode_f64_payload(const std::vector<std::uint8_t>& payload) -> double;

struct SendResult {
    bool ok{false};
    std::string error;
    std::size_t queue_depth{};
    // False when the packet was accepted but had no receiver (no connected peer)
    // and was dropped. Only meaningful when ok is true.
    bool delivered{true};
};

struct ReceiveResult {
    bool ok{false};
    bool has_packet{false};
    std::string error;
};

class IRuntimeNetEndpoint {
public:
    virtual ~IRuntimeNetEndpoint() = default;

    [[nodiscard]] virtual auto backend_name() const -> std::string = 0;

    virtual auto open_endpoint(const EndpointOpenRequest& request) -> EndpointOpenResult = 0;
    virtual void close_endpoint(const EndpointHandle& handle) = 0;

    virtual auto publish(const EndpointHandle& handle, const DataPacket& packet) -> SendResult = 0;
    virtual auto receive(const EndpointHandle& handle, DataPacket& packet) -> ReceiveResult = 0;

    // Connection-level failures (for example a malformed frame from a peer,
    // which closes that peer's connection). They are not tied to one stream,
    // so they are reported here rather than through receive(). Returns and
    // clears the errors recorded since the last call (bounded; oldest dropped).
    virtual auto take_transport_errors() -> std::vector<std::string> { return {}; }
};

class InMemoryRuntimeNetEndpoint final : public IRuntimeNetEndpoint {
public:
    [[nodiscard]] auto backend_name() const -> std::string override;

    auto open_endpoint(const EndpointOpenRequest& request) -> EndpointOpenResult override;
    void close_endpoint(const EndpointHandle& handle) override;

    auto publish(const EndpointHandle& handle, const DataPacket& packet) -> SendResult override;
    auto receive(const EndpointHandle& handle, DataPacket& packet) -> ReceiveResult override;

private:
    struct EndpointState {
        std::string stream_name;
        bool allow_publish{true};
        bool allow_receive{true};
    };

    std::mutex mutex_;
    std::uint64_t next_handle_{1};
    std::unordered_map<std::uint64_t, EndpointState> endpoints_;
    std::unordered_map<std::string, std::deque<DataPacket>> stream_queues_;
};

class BeastRuntimeNetEndpoint final : public IRuntimeNetEndpoint {
public:
    using ControlRequestHandler = std::function<std::string(const ControlPeer& peer, const std::string& request_json)>;

    BeastRuntimeNetEndpoint();
    ~BeastRuntimeNetEndpoint() override;

    [[nodiscard]] auto backend_name() const -> std::string override;

    void configure_server(std::uint16_t port, const TlsConfig& tls = {});
    void configure_client(const std::string& url, const TlsConfig& tls = {});

    void set_control_request_handler(ControlRequestHandler handler);
    [[nodiscard]] auto request_control_json(const std::string& request_json) -> std::string;

    auto open_endpoint(const EndpointOpenRequest& request) -> EndpointOpenResult override;
    void close_endpoint(const EndpointHandle& handle) override;
    auto publish(const EndpointHandle& handle, const DataPacket& packet) -> SendResult override;
    auto receive(const EndpointHandle& handle, DataPacket& packet) -> ReceiveResult override;
    auto take_transport_errors() -> std::vector<std::string> override;

private:
    struct Impl;

    void record_transport_error(std::string error);

    struct EndpointState {
        std::string stream_name;
        bool allow_publish{true};
        bool allow_receive{true};
    };

    std::unique_ptr<Impl> impl_;
    std::mutex mutex_;
    std::uint64_t next_handle_{1};
    std::unordered_map<std::uint64_t, EndpointState> endpoints_;
    std::unordered_map<std::string, std::deque<DataPacket>> stream_queues_;
    std::deque<std::string> transport_errors_;
};

using RuntimeNetEndpointPtr = std::shared_ptr<IRuntimeNetEndpoint>;

} // namespace s7::transport

#endif // S7TRANSPORT_INCLUDE_S7_TRANSPORT_HPP
