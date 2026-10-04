#ifndef S7RUNTIME_INCLUDE_S7_RUNTIME_SERVICE_HPP
#define S7RUNTIME_INCLUDE_S7_RUNTIME_SERVICE_HPP

#include <atomic>
#include <chrono>
#include <cstdint>
#include <optional>
#include <ostream>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include <boost/json.hpp>

#include "s7/control_plane_mvp.hpp"
#include "s7/core.hpp"
#include "s7/transport.hpp"

namespace s7 {

struct JsonElementDefinition {
    std::string id;
    std::string type;
    boost::json::object params;
    boost::json::value nested_schema;
};

struct SchemaPortBinding {
    std::string name;
    std::string type;
    std::string element;
    std::uint64_t port{};
};

struct JsonConnectionDefinition {
    std::string from_element;
    std::uint64_t from_port{};
    std::string to_element;
    std::uint64_t to_port{};
};

struct WebSocketServerConfig {
    bool enabled{};
    std::uint16_t port{};
    transport::TlsConfig tls{};
};

struct WebSocketClientConfig {
    bool enabled{};
    std::string url;
    transport::TlsConfig tls{};
};

struct SimulationConfig {
    bool enabled{};
    std::uint64_t periods{};
    std::uint64_t sample_rate_hz{};
    double frequency_hz{};
};

// Continuous stepping of a served node. step_rate_hz == 0 means the node
// serves the control plane but does not step its graph.
struct ExecutionConfig {
    double step_rate_hz{};
};

struct RuntimeSchemaDefinition {
    std::string name;
    std::vector<JsonElementDefinition> elements;
    std::vector<JsonConnectionDefinition> connections;
    std::vector<SchemaPortBinding> exposed_inputs;
    std::vector<SchemaPortBinding> exposed_outputs;
    WebSocketServerConfig server;
    WebSocketClientConfig client;
    SimulationConfig simulation;
    ExecutionConfig execution;
};

class JsonSchemaLoader {
public:
    static RuntimeSchemaDefinition load_from_file(const std::string& file_path);
};

class RuntimeService {
public:
    // state_dir holds the node's persistent state (node_uuid.txt, peers.json)
    // and is locked for the life of the service; each node needs its own.
    RuntimeService(RuntimeSchemaDefinition definition, std::filesystem::path state_dir);
    ~RuntimeService();
    RuntimeService(const RuntimeService&) = delete;
    RuntimeService& operator=(const RuntimeService&) = delete;
    RuntimeService(RuntimeService&&) = delete;
    RuntimeService& operator=(RuntimeService&&) = delete;

    [[nodiscard]] const RuntimeSchemaDefinition& schema() const;
    [[nodiscard]] bool has_simulation() const;
    [[nodiscard]] std::uint64_t simulation_step_count() const;
    [[nodiscard]] std::vector<std::filesystem::path> output_files() const;
    [[nodiscard]] s7::Schema build_schema() const;
    [[nodiscard]] const std::string& runtime_instance_uuid() const;
    [[nodiscard]] static std::string create_schema_instance_uuid();
    [[nodiscard]] auto network_endpoint() const -> transport::RuntimeNetEndpointPtr;
    [[nodiscard]] auto handle_control_request_json(const std::string& request_json) -> std::string;
    void add_seed_peer(const std::string& endpoint);
    void print_summary(std::ostream& out) const;

    void start_websocket_server() const;
    void connect_websocket_client() const;

private:
    RuntimeSchemaDefinition definition_;
    std::filesystem::path state_dir_;
    int state_dir_lock_fd_{-1};
    std::string runtime_instance_uuid_;
    std::shared_ptr<ControlPlaneMvp> control_plane_;
    transport::RuntimeNetEndpointPtr network_endpoint_;
    mutable std::vector<transport::EndpointHandle> opened_server_endpoints_;
    mutable std::vector<transport::EndpointHandle> opened_client_endpoints_;
};

struct StepLoopStats {
    enum class StopReason { StopRequested, Deadline };

    std::uint64_t steps{};
    double elapsed_seconds{};
    StopReason stop_reason{StopReason::StopRequested};
};

// Steps the runtime at step_rate_hz on the calling thread until stop_requested
// is set or the deadline passes. If stepping falls behind, the backlog is
// dropped rather than replayed in a burst.
StepLoopStats run_step_loop(NodeRuntime& runtime,
                            double step_rate_hz,
                            const std::atomic<bool>& stop_requested,
                            std::optional<std::chrono::steady_clock::time_point> deadline = std::nullopt);

} // namespace s7

#endif // S7RUNTIME_INCLUDE_S7_RUNTIME_SERVICE_HPP
