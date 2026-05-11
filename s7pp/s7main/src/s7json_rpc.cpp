#include "s7json_rpc.hpp"

namespace s7 {
namespace {

auto asId(const nlohmann::json& request) -> nlohmann::json
{
	return request.contains("id") ? request["id"] : nlohmann::json(nullptr);
}

auto requestParams(const nlohmann::json& request) -> nlohmann::json
{
	return request.value("params", nlohmann::json::object());
}

} // namespace

JsonRpcDispatcher::JsonRpcDispatcher(std::shared_ptr<NodeRuntimeService> service)
	: service_(std::move(service))
{
}

auto JsonRpcDispatcher::dispatch(const std::string& json_request_str) -> nlohmann::json
{
	try {
		return dispatchParsed(nlohmann::json::parse(json_request_str));
	} catch (const nlohmann::json::parse_error&) {
		return error_response(-32700, "Parse error", nullptr);
	}
}

auto JsonRpcDispatcher::dispatchParsed(const nlohmann::json& request) -> nlohmann::json
{
	const auto id = asId(request);

	try {
		if (!request.contains("jsonrpc") || request["jsonrpc"] != "2.0" || !request.contains("method")) {
			return error_response(-32600, "Invalid Request", id);
		}

		const std::string method = request["method"].get<std::string>();
		const auto params = requestParams(request);

		if (method == "runtime.hello") {
			return handle_runtime_hello(params, id);
		}
		if (method == "runtime.types.list") {
			return handle_runtime_types_list(params, id);
		}
		if (method == "runtime.status") {
			return handle_runtime_status(params, id);
		}
		if (method == "schema.load") {
			return handle_schema_load(params, id);
		}
		if (method == "schema.validate") {
			return handle_schema_validate(params, id);
		}
		if (method == "schema.run") {
			return handle_schema_run(params, id);
		}
		if (method == "schema.pause") {
			return handle_schema_pause(params, id);
		}
		if (method == "schema.resume") {
			return handle_schema_resume(params, id);
		}
		if (method == "schema.stop") {
			return handle_schema_stop(params, id);
		}
		if (method == "schema.reset") {
			return handle_schema_reset(params, id);
		}
		if (method == "schema.unload") {
			return handle_schema_unload(params, id);
		}

		return error_response(-32601, "Method not found", id);
	} catch (const std::exception& ex) {
		return error_response(-32000, ex.what(), id);
	}
}

auto JsonRpcDispatcher::handle_runtime_hello(const nlohmann::json&, const nlohmann::json& id) -> nlohmann::json
{
	const nlohmann::json result{
		{"protocol_version", 0},
		{"node_id", "node-local-001"},
		{"node_name", "s7main-local"},
		{"runtime_version", "0.1.0"},
		{"capabilities", {
			{"methods", {"runtime.hello", "runtime.types.list", "runtime.status", "runtime.events.subscribe", "runtime.events.unsubscribe", "schema.load", "schema.validate", "schema.run", "schema.pause", "schema.resume", "schema.stop", "schema.reset", "schema.unload"}},
			{"supports_event_stream", true},
			{"supports_parallel_runs", false},
			{"schema_handles_persist_on_disconnect", true},
			{"transport", "websocket"}
		}}
	};
	return success_response(result, id);
}

auto JsonRpcDispatcher::handle_runtime_types_list(const nlohmann::json&, const nlohmann::json& id) -> nlohmann::json
{
	const auto types = service_->getRegisteredTypes();
	nlohmann::json types_array = nlohmann::json::array();
	for (const auto& t : types) {
		types_array.push_back({{"name", t}});
	}
	return success_response({{"types", types_array}}, id);
}

auto JsonRpcDispatcher::handle_runtime_status(const nlohmann::json&, const nlohmann::json& id) -> nlohmann::json
{
	const auto status = service_->getStatus();
	return success_response(
		{{"uptime_ms", status.uptime_ms},
		 {"active_schema_handles", status.active_schema_handles},
		 {"active_runs", status.active_runs},
		 {"health", {{"state", status.health_state}, {"degraded", false}, {"warnings", nlohmann::json::array()}}}},
		id);
}

auto JsonRpcDispatcher::handle_schema_load(const nlohmann::json& params, const nlohmann::json& id) -> nlohmann::json
{
	if (!params.contains("source")) {
		return error_response(-32002, "Missing 'source' parameter", id);
	}

	const auto source = params["source"];
	const std::string kind = source.value("kind", "inline");
	NodeRuntimeService::LoadResult load_result{};

	if (kind == "inline") {
		const std::string schema_json = source.value("schema_json", std::string{});
		const std::string schema_name = params.value("schema_name", "unnamed");
		load_result = service_->loadSchema(schema_name, schema_json);
	} else if (kind == "filesystem") {
		const std::string path = source.value("path", std::string{});
		load_result = service_->loadSchemaFromFile(path);
	} else {
		return error_response(-32002, "Unknown source kind: " + kind, id);
	}

	return success_response(
		{{"schema_handle", load_result.schema_handle},
		 {"loaded", true},
		 {"summary", {{"id", load_result.schema_id}, {"elements", load_result.num_elements}, {"connections", load_result.num_connections}}}},
		id);
}

auto JsonRpcDispatcher::handle_schema_validate(const nlohmann::json& params, const nlohmann::json& id) -> nlohmann::json
{
	const std::string handle = params.at("schema_handle").get<std::string>();
	service_->validateSchema(handle);
	return success_response({{"schema_handle", handle}, {"valid", true}, {"diagnostics", nlohmann::json::array()}}, id);
}

auto JsonRpcDispatcher::handle_schema_run(const nlohmann::json& params, const nlohmann::json& id) -> nlohmann::json
{
	const std::string handle = params.at("schema_handle").get<std::string>();
	const auto mode = params.at("mode");
	const long ticks = mode.contains("ticks") ? mode["ticks"].get<long>() : 1;
	const auto run_result = service_->runSchema(handle, ticks);
	return success_response({{"run_id", run_result.run_id}, {"accepted", run_result.accepted}, {"mode", mode}}, id);
}

auto JsonRpcDispatcher::handle_schema_pause(const nlohmann::json& params, const nlohmann::json& id) -> nlohmann::json
{
	const std::string run_id = params.at("run_id").get<std::string>();
	service_->pauseRun(run_id);
	return success_response({{"run_id", run_id}, {"paused", true}}, id);
}

auto JsonRpcDispatcher::handle_schema_resume(const nlohmann::json& params, const nlohmann::json& id) -> nlohmann::json
{
	const std::string run_id = params.at("run_id").get<std::string>();
	service_->resumeRun(run_id);
	return success_response({{"run_id", run_id}, {"resumed", true}}, id);
}

auto JsonRpcDispatcher::handle_schema_stop(const nlohmann::json& params, const nlohmann::json& id) -> nlohmann::json
{
	const std::string run_id = params.at("run_id").get<std::string>();
	service_->stopRun(run_id);
	return success_response({{"run_id", run_id}, {"stopped", true}}, id);
}

auto JsonRpcDispatcher::handle_schema_reset(const nlohmann::json& params, const nlohmann::json& id) -> nlohmann::json
{
	const std::string handle = params.at("schema_handle").get<std::string>();
	service_->resetSchema(handle);
	return success_response({{"schema_handle", handle}, {"reset", true}}, id);
}

auto JsonRpcDispatcher::handle_schema_unload(const nlohmann::json& params, const nlohmann::json& id) -> nlohmann::json
{
	const std::string handle = params.at("schema_handle").get<std::string>();
	service_->unloadSchema(handle);
	return success_response({{"schema_handle", handle}, {"unloaded", true}}, id);
}

auto JsonRpcDispatcher::error_response(int error_code, const std::string& message, const nlohmann::json& id)
	-> nlohmann::json
{
	return nlohmann::json{{"jsonrpc", "2.0"}, {"id", id}, {"error", {{"code", error_code}, {"message", message}}}};
}

auto JsonRpcDispatcher::success_response(const nlohmann::json& result, const nlohmann::json& id)
	-> nlohmann::json
{
	return nlohmann::json{{"jsonrpc", "2.0"}, {"id", id}, {"result", result}};
}

} // namespace s7
