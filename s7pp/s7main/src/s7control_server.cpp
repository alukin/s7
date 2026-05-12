#include "s7control_server.hpp"

#include "s7json_rpc.hpp"

#include <boost/asio/connect.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/websocket.hpp>

#include <atomic>
#include <algorithm>
#include <iostream>
#include <mutex>
#include <boost/json.hpp>
#include <string>
#include <thread>
#include <vector>

namespace s7 {
namespace {

namespace asio = boost::asio;
namespace beast = boost::beast;
namespace http = beast::http;
namespace websocket = beast::websocket;
using tcp = asio::ip::tcp;
using json = boost::json::value;

auto parseTopics(const json& params) -> std::vector<std::string>
{
	if (!params.is_object()) {
		return {"schema", "run", "runtime"};
	}
	const auto& obj = params.as_object();
	const auto* topics_value = obj.if_contains("topics");
	if (!topics_value) {
		return {"schema", "run", "runtime"};
	}
	if (!topics_value->is_array()) {
		throw std::runtime_error("runtime.events.subscribe: 'topics' must be an array");
	}

	std::vector<std::string> topics;
	for (const auto& item : topics_value->as_array()) {
		topics.push_back(boost::json::value_to<std::string>(item));
	}
	if (topics.empty()) {
		topics = {"schema", "run", "runtime"};
	}
	return topics;
}

class ControlServer : public std::enable_shared_from_this<ControlServer> {
public:
	ControlServer(std::shared_ptr<NodeRuntimeService> service, int port)
		: service_(std::move(service)), dispatcher_(service_), acceptor_(ioc_)
	{
		tcp::endpoint endpoint{tcp::v4(), static_cast<unsigned short>(port)};
		acceptor_.open(endpoint.protocol());
		acceptor_.set_option(tcp::acceptor::reuse_address(true));
		acceptor_.bind(endpoint);
		acceptor_.listen();
	}

	void start()
	{
		auto self = shared_from_this();
		thread_ = std::thread([self] { self->run(); });
	}

	void stop()
	{
		if (stopped_.exchange(true)) {
			return;
		}
		beast::error_code ec;
		acceptor_.close(ec);
		ioc_.stop();
		if (thread_.joinable()) {
			thread_.join();
		}
	}

	~ControlServer()
	{
		stop();
	}

private:
	void run()
	{
		while (!stopped_.load()) {
			beast::error_code ec;
			tcp::socket socket(ioc_);
			acceptor_.accept(socket, ec);
			if (ec) {
				if (stopped_.load()) {
					break;
				}
				continue;
			}

			std::thread([self = shared_from_this(), socket = std::move(socket)]() mutable {
				self->session(std::move(socket));
			}).detach();
		}
	}

	void session(tcp::socket socket)
	{
		std::mutex ws_write_mutex;
		std::vector<uint64_t> subscriptions;
		std::atomic_bool session_active{true};

		auto cleanup_subscriptions = [&] {
			session_active.store(false, std::memory_order_release);
			for (const auto subscription_id : subscriptions) {
				service_->unsubscribeFromEvents(subscription_id);
			}
			subscriptions.clear();
		};

		try {
			beast::flat_buffer buffer;
			http::request<http::string_body> req;
			http::read(socket, buffer, req);
			websocket::stream<tcp::socket> ws(std::move(socket));
			ws.accept(req);

			for (;;) {
				beast::flat_buffer msg;
				ws.read(msg);
				const auto request = beast::buffers_to_string(msg.data());

				json response;
				try {
					const auto parsed = boost::json::parse(request);
					const auto& parsed_obj = parsed.as_object();
					const auto* method_value = parsed_obj.if_contains("method");
					const std::string method = (method_value && method_value->is_string())
						? boost::json::value_to<std::string>(*method_value)
						: std::string{};

					if (method == "runtime.events.subscribe") {
						const auto* id_ptr = parsed_obj.if_contains("id");
						const auto id = id_ptr ? *id_ptr : json(nullptr);
						const auto* params_ptr = parsed_obj.if_contains("params");
						const auto params = params_ptr ? *params_ptr : json(boost::json::object{});
						const auto topics = parseTopics(params);

						auto subscription_id_ref = std::make_shared<std::atomic<uint64_t>>(0);
						const auto subscription_id = service_->subscribeToEvents(
							topics,
							[&ws, &ws_write_mutex, &session_active, subscription_id_ref](const Event& event) {
								if (!session_active.load(std::memory_order_acquire)) {
									return;
								}

								const auto sid = subscription_id_ref->load(std::memory_order_acquire);
								boost::json::object params_obj;
								params_obj["subscription_id"] = sid;
								params_obj["event_name"] = event.event_name;
								params_obj["timestamp_ms"] = event.timestamp_ms;
								params_obj["payload"] = event.payload;
								boost::json::object notification_obj;
								notification_obj["jsonrpc"] = "2.0";
								notification_obj["method"] = "runtime.event";
								notification_obj["params"] = std::move(params_obj);

								std::scoped_lock lock(ws_write_mutex);
								try {
									ws.text(true);
									ws.write(asio::buffer(boost::json::serialize(notification_obj)));
								} catch (...) {
									// Ignore socket errors; session loop handles cleanup.
								}
							});
						subscription_id_ref->store(subscription_id, std::memory_order_release);

						subscriptions.push_back(subscription_id);
						boost::json::object result_obj;
						result_obj["subscribed"] = true;
						result_obj["subscription_id"] = subscription_id;
						result_obj["topics"] = boost::json::value_from(topics);
						boost::json::object response_obj;
						response_obj["jsonrpc"] = "2.0";
						response_obj["id"] = id;
						response_obj["result"] = std::move(result_obj);
						response = std::move(response_obj);
					} else if (method == "runtime.events.unsubscribe") {
						const auto* id_ptr = parsed_obj.if_contains("id");
						const auto id = id_ptr ? *id_ptr : json(nullptr);
						const auto* params_ptr = parsed_obj.if_contains("params");
						if (!params_ptr || !params_ptr->is_object()) {
							throw std::runtime_error("runtime.events.unsubscribe: missing params");
						}
						const auto subscription_id = boost::json::value_to<uint64_t>(params_ptr->as_object().at("subscription_id"));
						service_->unsubscribeFromEvents(subscription_id);
						subscriptions.erase(
							std::remove(subscriptions.begin(), subscriptions.end(), subscription_id),
							subscriptions.end());
						boost::json::object result_obj;
						result_obj["unsubscribed"] = true;
						result_obj["subscription_id"] = subscription_id;
						boost::json::object response_obj;
						response_obj["jsonrpc"] = "2.0";
						response_obj["id"] = id;
						response_obj["result"] = std::move(result_obj);
						response = std::move(response_obj);
					} else {
						response = dispatcher_.dispatchParsed(parsed);
					}
				} catch (...) {
					response = dispatcher_.dispatch(request);
				}

				std::scoped_lock lock(ws_write_mutex);
				ws.text(true);
				ws.write(asio::buffer(boost::json::serialize(response)));
			}
		} catch (const boost::system::system_error& ex) {
			cleanup_subscriptions();
			if (ex.code() != websocket::error::closed) {
				std::cerr << "control websocket session ended: " << ex.what() << '\n';
			}
			return;
		} catch (const std::exception& ex) {
			cleanup_subscriptions();
			std::cerr << "control websocket session ended: " << ex.what() << '\n';
			return;
		}

		cleanup_subscriptions();
	}

	std::shared_ptr<NodeRuntimeService> service_;
	JsonRpcDispatcher dispatcher_;
	asio::io_context ioc_{1};
	tcp::acceptor acceptor_;
	std::thread thread_;
	std::atomic_bool stopped_{false};
};

} // namespace

auto runControlServer(const std::shared_ptr<NodeRuntimeService>& service, int port) -> int
{
	auto server = std::make_shared<ControlServer>(service, port);
	server->start();

	std::cout << "Control websocket server listening on port " << port << "\n";
	std::cout << "Press Enter to stop server...\n";
	std::string line;
	std::getline(std::cin, line);

	server->stop();
	return 0;
}

} // namespace s7


