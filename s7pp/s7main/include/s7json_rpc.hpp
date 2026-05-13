#pragma once

#include "s7runtime.hpp"

#include <memory>
#include <string>

#include <boost/json.hpp>

namespace s7 {

class JsonRpcDispatcher {
public:
	explicit JsonRpcDispatcher(std::shared_ptr<NodeRuntimeService> service);

	auto dispatch(const std::string& json_request_str) -> boost::json::value;
	auto dispatchParsed(const boost::json::value& request) -> boost::json::value;

private:
	std::shared_ptr<NodeRuntimeService> service_;

	auto handle_runtime_hello(const boost::json::value& params, const boost::json::value& id) -> boost::json::value;
	auto handle_runtime_types_list(const boost::json::value& params, const boost::json::value& id) -> boost::json::value;
	auto handle_runtime_status(const boost::json::value& params, const boost::json::value& id) -> boost::json::value;
	auto handle_schema_load(const boost::json::value& params, const boost::json::value& id) -> boost::json::value;
	auto handle_schema_validate(const boost::json::value& params, const boost::json::value& id) -> boost::json::value;
	auto handle_schema_run(const boost::json::value& params, const boost::json::value& id) -> boost::json::value;
	auto handle_schema_pause(const boost::json::value& params, const boost::json::value& id) -> boost::json::value;
	auto handle_schema_resume(const boost::json::value& params, const boost::json::value& id) -> boost::json::value;
	auto handle_schema_stop(const boost::json::value& params, const boost::json::value& id) -> boost::json::value;
	auto handle_schema_reset(const boost::json::value& params, const boost::json::value& id) -> boost::json::value;
	auto handle_schema_unload(const boost::json::value& params, const boost::json::value& id) -> boost::json::value;

	static auto error_response(int error_code, const std::string& message, const boost::json::value& id)
		-> boost::json::value;
	static auto success_response(const boost::json::value& result, const boost::json::value& id)
		-> boost::json::value;
};

} // namespace s7
