#include "s7ctl_json_dto.hpp"

#include <stdexcept>

namespace s7::ctl::dto {

auto JsonRpcRequest::toJson() const -> boost::json::value
{
    boost::json::object obj;
    obj["jsonrpc"] = "2.0";
    obj["id"] = id;
    obj["method"] = method;
    obj["params"] = params;
    return obj;
}

auto TopicsList::toJson() const -> boost::json::value
{
    boost::json::object obj;
    obj["topics"] = boost::json::value_from(topics);
    return obj;
}

auto SubscribeAck::fromJson(const boost::json::value& value) -> SubscribeAck
{
    SubscribeAck ack;
    if (!value.is_object()) {
        return ack;
    }

    const auto& obj = value.as_object();
    const auto* result = obj.if_contains("result");
    if (!result || !result->is_object()) {
        return ack;
    }

    const auto* sid = result->as_object().if_contains("subscription_id");
    if (sid && (sid->is_uint64() || sid->is_int64())) {
        ack.subscriptionId = boost::json::value_to<uint64_t>(*sid);
    }
    return ack;
}

namespace {

auto makeRequest(long id, const std::string& method, boost::json::value params) -> boost::json::value
{
    return JsonRpcRequest{.id = id, .method = method, .params = std::move(params)}.toJson();
}

} // namespace

auto RequestFactory::hello(long id) -> boost::json::value
{
    return makeRequest(id, "runtime.hello", boost::json::object{{"client_name", "s7ctl"}});
}

auto RequestFactory::types(long id) -> boost::json::value
{
    return makeRequest(id, "runtime.types.list", boost::json::object{});
}

auto RequestFactory::status(long id) -> boost::json::value
{
    return makeRequest(id, "runtime.status", boost::json::object{});
}

auto RequestFactory::loadFilesystem(long id, const std::string& path) -> boost::json::value
{
    return makeRequest(id, "schema.load", boost::json::object{{"source", boost::json::object{{"kind", "filesystem"}, {"path", path}}}});
}

auto RequestFactory::loadInline(long id, const std::string& schema_json) -> boost::json::value
{
    return makeRequest(id, "schema.load", boost::json::object{{"source", boost::json::object{{"kind", "inline"}, {"schema_json", schema_json}}}});
}

auto RequestFactory::validate(long id, const std::string& schema_handle) -> boost::json::value
{
    return makeRequest(id, "schema.validate", boost::json::object{{"schema_handle", schema_handle}});
}

auto RequestFactory::run(long id, const std::string& schema_handle, long ticks) -> boost::json::value
{
    return makeRequest(id, "schema.run", boost::json::object{{"schema_handle", schema_handle}, {"mode", boost::json::object{{"ticks", ticks}}}});
}

auto RequestFactory::pause(long id, const std::string& run_id) -> boost::json::value
{
    return makeRequest(id, "schema.pause", boost::json::object{{"run_id", run_id}});
}

auto RequestFactory::resume(long id, const std::string& run_id) -> boost::json::value
{
    return makeRequest(id, "schema.resume", boost::json::object{{"run_id", run_id}});
}

auto RequestFactory::stop(long id, const std::string& run_id) -> boost::json::value
{
    return makeRequest(id, "schema.stop", boost::json::object{{"run_id", run_id}});
}

auto RequestFactory::reset(long id, const std::string& schema_handle) -> boost::json::value
{
    return makeRequest(id, "schema.reset", boost::json::object{{"schema_handle", schema_handle}});
}

auto RequestFactory::unload(long id, const std::string& schema_handle) -> boost::json::value
{
    return makeRequest(id, "schema.unload", boost::json::object{{"schema_handle", schema_handle}});
}

auto RequestFactory::raw(long id, const std::string& method, const std::string& params_json) -> boost::json::value
{
    return makeRequest(id, method, boost::json::parse(params_json));
}

auto RequestFactory::eventsSubscribe(long id, const std::vector<std::string>& topics) -> boost::json::value
{
    return makeRequest(id, "runtime.events.subscribe", TopicsList{.topics = topics}.toJson());
}

auto RequestFactory::eventsUnsubscribe(long id, uint64_t subscription_id) -> boost::json::value
{
    return makeRequest(id, "runtime.events.unsubscribe", boost::json::object{{"subscription_id", subscription_id}});
}

} // namespace s7::ctl::dto


