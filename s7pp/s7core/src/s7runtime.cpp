#include "s7runtime.hpp"
#include "s7netlist.hpp"
#include <chrono>
namespace s7 {
NodeRuntimeService::NodeRuntimeService()
{
}
NodeRuntimeService::LoadResult NodeRuntimeService::loadSchema(
    const std::string& schema_name,
    const std::string& schema_json)
{
    std::lock_guard<std::mutex> lock(state_mutex_);
    try {
        auto doc = SchemaBuilder::parseJson(schema_json);
        auto schema = SchemaBuilder::build(doc);
        std::string handle = generateSchemaHandle();
        loaded_schemas_[handle] = schema;
        nlohmann::json event_payload;
        event_payload["schema_handle"] = handle;
        event_payload["schema_id"] = doc.schema_id;
        event_payload["elements"] = (int)doc.elements.size();
        event_payload["connections"] = (int)doc.connections.size();
        event_bus_.publishEvent("schema.loaded", event_payload);
        return LoadResult{
            handle,
            doc.schema_id,
            (int)doc.elements.size(),
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
        nlohmann::json event_payload;
        event_payload["schema_handle"] = handle;
        event_payload["schema_id"] = doc.schema_id;
        event_payload["path"] = path;
        event_bus_.publishEvent("schema.loaded", event_payload);
        return LoadResult{
            handle,
            doc.schema_id,
            (int)doc.elements.size(),
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
    nlohmann::json event_payload;
    event_payload["schema_handle"] = schema_handle;
    event_bus_.publishEvent("schema.validated", event_payload);
}
NodeRuntimeService::RunResult NodeRuntimeService::runSchema(
    const std::string& schema_handle,
    long ticks)
{
    std::lock_guard<std::mutex> lock(state_mutex_);
    if (hasActiveRun()) {
        throw std::runtime_error("Run already active for schema (single-active-run policy)");
    }
    auto it = loaded_schemas_.find(schema_handle);
    if (it == loaded_schemas_.end()) {
        throw std::runtime_error("Schema handle not found: " + schema_handle);
    }
    std::string run_id = generateRunId();
    RunInfo info{run_id, schema_handle, RunState::Starting, ticks, 0};
    active_runs_[run_id] = info;
    active_run_id_ = run_id;
    nlohmann::json event_payload;
    event_payload["run_id"] = run_id;
    event_payload["schema_handle"] = schema_handle;
    event_payload["ticks"] = ticks;
    event_bus_.publishEvent("run.started", event_payload);
    auto schema = it->second;
    try {
        schema->run(ticks);
        info.state = RunState::Completed;
        info.completed_ticks = ticks;
        active_runs_[run_id] = info;
        nlohmann::json event_payload2;
        event_payload2["run_id"] = run_id;
        event_payload2["ticks"] = ticks;
        event_bus_.publishEvent("run.completed", event_payload2);
    } catch (const std::exception& e) {
        info.state = RunState::Failed;
        active_runs_[run_id] = info;
        nlohmann::json event_payload3;
        event_payload3["run_id"] = run_id;
        event_payload3["error"] = e.what();
        event_bus_.publishEvent("run.failed", event_payload3);
        throw;
    }
    active_run_id_ = "";
    return RunResult{run_id, true};
}
void NodeRuntimeService::pauseRun(const std::string& run_id)
{
    std::lock_guard<std::mutex> lock(state_mutex_);
    auto it = active_runs_.find(run_id);
    if (it == active_runs_.end()) {
        throw std::runtime_error("Run not found: " + run_id);
    }
    it->second.state = RunState::Paused;
    nlohmann::json event_payload;
    event_payload["run_id"] = run_id;
    event_bus_.publishEvent("run.paused", event_payload);
}
void NodeRuntimeService::resumeRun(const std::string& run_id)
{
    std::lock_guard<std::mutex> lock(state_mutex_);
    auto it = active_runs_.find(run_id);
    if (it == active_runs_.end()) {
        throw std::runtime_error("Run not found: " + run_id);
    }
    it->second.state = RunState::Running;
    nlohmann::json event_payload;
    event_payload["run_id"] = run_id;
    event_bus_.publishEvent("run.resumed", event_payload);
}
void NodeRuntimeService::stopRun(const std::string& run_id)
{
    std::lock_guard<std::mutex> lock(state_mutex_);
    auto it = active_runs_.find(run_id);
    if (it == active_runs_.end()) {
        throw std::runtime_error("Run not found: " + run_id);
    }
    it->second.state = RunState::Stopped;
    nlohmann::json event_payload;
    event_payload["run_id"] = run_id;
    event_bus_.publishEvent("run.stopped", event_payload);
}
void NodeRuntimeService::resetSchema(const std::string& schema_handle)
{
    std::lock_guard<std::mutex> lock(state_mutex_);
    auto it = loaded_schemas_.find(schema_handle);
    if (it == loaded_schemas_.end()) {
        throw std::runtime_error("Schema handle not found: " + schema_handle);
    }
    nlohmann::json event_payload;
    event_payload["schema_handle"] = schema_handle;
    event_bus_.publishEvent("schema.reset", event_payload);
}
void NodeRuntimeService::unloadSchema(const std::string& schema_handle)
{
    std::lock_guard<std::mutex> lock(state_mutex_);
    auto it = loaded_schemas_.find(schema_handle);
    if (it == loaded_schemas_.end()) {
        throw std::runtime_error("Schema handle not found: " + schema_handle);
    }
    loaded_schemas_.erase(it);
    nlohmann::json event_payload;
    event_payload["schema_handle"] = schema_handle;
    event_bus_.publishEvent("schema.unloaded", event_payload);
}
NodeRuntimeService::RuntimeStatus NodeRuntimeService::getStatus() const
{
    std::lock_guard<std::mutex> lock(state_mutex_);
    RuntimeStatus status;
    status.uptime_ms = 0;
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
    return it->second;
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
