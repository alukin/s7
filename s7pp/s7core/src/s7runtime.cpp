#include "s7runtime.hpp"
#include "s7core_json_dto.hpp"
#include "s7netlist.hpp"
#include <chrono>
#include <thread>
namespace s7 {
NodeRuntimeService::NodeRuntimeService()
{
    startup_time_ = std::chrono::steady_clock::now();
}
NodeRuntimeService::LoadResult NodeRuntimeService::loadSchema(
    const std::string& schema_name,
    const std::string& schema_json)
{
    std::lock_guard<std::mutex> lock(state_mutex_);
    try {
        auto doc = SchemaBuilder::parseJsonString(schema_json);
        auto schema = SchemaBuilder::build(doc);
        std::string handle = generateSchemaHandle();
        loaded_schemas_[handle] = schema;
        event_bus_.publishEvent(
            "schema.loaded",
            dto::SchemaLoadedEvent{
                .schema_handle = handle,
                .schema_id = doc.schema_id,
                .nodes = (int)doc.nodes.size(),
                .connections = (int)doc.connections.size()}
                .toJson());
        return LoadResult{
            handle,
            doc.schema_id,
            (int)doc.nodes.size(),
            (int)doc.connections.size()
        };
    } catch (const std::exception& e) {
        throw std::runtime_error(std::string("Failed to load schema: ") + e.what());
    }
}
NodeRuntimeService::LoadResult NodeRuntimeService::loadSchemaFromFile(
    const std::string& path)
{
    std::lock_guard<std::mutex> lock(state_mutex_);
    try {
        auto doc = SchemaBuilder::parseJsonFile(path);
        auto schema = SchemaBuilder::build(doc);
        std::string handle = generateSchemaHandle();
        loaded_schemas_[handle] = schema;
        event_bus_.publishEvent(
            "schema.loaded",
            dto::SchemaLoadedEvent{
                .schema_handle = handle,
                .schema_id = doc.schema_id,
                .nodes = (int)doc.nodes.size(),
                .connections = (int)doc.connections.size(),
                .path = path}
                .toJson());
        return LoadResult{
            handle,
            doc.schema_id,
            (int)doc.nodes.size(),
            (int)doc.connections.size()
        };
    } catch (const std::exception& e) {
        throw std::runtime_error(std::string("File load error: ") + e.what());
    }
}
void NodeRuntimeService::validateSchema(const std::string& schema_handle)
{
    std::lock_guard<std::mutex> lock(state_mutex_);
    auto it = loaded_schemas_.find(schema_handle);
    if (it == loaded_schemas_.end()) {
        throw std::runtime_error("Schema handle not found: " + schema_handle);
    }
    event_bus_.publishEvent("schema.validated", dto::SchemaHandleEvent{.schema_handle = schema_handle}.toJson());
}
NodeRuntimeService::RunResult NodeRuntimeService::runSchema(
    const std::string& schema_handle,
    long ticks)
{
    // Validate and prepare state while holding the mutex, then launch the async
    // run without holding it so pause/resume/stop/status can interleave.
    std::shared_ptr<Schema> schema;
    std::string run_id;
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        if (hasActiveRun()) {
            throw std::runtime_error("Run already active for schema (single-active-run policy)");
        }
        auto it = loaded_schemas_.find(schema_handle);
        if (it == loaded_schemas_.end()) {
            throw std::runtime_error("Schema handle not found: " + schema_handle);
        }
        schema = it->second;
        run_id = generateRunId();
        RunInfo info{run_id, schema_handle, RunState::Starting, ticks, 0};
        active_runs_[run_id] = info;
        active_run_id_ = run_id;
        pause_requested_.store(false);
        stop_requested_.store(false);
        current_tick_count_.store(0);
    }

    {
        event_bus_.publishEvent(
            "run.started",
            dto::RunStartedEvent{.run_id = run_id, .schema_handle = schema_handle, .ticks = ticks}.toJson());
    }

    // Capture by value so the lambda owns everything it needs.
    run_future_ = std::async(std::launch::async,
        [this, schema, run_id, ticks] {
            bool        succeeded  = true;
            std::string error_msg;

            try {
                schema->prepareExecution();

                {
                    std::lock_guard<std::mutex> lock(state_mutex_);
                    if (active_runs_.count(run_id)) {
                        active_runs_[run_id].state = RunState::Running;
                    }
                }

                for (long i = 0; i < ticks; ++i) {
                    if (stop_requested_.load()) break;

                    // Spin-wait while paused.
                    while (pause_requested_.load() && !stop_requested_.load()) {
                        std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    }
                    if (stop_requested_.load()) break;

                    schema->tick();
                    current_tick_count_.store(i + 1);
                }
            } catch (const std::exception& ex) {
                succeeded = false;
                error_msg = ex.what();
            }

            // Update final state under the mutex.
            {
                std::lock_guard<std::mutex> lock(state_mutex_);
                if (active_runs_.count(run_id)) {
                    auto& info = active_runs_[run_id];
                    info.completed_ticks = current_tick_count_.load();
                    if (stop_requested_.load()) {
                        info.state = RunState::Stopped;
                    } else if (succeeded) {
                        info.state = RunState::Completed;
                    } else {
                        info.state = RunState::Failed;
                    }
                }
                active_run_id_ = "";
            }

            if (succeeded && !stop_requested_.load()) {
                event_bus_.publishEvent(
                    "run.completed",
                    dto::RunCompletedEvent{.run_id = run_id, .ticks = current_tick_count_.load()}.toJson());
            } else if (stop_requested_.load()) {
                event_bus_.publishEvent("run.stopped", dto::RunIdEvent{.run_id = run_id}.toJson());
            } else {
                event_bus_.publishEvent("run.failed", dto::RunFailedEvent{.run_id = run_id, .error = error_msg}.toJson());
            }
        });

    return RunResult{run_id, true};
}
void NodeRuntimeService::pauseRun(const std::string& run_id)
{
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        auto it = active_runs_.find(run_id);
        if (it == active_runs_.end()) {
            throw std::runtime_error("Run not found: " + run_id);
        }
        it->second.state = RunState::Paused;
    }
    pause_requested_.store(true);
    event_bus_.publishEvent("run.paused", dto::RunIdEvent{.run_id = run_id}.toJson());
}
void NodeRuntimeService::resumeRun(const std::string& run_id)
{
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        auto it = active_runs_.find(run_id);
        if (it == active_runs_.end()) {
            throw std::runtime_error("Run not found: " + run_id);
        }
        it->second.state = RunState::Running;
    }
    pause_requested_.store(false);
    event_bus_.publishEvent("run.resumed", dto::RunIdEvent{.run_id = run_id}.toJson());
}
void NodeRuntimeService::stopRun(const std::string& run_id)
{
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        auto it = active_runs_.find(run_id);
        if (it == active_runs_.end()) {
            throw std::runtime_error("Run not found: " + run_id);
        }
        it->second.state = RunState::Stopped;
    }
    stop_requested_.store(true);
    pause_requested_.store(false); // unblock any pause-spin
    event_bus_.publishEvent("run.stopped", dto::RunIdEvent{.run_id = run_id}.toJson());
}
void NodeRuntimeService::resetSchema(const std::string& schema_handle)
{
    std::lock_guard<std::mutex> lock(state_mutex_);
    auto it = loaded_schemas_.find(schema_handle);
    if (it == loaded_schemas_.end()) {
        throw std::runtime_error("Schema handle not found: " + schema_handle);
    }
    event_bus_.publishEvent("schema.reset", dto::SchemaHandleEvent{.schema_handle = schema_handle}.toJson());
}
void NodeRuntimeService::unloadSchema(const std::string& schema_handle)
{
    std::lock_guard<std::mutex> lock(state_mutex_);
    auto it = loaded_schemas_.find(schema_handle);
    if (it == loaded_schemas_.end()) {
        throw std::runtime_error("Schema handle not found: " + schema_handle);
    }
    loaded_schemas_.erase(it);
    event_bus_.publishEvent("schema.unloaded", dto::SchemaHandleEvent{.schema_handle = schema_handle}.toJson());
}
NodeRuntimeService::RuntimeStatus NodeRuntimeService::getStatus() const
{
    std::lock_guard<std::mutex> lock(state_mutex_);
    RuntimeStatus status;
    // Real uptime in milliseconds since construction.
    status.uptime_ms = static_cast<long>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - startup_time_).count());
    status.health_state = "ok";
    for (const auto& [handle, _] : loaded_schemas_) {
        status.active_schema_handles.push_back(handle);
    }
    for (const auto& [run_id, _] : active_runs_) {
        status.active_runs.push_back(run_id);
    }
    return status;
}
std::vector<std::string> NodeRuntimeService::getRegisteredTypes() const
{
    return ElementRegistry::instance().registeredTypes();
}
RunInfo NodeRuntimeService::getRunInfo(const std::string& run_id) const
{
    std::lock_guard<std::mutex> lock(state_mutex_);
    auto it = active_runs_.find(run_id);
    if (it == active_runs_.end()) {
        throw std::runtime_error("Run not found: " + run_id);
    }
    RunInfo info = it->second;
    // Fill in live tick count for in-progress runs.
    if (info.state == RunState::Running || info.state == RunState::Paused) {
        info.completed_ticks = current_tick_count_.load();
    }
    return info;
}
uint64_t NodeRuntimeService::subscribeToEvents(const std::vector<std::string>& topics,
                                               EventCallback callback)
{
    return event_bus_.subscribe(topics, callback);
}
void NodeRuntimeService::unsubscribeFromEvents(uint64_t subscription_id)
{
    event_bus_.unsubscribe(subscription_id);
}
std::string NodeRuntimeService::generateSchemaHandle()
{
    static int counter = 0;
    return "schema-" + std::to_string(++counter);
}
std::string NodeRuntimeService::generateRunId()
{
    static int counter = 0;
    return "run-" + std::to_string(++counter);
}
bool NodeRuntimeService::hasActiveRun() const
{
    return !active_run_id_.empty();
}
} // namespace s7
