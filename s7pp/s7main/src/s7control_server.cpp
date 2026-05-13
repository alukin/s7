#include "s7control_server.hpp"

#include "s7json_rpc.hpp"
#include "s7main_json_dto.hpp"

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
					const auto rpc = dto::RpcRequest::fromJson(parsed);
					const std::string method = rpc.method;

					if (method == "runtime.events.subscribe") {
						const auto id = rpc.id;
						const auto params = rpc.params;
						const auto topics = dto::RuntimeEventsSubscribeParams::fromJson(params).topics;

						auto subscription_id_ref = std::make_shared<std::atomic<uint64_t>>(0);
						const auto subscription_id = service_->subscribeToEvents(
							topics,
							[&ws, &ws_write_mutex, &session_active, subscription_id_ref](const Event& event) {
								if (!session_active.load(std::memory_order_acquire)) {
									return;
								}

								const auto sid = subscription_id_ref->load(std::memory_order_acquire);
								auto notification_obj = dto::RuntimeEventNotification{.subscription_id = sid, .event = event}.toJson();

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
						response = dto::RpcEnvelope::success(
							id,
							dto::RuntimeEventsSubscribeResult{
								.subscribed = true,
								.subscription_id = subscription_id,
								.topics = topics}
								.toJson());
					} else if (method == "runtime.events.unsubscribe") {
						const auto id = rpc.id;
						const auto params = rpc.params;
						const auto unsub = dto::RuntimeEventsUnsubscribeParams::fromJson(params);
						const auto subscription_id = unsub.subscription_id;
						service_->unsubscribeFromEvents(subscription_id);
						subscriptions.erase(
							std::remove(subscriptions.begin(), subscriptions.end(), subscription_id),
							subscriptions.end());
						response = dto::RpcEnvelope::success(
							id,
							dto::RuntimeEventsUnsubscribeResult{.unsubscribed = true, .subscription_id = subscription_id}.toJson());
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


