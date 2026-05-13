#pragma once

#include <boost/json.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace s7::ctl::dto {

struct JsonRpcRequest {
    long id = 0;
    std::string method;
    boost::json::value params = boost::json::object{};

    [[nodiscard]] auto toJson() const -> boost::json::value;
};

struct TopicsList {
    std::vector<std::string> topics;

    [[nodiscard]] auto toJson() const -> boost::json::value;
};

struct SubscribeAck {
    uint64_t subscriptionId = 0;

    static auto fromJson(const boost::json::value& value) -> SubscribeAck;
};

class RequestFactory {
public:
    static auto hello(long id) -> boost::json::value;
    static auto types(long id) -> boost::json::value;
    static auto status(long id) -> boost::json::value;
    static auto loadFilesystem(long id, const std::string& path) -> boost::json::value;
    static auto loadInline(long id, const std::string& schema_json) -> boost::json::value;
    static auto validate(long id, const std::string& schema_handle) -> boost::json::value;
    static auto run(long id, const std::string& schema_handle, long ticks) -> boost::json::value;
    static auto pause(long id, const std::string& run_id) -> boost::json::value;
    static auto resume(long id, const std::string& run_id) -> boost::json::value;
    static auto stop(long id, const std::string& run_id) -> boost::json::value;
    static auto reset(long id, const std::string& schema_handle) -> boost::json::value;
    static auto unload(long id, const std::string& schema_handle) -> boost::json::value;
    static auto raw(long id, const std::string& method, const std::string& params_json) -> boost::json::value;
    static auto eventsSubscribe(long id, const std::vector<std::string>& topics) -> boost::json::value;
    static auto eventsUnsubscribe(long id, uint64_t subscription_id) -> boost::json::value;
};

} // namespace s7::ctl::dto


