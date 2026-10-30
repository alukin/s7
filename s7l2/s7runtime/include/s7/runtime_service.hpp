// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: LGPL-3.0-or-later

#ifndef S7RUNTIME_INCLUDE_S7_RUNTIME_SERVICE_HPP
#define S7RUNTIME_INCLUDE_S7_RUNTIME_SERVICE_HPP

#include <atomic>
#include <chrono>
#include <cstdint>
#include <optional>
#include <ostream>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include <boost/json.hpp>

#include "s7/control_plane.hpp"
#include "s7/core.hpp"
#include "s7/platform.hpp"
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
    // Empty: all interfaces with TLS, loopback only without (plain WebSocket
    // is reachable from other hosts only when an address is given).
    std::string bind_address;
    transport::TlsConfig tls{};
    std::string advertise_address{}; // reachable hostname/address, when bind_address is a wildcard
};

struct DataServerConfig {
    bool enabled{};
    std::uint16_t port{};
    std::string bind_address;
    std::string advertise_address;
    std::string security{"mtls"}; // "mtls" or explicit "trusted_plaintext"
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

// The node's clock: "clock": { "max_error_ms": 250, "assume_synchronized": false }.
// Its UTC times count as the shared "utc" domain while the kernel reports it
// NTP-synchronized with a worst-case error within max_error_ms.
// assume_synchronized skips the kernel check (tests and hosts whose
// synchronization the kernel does not know about).
struct ClockConfig {
    double max_error_ms{250.0};
    bool assume_synchronized{false};
};

struct RuntimeSchemaDefinition;

struct RuntimeSchemaDefinition {
    std::string name;
    // The schema's own model-time step in seconds (doc/S7_Sample_Rate_Model.md):
    // "dt": 0.001, seeding Schema::set_rate() with no other effect (unlike
    // execution.step_rate_hz/simulation.sample_rate_hz, which also pace real-time
    // serving and are used as a fallback seed when this is absent).
    std::optional<double> dt;
    std::vector<JsonElementDefinition> elements;
    std::vector<JsonConnectionDefinition> connections;
    std::vector<SchemaPortBinding> exposed_inputs;
    std::vector<SchemaPortBinding> exposed_outputs;
    WebSocketServerConfig server;
    DataServerConfig data_server;
    WebSocketClientConfig client;
    SimulationConfig simulation;
    ExecutionConfig execution;
    ClockConfig clock;
    // Node settings, not deployable: "control": { "studio_key_purpose_oid": "...",
    // "studio_certificate_sha256": "<hex>" } names the authorized Studio.
    ControlAuthorization control;
};

// How the active graph is stepped while the node serves (RuntimeService::serve).
// grid: a deployed graph with a model-time step steps in real time on a UTC
// grid, step n due at origin_utc_ns + n * period_ns; when it falls behind it
// catches up (every step runs; the lag is reported). Otherwise the node's
// execution.step_rate_hz paces it on its own clock (0: not stepped).
struct GraphPacing {
    std::optional<std::int64_t> period_ns;
    std::int64_t origin_utc_ns{};
};

struct ServeStats {
    std::uint64_t steps{};
    double elapsed_seconds{};
    bool stopped_by_signal{};
};

class JsonSchemaLoader {
public:
    static RuntimeSchemaDefinition load_from_file(const std::string& file_path);
    static RuntimeSchemaDefinition load_from_json(const boost::json::value& json);
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

    // The running graph. initialize_graph() restores the deployment that was
    // active when the node stopped (state_dir/deployment.json), or else
    // compiles the schema the service was started with; a committed
    // deployment replaces it. step() and
    // step_count() are safe to call while the control plane activates a new
    // graph from another thread.
    void initialize_graph();
    void step();
    [[nodiscard]] std::uint64_t step_count() const;
    [[nodiscard]] double active_real_output(const std::string& element_id, std::size_t port = 0) const;
    // Reported to the control plane as the execution state of the active graph.
    void set_stepping(bool stepping);
    // Successful RECOVER_STREAM requests so far; a step loop stopped by a
    // stream fault resumes when this changes.
    [[nodiscard]] std::uint64_t stream_recoveries() const { return stream_recoveries_.load(); }
    [[nodiscard]] const std::string& runtime_instance_uuid() const;
    [[nodiscard]] static std::string create_schema_instance_uuid();
    [[nodiscard]] auto network_endpoint() const -> transport::RuntimeNetEndpointPtr;
    // In-process control request, as if received from peer.
    [[nodiscard]] auto handle_control_request_json(const transport::ControlPeer& peer, const std::string& request_json)
        -> std::string;
    void add_seed_peer(const std::string& endpoint);
    // Prints and clears connection-level transport errors (e.g. malformed frames).
    void report_transport_errors(std::ostream& out) const;
    // Re-reads the clock synchronization (at most once a second unless
    // forced). A change is printed; a lost synchronization is also recorded
    // as CLOCK_UNSYNCHRONIZED on every stream. Returns whether the clock is usable.
    bool check_clock(std::ostream& out, bool force = false);
    [[nodiscard]] auto clock_sync() const -> clock::SyncStatus;
    // Replaces where the synchronization is read from (default: the node's
    // clock configuration, then the kernel).
    void set_clock_source(std::function<clock::SyncStatus()> source);
    // Prints connection counts, queue drops and packet counters of the transport.
    void report_transport_stats(std::ostream& out) const;
    void print_summary(std::ostream& out) const;

    // Steps the active graph as its pacing says until stop_requested or the
    // deadline, and every 100 ms reports transport errors and checks the
    // clock. A step that fails (e.g. a stream fault) stops stepping until a
    // RECOVER_STREAM or a new deployment; a grid then continues at the next
    // grid slot (the missed steps are skipped, not burst). Progress lines go
    // to `out`, errors to `err`.
    auto serve(const std::atomic<bool>& stop_requested, std::optional<std::chrono::steady_clock::time_point> deadline,
               std::ostream& out, std::ostream& err) -> ServeStats;
    [[nodiscard]] auto pacing() const -> GraphPacing;
    [[nodiscard]] auto execution_timing() const -> boost::json::object;

    // Port 0 lets the system choose. Returns the port listened on (0 if the
    // server is not enabled).
    std::uint16_t start_websocket_server() const;
    void connect_websocket_client() const;

private:
    RuntimeSchemaDefinition definition_;
    std::filesystem::path state_dir_;
    std::optional<platform::FileLock> state_dir_lock_; // released after every later member is destroyed
    std::string runtime_instance_uuid_;
    std::shared_ptr<ControlPlane> control_plane_;
    transport::RuntimeNetEndpointPtr network_endpoint_;
    mutable std::atomic<std::uint16_t> control_port_{0};
    mutable std::atomic<std::uint16_t> data_port_{0};

    class GraphHost;
    friend class GraphHost;
    // Declared last so the graph (and its network elements) is destroyed first.
    mutable std::mutex graph_mutex_;
    std::unique_ptr<NodeRuntime> graph_;
    std::atomic<bool> stepping_{false};
    GraphPacing pacing_;                  // guarded by graph_mutex_
    std::uint64_t pacing_generation_{0};  // changes with every graph (guarded by graph_mutex_)
    std::atomic<std::int64_t> lag_ns_{0};
    std::atomic<std::int64_t> max_lag_ns_{0};
    std::atomic<std::uint64_t> grid_rebases_{0};
    // Steps the graph if it is still the one of `generation`.
    bool step_generation(std::uint64_t generation);
    // A committed graph waiting for its activation time (guarded by graph_mutex_).
    struct PendingGraph {
        std::unique_ptr<NodeRuntime> runtime;
        std::vector<transport::DataPeer> data_peers;
        GraphPacing pacing; // origin: the activation time
    };
    std::optional<PendingGraph> pending_;
    // Makes `runtime` the active graph (the previous one is destroyed first).
    void install_graph(std::unique_ptr<NodeRuntime> runtime, const std::vector<transport::DataPeer>& data_peers, GraphPacing pacing);
    // Installs the pending graph once its activation time has come; returns
    // whether it did.
    bool apply_due_activation();
    [[nodiscard]] auto pending_activation_ns() const -> std::optional<std::int64_t>;
    std::atomic<std::uint64_t> stream_recoveries_{0};
    std::shared_ptr<GraphHost> graph_host_;
    std::optional<bool> clock_usable_;
    std::function<clock::SyncStatus()> clock_source_;
    std::chrono::steady_clock::time_point clock_checked_{};
};

struct StepLoopStats {
    enum class StopReason { StopRequested, Deadline };

    std::uint64_t steps{};
    double elapsed_seconds{};
    StopReason stop_reason{StopReason::StopRequested};
};

// Calls step at step_rate_hz on the calling thread until stop_requested is set
// or the deadline passes. If stepping falls behind, the backlog is dropped
// rather than replayed in a burst. housekeeping, if set, runs on the same
// thread about every 100 ms.
StepLoopStats run_step_loop(const std::function<void()>& step,
                            double step_rate_hz,
                            const std::atomic<bool>& stop_requested,
                            std::optional<std::chrono::steady_clock::time_point> deadline = std::nullopt,
                            const std::function<void()>& housekeeping = {});

} // namespace s7

#endif // S7RUNTIME_INCLUDE_S7_RUNTIME_SERVICE_HPP
