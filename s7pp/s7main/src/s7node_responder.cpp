#include "s7node_responder.hpp"

#include <folly/ExceptionWrapper.h>

#include <stdexcept>
namespace s7 {
S7NodeResponder::S7NodeResponder(std::shared_ptr<NodeRuntimeService> service)
    : service_(service), dispatcher_(std::make_shared<JsonRpcDispatcher>(service))
{
}
std::shared_ptr<yarpl::single::Single<rsocket::Payload>>
S7NodeResponder::handleRequestResponse(rsocket::Payload request,
                                        rsocket::StreamId streamId)
{
    return yarpl::single::Single<rsocket::Payload>::create(
        [this, request = std::move(request)](auto subscriber) mutable {
            try {
                std::string request_str = request.moveDataToString();
                nlohmann::json response_json = dispatcher_->dispatch(request_str);
                std::string response_str = response_json.dump();
                subscriber->onSubscribe(
                    yarpl::single::SingleSubscriptions::empty());
                subscriber->onSuccess(rsocket::Payload(response_str));
            } catch (const std::exception& e) {
                subscriber->onSubscribe(
                    yarpl::single::SingleSubscriptions::empty());
                subscriber->onError(folly::make_exception_wrapper<std::runtime_error>(e.what()));
            }
        });
}
} // namespace s7
