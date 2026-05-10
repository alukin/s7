#include "s7json_rpc.hpp"
#include <iostream>
namespace s7 {
JsonRpcDispatcher::JsonRpcDispatcher(std::shared_ptr<NodeRuntimeService> service)
    : service_(service)
{
}
nlohmann::json JsonRpcDispatcher::dispatch(const std::string& json_request_str)
{
    try {
        auto request = nlohmann::json::parse(json_request_str);
        return dispatchParsed(request);
    } catch (const nlohmann::json::parse_error& e) {
        return error_response(-32700, "Parse error", "null");
    }
}
nlohmann::json JsonRpcDispatcher::dispatchParsed(const nlohmann::json& request)
{
    try {
        if (!request.contains("jsonrpc") || request["jsonrpc"] != "2.0") {
            return error_response(-32600, "Invalid Request", "null");
        }
        if (!request.contains("method")) {
            return error_response(-32600, "Invalid Request", "null");
        }
        std::string method = request["method"].get<std::string>();
        std::string id = request.contains("id") ? request["id"].dump() : "null";
        nlohmann::json params = request.value("params", nlohmann::json::object());
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
        return error_response(-32001, "Method not found", id);
    } catch (const std::exception& e) {
        return error_response(-32000, std::string("Server error: ") + e.what(), "null");
    }
}
nlohmann::json JsonRpcDispatcher::handle_runtime_hello(const nlohmann::json& params,
                                                       const std::string& id)
{
    nlohmann::json result{
        {"protocol_version", 0},
        {"node_id", "node-local-001"},
        {"node_name", "s7main-local"},
        {"runtime_version", "0.1.0"},
        {"capabilities", {
            {"methods", {
                "runtime.hello",
                "runtime.types.list",
                "runtime.status",
                "schema.load",
                "schema.validate",
                "schema.run",
                "schema.pause",
                "schema.resume",
                "schema.stop",
                "schema.reset",
                "schema.unload"
            }},
            {"supports_event_stream", true},
            {"supports_parallel_runs", false},
            {"schema_handles_persist_on_disconnect", true},
            {"authorization_model", "node-level"}
        }}
    };
    return success_response(result, id);
}
nlohmann::json JsonRpcDispatcher::handle_runtime_types_list(const nlohmann::json& params,
                                                            const std::string& id)
{
    try {
        auto types = service_->getRegisteredTypes();
        nlohmann::json types_array = nlohmann::json::array();
        for (const auto& t : types) {
            types_array.push_back(nlohmann::json{{"name", t}});
        }
        nlohmann::json result{{"types", types_array}};
        return success_response(result, id);
    } catch (const std::exception& e) {
        return error_response(-32000, e.what(), id);
    }
}
nlohmann::json JsonRpcDispatcher::handle_runtime_status(const nlohmann::json& params,
                                                        const std::string& id)
{
    try {
        auto status = service_->getStatus();
        nlohmann::json result{
            {"uptime_ms", status.uptime_ms},
            {"active_schema_handles", status.active_schema_handles},
            {"active_runs", status.active_runs},
            {"health", {
                {"state", status.health_state},
                {"degraded", false},
                {"warnings", nlohmann::json::array()}
            }}
        };
        return success_response(result, id);
    } catch (const std::exception& e) {
        return error_response(-32000, e.what(), id);
    }
}
nlohmann::json JsonRpcDispatcher::handle_schema_load(const nlohmann::json& params,
                                                     const std::string& id)
{
    try {
        if (!params.contains("source")) {
            return error_response(-32002, "Missing 'source' parameter", id);
        }
        auto source = params["source"];
        std::string kind = source.value("kind", "inline");
        NodeRuntimeService::LoadResult load_result;
        if (kind == "inline") {
            std::string schema_json = source["schema_json"];
            std::string schema_name = params.value("schema_name", "unnamed");
            load_result = service_->loadSchema(schema_name, schema_json);
        } else if (kind == "filesystem") {
            std::string path = source["path"];
            load_result = service_->loadSchemaFromFile(path);
        } else {
            return error_response(-32002, "Unknown source kind: " + kind, id);
        }
        nlohmann::json result{
            {"schema_handle", load_result.schema_handle},
            {"loaded", true},
            {"summary", {
                {"id", load_result.schema_id},
                {"elements", load_result.num_elements},
                {"connections", load_result.num_connections}
            }}
        };
        return success_response(result, id);
    } catch (const std::exception& e) {
        return error_response(-32010, e.what(), id);
    }
}
nlohmann::json JsonRpcDispatcher::handle_schema_validate(const nlohmann::json& params,
                                                         const std::string& id)
{
    try {
        std::string handle = params["schema_handle"];
        service_->validateSchema(handle);
        nlohmann::json result{
            {"schema_handle", handle},
            {"valid", true},
            {"diagnostics", nlohmann::json::array()}
        };
        return success_response(result, id);
    } catch (const std::exception& e) {
        return error_response(-32010, e.what(), id);
    }
}
nlohmann::json JsonRpcDispatcher::handle_schema_run(const nlohmann::json& params,
                                                    const std::string& id)
{
    try {
        std::string handle = params["schema_handle"];
        auto mode = params["mode"];
        long ticks = 1;
        if (mode.contains("ticks")) {
            ticks = mode["ticks"].get<long>();
        }
        auto run_result = service_->runSchema(handle, ticks);
        nlohmann::json result{
            {"run_id", run_result.run_id},
            {"accepted", run_result.accepted},
            {"mode", mode}
        };
        return success_response(result, id);
    } catch (const std::exception& e) {
        return error_response(-32012, e.what(), id);
    }
}
nlohmann::json JsonRpcDispatcher::handle_schema_pause(const nlohmann::json& params,
                                                      const std::string& id)
{
    try {
        std::string run_id = params["run_id"];
        service_->pauseRun(run_id);
        nlohmann::json result{
            {"run_id", run_id},
            {"paused", true}
        };
        return success_response(result, id);
    } catch (const std::exception& e) {
        return error_response(-32000, e.what(), id);
    }
}
nlohmann::json JsonRpcDispatcher::handle_schema_resume(const nlohmann::json& params,
                                                       const std::string& id)
{
    try {
        std::string run_id = params["run_id"];
        service_->resumeRun(run_id);
        nlohmann::json result{
            {"run_id", run_id},
            {"resumed", true}
        };
        return success_response(result, id);
    } catch (const std::exception& e) {
        return error_response(-32000, e.what(), id);
    }
}
nlohmann::json JsonRpcDispatcher::handle_schema_stop(const nlohmann::json& params,
                                                     const std::string& id)
{
    try {
        std::string run_id = params["run_id"];
        service_->stopRun(run_id);
        nlohmann::json result{
            {"stopped", true},
            {"run_id", run_id}
        };
        return success_response(result, id);
    } catch (const std::exception& e) {
        return error_response(-32000, e.what(), id);
    }
}
nlohmann::json JsonRpcDispatcher::handle_schema_reset(const nlohmann::json& params,
                                                      const std::string& id)
{
    try {
        std::string handle = params["schema_handle"];
        service_->resetSchema(handle);
        nlohmann::json result{
            {"schema_handle", handle},
            {"reset", true}
        };
        return success_response(result, id);
    } catch (const std::exception& e) {
        return error_response(-32000, e.what(), id);
    }
}
nlohmann::json JsonRpcDispatcher::handle_schema_unload(const nlohmann::json& params,
                                                       const std::string& id)
{
    try {
        std::string handle = params["schema_handle"];
        service_->unloadSchema(handle);
        nlohmann::json result{
            {"schema_handle", handle},
            {"unloaded", true}
        };
        return success_response(result, id);
    } catch (const std::exception& e) {
        return error_response(-32000, e.what(), id);
    }
}
nlohmann::json JsonRpcDispatcher::error_response(int error_code,
                                                 const std::string& message,
                                                 const std::string& id)
{
    nlohmann::json response{
        {"jsonrpc", "2.0"},
        {"error", {
            {"code", error_code},
            {"message", message}
        }},
        {"id", nlohmann::json::parse(id)}
    };
    return response;
}
nlohmann::json JsonRpcDispatcher::success_response(const nlohmann::json& result,
                                                   const std::string& id)
{
    nlohmann::json response{
        {"jsonrpc", "2.0"},
        {"result", result},
        {"id", nlohmann::json::parse(id)}
    };
    return response;
}
} // namespace s7
