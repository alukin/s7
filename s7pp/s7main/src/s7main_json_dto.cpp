#include "s7main_json_dto.hpp"

#include <stdexcept>

namespace s7::dto {

auto RpcRequest::fromJson(const boost::json::value& request) -> RpcRequest
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

auto RpcEnvelope::success(const boost::json::value& id, const boost::json::value& result) -> boost::json::value
{
    boost::json::object response;
    response["jsonrpc"] = "2.0";
    response["id"] = id;
    response["result"] = result;
    return response;
}

auto RpcEnvelope::error(const boost::json::value& id, int error_code, const std::string& message) -> boost::json::value
{
    boost::json::object error;
    error["code"] = error_code;
    error["message"] = message;

    boost::json::object response;
    response["jsonrpc"] = "2.0";
    response["id"] = id;
    response["error"] = std::move(error);
    return response;
}

auto SchemaLoadSource::fromJson(const boost::json::value& params) -> SchemaLoadSource
{
    if (!params.is_object()) {
        throw std::runtime_error("Missing 'source' parameter");
    }

    const auto& params_obj = params.as_object();
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

auto RuntimeEventsSubscribeParams::fromJson(const boost::json::value& params) -> RuntimeEventsSubscribeParams
{
    RuntimeEventsSubscribeParams out;
    if (!params.is_object()) {
        return out;
    }

    const auto* topics_value = params.as_object().if_contains("topics");
    if (!topics_value) {
        return out;
    }
    if (!topics_value->is_array()) {
        throw std::runtime_error("runtime.events.subscribe: 'topics' must be an array");
    }

    out.topics.clear();
    for (const auto& item : topics_value->as_array()) {
        out.topics.push_back(boost::json::value_to<std::string>(item));
    }
    if (out.topics.empty()) {
        out.topics = {"schema", "run", "runtime"};
    }
    return out;
}

auto RuntimeEventsSubscribeResult::toJson() const -> boost::json::value
{
    boost::json::object result;
    result["subscribed"] = subscribed;
    result["subscription_id"] = subscription_id;
    result["topics"] = boost::json::value_from(topics);
    return result;
}

auto RuntimeEventsUnsubscribeParams::fromJson(const boost::json::value& params) -> RuntimeEventsUnsubscribeParams
{
    if (!params.is_object()) {
        throw std::runtime_error("runtime.events.unsubscribe: missing params");
    }

    RuntimeEventsUnsubscribeParams out;
    out.subscription_id = boost::json::value_to<uint64_t>(params.as_object().at("subscription_id"));
    return out;
}

auto RuntimeEventsUnsubscribeResult::toJson() const -> boost::json::value
{
    boost::json::object result;
    result["unsubscribed"] = unsubscribed;
    result["subscription_id"] = subscription_id;
    return result;
}

auto RuntimeEventNotification::toJson() const -> boost::json::value
{
    boost::json::object params;
    params["subscription_id"] = subscription_id;
    params["event_name"] = event.event_name;
    params["timestamp_ms"] = event.timestamp_ms;
    params["payload"] = event.payload;

    boost::json::object notification;
    notification["jsonrpc"] = "2.0";
    notification["method"] = "runtime.event";
    notification["params"] = std::move(params);
    return notification;
}

auto SchemaHandleParam::fromJson(const boost::json::value& params) -> SchemaHandleParam
{
    if (!params.is_object()) {
        throw std::runtime_error("Invalid 'params' object");
    }
    return SchemaHandleParam{.schema_handle = boost::json::value_to<std::string>(params.as_object().at("schema_handle"))};
}

auto RunIdParam::fromJson(const boost::json::value& params) -> RunIdParam
{
    if (!params.is_object()) {
        throw std::runtime_error("Invalid 'params' object");
    }
    return RunIdParam{.run_id = boost::json::value_to<std::string>(params.as_object().at("run_id"))};
}

auto SchemaRunParams::fromJson(const boost::json::value& params) -> SchemaRunParams
{
    if (!params.is_object()) {
        throw std::runtime_error("Invalid 'params' object");
    }

    const auto& obj = params.as_object();
    const auto& mode = obj.at("mode").as_object();
    const auto* ticks_ptr = mode.if_contains("ticks");

    SchemaRunParams out;
    out.schema_handle = boost::json::value_to<std::string>(obj.at("schema_handle"));
    out.ticks = ticks_ptr ? boost::json::value_to<long>(*ticks_ptr) : 1;
    out.mode = mode;
    return out;
}

} // namespace s7::dto


