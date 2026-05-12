#include "s7json_rpc.hpp"

namespace s7 {
namespace {

using boost::json::array;
using boost::json::object;
using boost::json::value;

struct RpcRequest
{
	value id = nullptr;
	std::string method;
	value params = object{};

	static auto fromJson(const value& request) -> RpcRequest
	{
		if (!request.is_object()) {
			throw std::runtime_error("Invalid Request");
		}
		const auto& obj = request.as_object();
		const auto* version = obj.if_contains("jsonrpc");
		const auto* method_val = obj.if_contains("method");
		if (!version || !version->is_string() || boost::json::value_to<std::string>(*version) != "2.0" ||
			!method_val || !method_val->is_string()) {
			throw std::runtime_error("Invalid Request");
		}

		RpcRequest out;
		if (const auto* id = obj.if_contains("id")) {
			out.id = *id;
		}
		out.method = boost::json::value_to<std::string>(*method_val);
		if (const auto* params = obj.if_contains("params")) {
			out.params = *params;
		}
		return out;
	}
};

struct RpcEnvelope
{
	static auto success(const value& id, const value& result) -> value
	{
		object response;
		response["jsonrpc"] = "2.0";
		response["id"] = id;
		response["result"] = result;
		return response;
	}

	static auto error(const value& id, int error_code, const std::string& message) -> value
	{
		object error;
		error["code"] = error_code;
		error["message"] = message;
		object response;
		response["jsonrpc"] = "2.0";
		response["id"] = id;
		response["error"] = std::move(error);
		return response;
	}
};

struct SchemaLoadSource
{
	std::string kind = "inline";
	std::string schema_json;
	std::string schema_name = "unnamed";
	std::string path;

	static auto fromJson(const object& params_obj) -> SchemaLoadSource
	{
		const auto* source_ptr = params_obj.if_contains("source");
		if (!source_ptr || !source_ptr->is_object()) {
			throw std::runtime_error("Missing 'source' parameter");
		}

		SchemaLoadSource source;
		const auto& source_obj = source_ptr->as_object();
		if (const auto* kind_ptr = source_obj.if_contains("kind"); kind_ptr && kind_ptr->is_string()) {
			source.kind = boost::json::value_to<std::string>(*kind_ptr);
		}

		if (source.kind == "inline") {
			if (const auto* schema_json_ptr = source_obj.if_contains("schema_json"); schema_json_ptr && schema_json_ptr->is_string()) {
				source.schema_json = boost::json::value_to<std::string>(*schema_json_ptr);
			}
			if (const auto* schema_name_ptr = params_obj.if_contains("schema_name"); schema_name_ptr && schema_name_ptr->is_string()) {
				source.schema_name = boost::json::value_to<std::string>(*schema_name_ptr);
			}
			return source;
		}

		if (source.kind == "filesystem") {
			if (const auto* path_ptr = source_obj.if_contains("path"); path_ptr && path_ptr->is_string()) {
				source.path = boost::json::value_to<std::string>(*path_ptr);
			}
			return source;
		}

		throw std::runtime_error("Unknown source kind: " + source.kind);
	}
};

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
		const auto rpc = RpcRequest::fromJson(request);
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
	const auto& params_obj = params.as_object();
	NodeRuntimeService::LoadResult load_result{};
	SchemaLoadSource source;
	try {
		source = SchemaLoadSource::fromJson(params_obj);
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
	const std::string handle = boost::json::value_to<std::string>(params.as_object().at("schema_handle"));
	service_->validateSchema(handle);
	object result;
	result["schema_handle"] = handle;
	result["valid"] = true;
	result["diagnostics"] = array{};
	return success_response(result, id);
}

auto JsonRpcDispatcher::handle_schema_run(const value& params, const value& id) -> value
{
	const auto& params_obj = params.as_object();
	const std::string handle = boost::json::value_to<std::string>(params_obj.at("schema_handle"));
	const auto& mode = params_obj.at("mode").as_object();
	const auto* ticks_ptr = mode.if_contains("ticks");
	const long ticks = ticks_ptr ? boost::json::value_to<long>(*ticks_ptr) : 1;
	const auto run_result = service_->runSchema(handle, ticks);
	object result;
	result["run_id"] = run_result.run_id;
	result["accepted"] = run_result.accepted;
	result["mode"] = mode;
	return success_response(result, id);
}

auto JsonRpcDispatcher::handle_schema_pause(const value& params, const value& id) -> value
{
	const std::string run_id = boost::json::value_to<std::string>(params.as_object().at("run_id"));
	service_->pauseRun(run_id);
	object result;
	result["run_id"] = run_id;
	result["paused"] = true;
	return success_response(result, id);
}

auto JsonRpcDispatcher::handle_schema_resume(const value& params, const value& id) -> value
{
	const std::string run_id = boost::json::value_to<std::string>(params.as_object().at("run_id"));
	service_->resumeRun(run_id);
	object result;
	result["run_id"] = run_id;
	result["resumed"] = true;
	return success_response(result, id);
}

auto JsonRpcDispatcher::handle_schema_stop(const value& params, const value& id) -> value
{
	const std::string run_id = boost::json::value_to<std::string>(params.as_object().at("run_id"));
	service_->stopRun(run_id);
	object result;
	result["run_id"] = run_id;
	result["stopped"] = true;
	return success_response(result, id);
}

auto JsonRpcDispatcher::handle_schema_reset(const value& params, const value& id) -> value
{
	const std::string handle = boost::json::value_to<std::string>(params.as_object().at("schema_handle"));
	service_->resetSchema(handle);
	object result;
	result["schema_handle"] = handle;
	result["reset"] = true;
	return success_response(result, id);
}

auto JsonRpcDispatcher::handle_schema_unload(const value& params, const value& id) -> value
{
	const std::string handle = boost::json::value_to<std::string>(params.as_object().at("schema_handle"));
	service_->unloadSchema(handle);
	object result;
	result["schema_handle"] = handle;
	result["unloaded"] = true;
	return success_response(result, id);
}

auto JsonRpcDispatcher::error_response(int error_code, const std::string& message, const value& id)
	-> value
{
	return RpcEnvelope::error(id, error_code, message);
}

auto JsonRpcDispatcher::success_response(const value& result, const value& id)
	-> value
{
	return RpcEnvelope::success(id, result);
}

} // namespace s7
