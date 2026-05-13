#include "s7json_rpc.hpp"
#include "s7main_json_dto.hpp"

namespace s7 {
namespace {

using boost::json::array;
using boost::json::object;
using boost::json::value;

} // namespace

JsonRpcDispatcher::JsonRpcDispatcher(std::shared_ptr<NodeRuntimeService> service)
	: service_(std::move(service))
{
}

auto JsonRpcDispatcher::dispatch(const std::string& json_request_str) -> value
{
	try {
		return dispatchParsed(boost::json::parse(json_request_str));
	} catch (const std::exception&) {
		return error_response(-32700, "Parse error", nullptr);
	}
}

auto JsonRpcDispatcher::dispatchParsed(const value& request) -> value
{
	try {
		const auto rpc = dto::RpcRequest::fromJson(request);
		const auto& method = rpc.method;
		const auto& params = rpc.params;

		if (method == "runtime.hello") {
			return handle_runtime_hello(params, rpc.id);
		}
		if (method == "runtime.types.list") {
			return handle_runtime_types_list(params, rpc.id);
		}
		if (method == "runtime.status") {
			return handle_runtime_status(params, rpc.id);
		}
		if (method == "schema.load") {
			return handle_schema_load(params, rpc.id);
		}
		if (method == "schema.validate") {
			return handle_schema_validate(params, rpc.id);
		}
		if (method == "schema.run") {
			return handle_schema_run(params, rpc.id);
		}
		if (method == "schema.pause") {
			return handle_schema_pause(params, rpc.id);
		}
		if (method == "schema.resume") {
			return handle_schema_resume(params, rpc.id);
		}
		if (method == "schema.stop") {
			return handle_schema_stop(params, rpc.id);
		}
		if (method == "schema.reset") {
			return handle_schema_reset(params, rpc.id);
		}
		if (method == "schema.unload") {
			return handle_schema_unload(params, rpc.id);
		}

		return error_response(-32601, "Method not found", rpc.id);
	} catch (const std::exception& ex) {
		if (std::string(ex.what()) == "Invalid Request") {
			return error_response(-32600, "Invalid Request", nullptr);
		}
		return error_response(-32000, ex.what(), nullptr);
	}
}

auto JsonRpcDispatcher::handle_runtime_hello(const value&, const value& id) -> value
{
	array methods{
		"runtime.hello", "runtime.types.list", "runtime.status", "runtime.events.subscribe",
		"runtime.events.unsubscribe", "schema.load", "schema.validate", "schema.run",
		"schema.pause", "schema.resume", "schema.stop", "schema.reset", "schema.unload"};
	object capabilities;
	capabilities["methods"] = std::move(methods);
	capabilities["supports_event_stream"] = true;
	capabilities["supports_parallel_runs"] = false;
	capabilities["schema_handles_persist_on_disconnect"] = true;
	capabilities["transport"] = "websocket";

	object result;
	result["protocol_version"] = 0;
	result["node_id"] = "node-local-001";
	result["node_name"] = "s7main-local";
	result["runtime_version"] = "0.1.0";
	result["capabilities"] = std::move(capabilities);
	return success_response(result, id);
}

auto JsonRpcDispatcher::handle_runtime_types_list(const value&, const value& id) -> value
{
	const auto types = service_->getRegisteredTypes();
	array types_array;
	for (const auto& t : types) {
		object entry;
		entry["name"] = t;
		types_array.push_back(std::move(entry));
	}
	object result;
	result["types"] = std::move(types_array);
	return success_response(result, id);
}

auto JsonRpcDispatcher::handle_runtime_status(const value&, const value& id) -> value
{
	const auto status = service_->getStatus();
	object health;
	health["state"] = status.health_state;
	health["degraded"] = false;
	health["warnings"] = array{};

	object result;
	result["uptime_ms"] = status.uptime_ms;
	result["active_schema_handles"] = boost::json::value_from(status.active_schema_handles);
	result["active_runs"] = boost::json::value_from(status.active_runs);
	result["health"] = std::move(health);
	return success_response(result, id);
}

auto JsonRpcDispatcher::handle_schema_load(const value& params, const value& id) -> value
{
	if (!params.is_object()) {
		return error_response(-32002, "Invalid 'params' object", id);
	}
	NodeRuntimeService::LoadResult load_result{};
	dto::SchemaLoadSource source;
	try {
		source = dto::SchemaLoadSource::fromJson(params);
	} catch (const std::exception& ex) {
		return error_response(-32002, ex.what(), id);
	}

	if (source.kind == "inline") {
		load_result = service_->loadSchema(source.schema_name, source.schema_json);
	} else if (source.kind == "filesystem") {
		load_result = service_->loadSchemaFromFile(source.path);
	} else {
		return error_response(-32002, "Unknown source kind: " + source.kind, id);
	}

	object summary;
	summary["id"] = load_result.schema_id;
	summary["elements"] = load_result.num_elements;
	summary["connections"] = load_result.num_connections;
	object result;
	result["schema_handle"] = load_result.schema_handle;
	result["loaded"] = true;
	result["summary"] = std::move(summary);
	return success_response(result, id);
}

auto JsonRpcDispatcher::handle_schema_validate(const value& params, const value& id) -> value
{
	const auto handle = dto::SchemaHandleParam::fromJson(params).schema_handle;
	service_->validateSchema(handle);
	object result;
	result["schema_handle"] = handle;
	result["valid"] = true;
	result["diagnostics"] = array{};
	return success_response(result, id);
}

auto JsonRpcDispatcher::handle_schema_run(const value& params, const value& id) -> value
{
	const auto run_params = dto::SchemaRunParams::fromJson(params);
	const auto run_result = service_->runSchema(run_params.schema_handle, run_params.ticks);
	object result;
	result["run_id"] = run_result.run_id;
	result["accepted"] = run_result.accepted;
	result["mode"] = run_params.mode;
	return success_response(result, id);
}

auto JsonRpcDispatcher::handle_schema_pause(const value& params, const value& id) -> value
{
	const auto run_id = dto::RunIdParam::fromJson(params).run_id;
	service_->pauseRun(run_id);
	object result;
	result["run_id"] = run_id;
	result["paused"] = true;
	return success_response(result, id);
}

auto JsonRpcDispatcher::handle_schema_resume(const value& params, const value& id) -> value
{
	const auto run_id = dto::RunIdParam::fromJson(params).run_id;
	service_->resumeRun(run_id);
	object result;
	result["run_id"] = run_id;
	result["resumed"] = true;
	return success_response(result, id);
}

auto JsonRpcDispatcher::handle_schema_stop(const value& params, const value& id) -> value
{
	const auto run_id = dto::RunIdParam::fromJson(params).run_id;
	service_->stopRun(run_id);
	object result;
	result["run_id"] = run_id;
	result["stopped"] = true;
	return success_response(result, id);
}

auto JsonRpcDispatcher::handle_schema_reset(const value& params, const value& id) -> value
{
	const auto handle = dto::SchemaHandleParam::fromJson(params).schema_handle;
	service_->resetSchema(handle);
	object result;
	result["schema_handle"] = handle;
	result["reset"] = true;
	return success_response(result, id);
}

auto JsonRpcDispatcher::handle_schema_unload(const value& params, const value& id) -> value
{
	const auto handle = dto::SchemaHandleParam::fromJson(params).schema_handle;
	service_->unloadSchema(handle);
	object result;
	result["schema_handle"] = handle;
	result["unloaded"] = true;
	return success_response(result, id);
}

auto JsonRpcDispatcher::error_response(int error_code, const std::string& message, const value& id)
	-> value
{
	return dto::RpcEnvelope::error(id, error_code, message);
}

auto JsonRpcDispatcher::success_response(const value& result, const value& id)
	-> value
{
	return dto::RpcEnvelope::success(id, result);
}

} // namespace s7
