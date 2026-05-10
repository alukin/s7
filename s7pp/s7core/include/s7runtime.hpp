//
// s7runtime.hpp — NodeRuntimeService for control plane operations
//
// Manages:
//   - Schema lifecycle: load, validate, run, pause, resume, stop, reset, unload
//   - Single active run per node (enforced)
//   - Schema handles: survive client disconnect
//   - Event publication to EventBus
//   - Schema file operations (list, get, save)
//
// Thread-safe: uses internal mutex for state management.
//

#pragma once

#include "s7.hpp"
#include "s7events.hpp"

#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include <nlohmann/json.hpp>

namespace s7 {

// Forward declarations
class EventBus;

enum class RunState {
    Starting,
    Running,
    Paused,
    Completed,
    Stopped,
    Failed,
};

struct RunInfo {
    std::string run_id;
    std::string schema_handle;
    RunState state;
    long total_ticks;
    long completed_ticks;
};

/// NodeRuntimeService — orchestrates schema execution and event distribution.
class NodeRuntimeService : public std::enable_shared_from_this<NodeRuntimeService> {
public:
    /// Factory for single instance
    static std::shared_ptr<NodeRuntimeService> create()
    {
        return std::make_shared<NodeRuntimeService>();
    }

    NodeRuntimeService();
    ~NodeRuntimeService() = default;

    NodeRuntimeService(const NodeRuntimeService&) = delete;
    NodeRuntimeService& operator=(const NodeRuntimeService&) = delete;

    // ===== Schema Lifecycle =====

    struct LoadResult {
        std::string schema_handle;
        std::string schema_id;
        int num_elements;
        int num_connections;
    };

    /// Load a schema from inline JSON string
    LoadResult loadSchema(const std::string& schema_name,
                         const std::string& schema_json);

    /// Load a schema from filesystem (relative to configured root, or absolute allowed)
    LoadResult loadSchemaFromFile(const std::string& path);

    /// Validate a loaded schema without executing
    void validateSchema(const std::string& schema_handle);

    /// Run a schema for N ticks
    struct RunResult {
        std::string run_id;
        bool accepted;
    };

    RunResult runSchema(const std::string& schema_handle,
                       long ticks);

    /// Pause active run (graceful, at tick boundary)
    void pauseRun(const std::string& run_id);

    /// Resume a paused run
    void resumeRun(const std::string& run_id);

    /// Stop active run (graceful, at tick boundary)
    void stopRun(const std::string& run_id);

    /// Reset schema state and parameter calculations
    void resetSchema(const std::string& schema_handle);

    /// Unload a schema (keeps run state if active)
    void unloadSchema(const std::string& schema_handle);

    // ===== Query =====

    struct RuntimeStatus {
        long uptime_ms;
        std::vector<std::string> active_schema_handles;
        std::vector<std::string> active_runs;
        std::string health_state;
    };

    RuntimeStatus getStatus() const;

    /// Get list of registered element types (from singleton registry)
    std::vector<std::string> getRegisteredTypes() const;

    /// Get run info by run_id
    RunInfo getRunInfo(const std::string& run_id) const;

    // ===== Events =====

    using EventCallback = std::function<void(const Event&)>;

    /// Subscribe to events (topics like "run", "schema", "runtime")
    uint64_t subscribeToEvents(const std::vector<std::string>& topics,
                               EventCallback callback);

    /// Unsubscribe from events
    void unsubscribeFromEvents(uint64_t subscription_id);

    /// Access event bus directly (for RSocket integration)
    EventBus& eventBus() { return event_bus_; }

private:
    // State
    std::unordered_map<std::string, std::shared_ptr<Schema>> loaded_schemas_;
    std::unordered_map<std::string, RunInfo> active_runs_;
    std::string active_run_id_;  // Single active run constraint
    mutable std::mutex state_mutex_;
    EventBus event_bus_;

    // Internal helpers
    std::string generateSchemaHandle();
    std::string generateRunId();
    bool hasActiveRun() const;
};

} // namespace s7


