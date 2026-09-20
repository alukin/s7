#ifndef S7TRANSPORT_INCLUDE_S7_TRANSPORT_HPP
#define S7TRANSPORT_INCLUDE_S7_TRANSPORT_HPP

#include <cstddef>
#include <cstdint>
#include <deque>
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

struct DataPacket {
    std::string stream_name;
    std::uint64_t sequence{};
    std::uint64_t stream_id{};
    std::uint32_t channel_id{};
    std::uint16_t data_type{0x000C}; // F64
    std::uint32_t elem_count{};
    std::uint16_t flags{};
    std::uint64_t t_model_ns{};
    std::optional<std::uint64_t> t_system_ns;
    std::vector<std::uint8_t> extensions;
    double t_model{};
    std::vector<std::uint8_t> payload;
};

struct ParsedWebSocketUrl {
    std::string host;
    std::string port;
    std::string target;
};

[[nodiscard]] auto parse_ws_url(const std::string& url) -> ParsedWebSocketUrl;

[[nodiscard]] auto encode_packet_v1(const DataPacket& packet) -> std::vector<std::uint8_t>;
[[nodiscard]] auto decode_packet_v1(const std::vector<std::uint8_t>& frame) -> DataPacket;
[[nodiscard]] auto encode_f64_payload(double value) -> std::vector<std::uint8_t>;
[[nodiscard]] auto decode_f64_payload(const std::vector<std::uint8_t>& payload) -> double;

struct SendResult {
    bool ok{false};
    std::string error;
    std::size_t queue_depth{};
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
    BeastRuntimeNetEndpoint();
    ~BeastRuntimeNetEndpoint() override;

    [[nodiscard]] auto backend_name() const -> std::string override;

    void configure_server(std::uint16_t port);
    void configure_client(const std::string& url);

    auto open_endpoint(const EndpointOpenRequest& request) -> EndpointOpenResult override;
    void close_endpoint(const EndpointHandle& handle) override;
    auto publish(const EndpointHandle& handle, const DataPacket& packet) -> SendResult override;
    auto receive(const EndpointHandle& handle, DataPacket& packet) -> ReceiveResult override;

private:
    struct Impl;
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
};

using RuntimeNetEndpointPtr = std::shared_ptr<IRuntimeNetEndpoint>;

} // namespace s7::transport

#endif // S7TRANSPORT_INCLUDE_S7_TRANSPORT_HPP

