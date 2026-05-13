#pragma once

#include <s7events.hpp>

#include <boost/json.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace s7::dto {

struct RpcRequest {
    boost::json::value id = nullptr;
    std::string method;
    boost::json::value params = boost::json::object{};

    static auto fromJson(const boost::json::value& request) -> RpcRequest;
};

struct RpcEnvelope {
    static auto success(const boost::json::value& id, const boost::json::value& result) -> boost::json::value;
    static auto error(const boost::json::value& id, int error_code, const std::string& message) -> boost::json::value;
};

struct SchemaLoadSource {
    std::string kind = "inline";
    std::string schema_json;
    std::string schema_name = "unnamed";
    std::string path;

    static auto fromJson(const boost::json::value& params) -> SchemaLoadSource;
};

struct RuntimeEventsSubscribeParams {
    std::vector<std::string> topics{"schema", "run", "runtime"};

    static auto fromJson(const boost::json::value& params) -> RuntimeEventsSubscribeParams;
};

struct RuntimeEventsSubscribeResult {
    bool subscribed = true;
    uint64_t subscription_id = 0;
    std::vector<std::string> topics;

    [[nodiscard]] auto toJson() const -> boost::json::value;
};

struct RuntimeEventsUnsubscribeParams {
    uint64_t subscription_id = 0;

    static auto fromJson(const boost::json::value& params) -> RuntimeEventsUnsubscribeParams;
};

struct RuntimeEventsUnsubscribeResult {
    bool unsubscribed = true;
    uint64_t subscription_id = 0;

    [[nodiscard]] auto toJson() const -> boost::json::value;
};

struct RuntimeEventNotification {
    uint64_t subscription_id = 0;
    Event event;

    [[nodiscard]] auto toJson() const -> boost::json::value;
};

struct SchemaHandleParam {
    std::string schema_handle;
    static auto fromJson(const boost::json::value& params) -> SchemaHandleParam;
};

struct RunIdParam {
    std::string run_id;
    static auto fromJson(const boost::json::value& params) -> RunIdParam;
};

struct SchemaRunParams {
    std::string schema_handle;
    long ticks = 1;
    boost::json::value mode = boost::json::object{{"ticks", 1}};

    static auto fromJson(const boost::json::value& params) -> SchemaRunParams;
};

} // namespace s7::dto


