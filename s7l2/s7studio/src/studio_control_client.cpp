// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-S7-plugin-permission

#include "studio_control_client.hpp"

#include <boost/asio/awaitable.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/redirect_error.hpp>
#include <boost/asio/ssl/context.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/beast/core/buffers_to_string.hpp>
#include <boost/beast/core/flat_buffer.hpp>
#include <boost/beast/core/tcp_stream.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/beast/websocket.hpp>

#include <cstring>
#include <exception>
#include <functional>
#include <memory>
#include <stdexcept>
#include <utility>

namespace s7studio::ui {
namespace {

namespace websocket = boost::beast::websocket;
namespace ssl = boost::asio::ssl;
using Tcp = boost::asio::ip::tcp;

void configureTls(ssl::context& ctx, const s7::transport::TlsConfig& tls)
{
    if (tls.ca_file.empty() || tls.cert_file.empty() || tls.key_file.empty())
        throw std::invalid_argument("Missing TLS files (CA/cert/key) for wss endpoint.");
    ctx.load_verify_file(tls.ca_file);
    ctx.use_certificate_chain_file(tls.cert_file);
    ctx.use_private_key_file(tls.key_file, ssl::context::pem);
    ctx.set_verify_mode(ssl::verify_peer);
}

[[nodiscard]] auto controlTarget(const s7::transport::ParsedWebSocketUrl& parsed) -> std::string
{
    if (parsed.target.empty() || parsed.target == "/") return "/control";
    if (parsed.target.ends_with("/control")) return parsed.target;
    if (parsed.target.ends_with("/data")) {
        auto base = parsed.target;
        base.resize(base.size() - std::strlen("/data"));
        return base.empty() ? "/control" : base + "/control";
    }
    return parsed.target + "/control";
}

struct RequestControl {
    explicit RequestControl(boost::asio::io_context& context) : io(context) {}
    boost::asio::io_context& io;
    std::shared_ptr<Tcp::resolver> resolver;
    std::function<void()> abort;
    std::optional<s7::transport::PeerCertificate> certificate;
    bool timedOut = false;
    bool cancelled = false;
};

template <class Stream>
void abortStream(const std::shared_ptr<Tcp::resolver>& resolver, const std::shared_ptr<Stream>& stream)
{
    resolver->cancel();
    boost::system::error_code ignored;
    auto& socket = boost::beast::get_lowest_layer(*stream).socket();
    socket.cancel(ignored);
    socket.close(ignored);
}

auto requestAsync(RequestControl& control, s7::transport::ParsedWebSocketUrl parsed, std::string requestText,
                  ssl::context* tlsContext, std::string expectedNodeUuid) -> boost::asio::awaitable<std::string>
{
    using boost::asio::use_awaitable;
    control.resolver = std::make_shared<Tcp::resolver>(control.io);
    control.abort = [resolver = control.resolver] { resolver->cancel(); };
    const auto endpoints = co_await control.resolver->async_resolve(parsed.host, parsed.port, use_awaitable);
    const auto hostPort = (parsed.host.find(':') != std::string::npos ? "[" + parsed.host + "]" : parsed.host) + ":" + parsed.port;
    const auto target = controlTarget(parsed);

    if (parsed.secure) {
        auto ws = std::make_shared<websocket::stream<boost::beast::ssl_stream<boost::beast::tcp_stream>>>(control.io, *tlsContext);
        control.abort = [resolver = control.resolver, ws] { abortStream(resolver, ws); };
        co_await boost::beast::get_lowest_layer(*ws).async_connect(endpoints, use_awaitable);
        co_await ws->next_layer().async_handshake(ssl::stream_base::client, use_awaitable);
        control.certificate = s7::transport::verified_peer_certificate_of(ws->next_layer().native_handle());
        if (!expectedNodeUuid.empty() &&
            s7::transport::check_node_identity(control.certificate, expectedNodeUuid) != s7::transport::NodeIdentity::Verified)
            throw std::runtime_error("Control server certificate does not match node " + expectedNodeUuid);
        ws->set_option(websocket::stream_base::timeout::suggested(boost::beast::role_type::client));
        co_await ws->async_handshake(hostPort, target, use_awaitable);
        co_await ws->async_write(boost::asio::buffer(requestText), use_awaitable);
        boost::beast::flat_buffer buffer;
        co_await ws->async_read(buffer, use_awaitable);
        const auto response = boost::beast::buffers_to_string(buffer.data());
        boost::system::error_code closeError;
        co_await ws->async_close(websocket::close_code::normal, boost::asio::redirect_error(use_awaitable, closeError));
        co_return response;
    }

    auto ws = std::make_shared<websocket::stream<boost::beast::tcp_stream>>(control.io);
    control.abort = [resolver = control.resolver, ws] { abortStream(resolver, ws); };
    co_await boost::beast::get_lowest_layer(*ws).async_connect(endpoints, use_awaitable);
    ws->set_option(websocket::stream_base::timeout::suggested(boost::beast::role_type::client));
    co_await ws->async_handshake(hostPort, target, use_awaitable);
    co_await ws->async_write(boost::asio::buffer(requestText), use_awaitable);
    boost::beast::flat_buffer buffer;
    co_await ws->async_read(buffer, use_awaitable);
    const auto response = boost::beast::buffers_to_string(buffer.data());
    boost::system::error_code closeError;
    co_await ws->async_close(websocket::close_code::normal, boost::asio::redirect_error(use_awaitable, closeError));
    co_return response;
}

} // namespace

auto requestStudioControl(const std::string& endpoint, const std::string& requestJson,
                          const s7::transport::TlsConfig& tls, const std::atomic_bool& cancelled,
                          const std::chrono::milliseconds timeout, std::string expectedNodeUuid) -> StudioControlReply
{
    if (cancelled.load()) throw std::runtime_error("Control request cancelled");
    const auto parsed = s7::transport::parse_ws_url(endpoint);
    boost::asio::io_context io;
    std::optional<ssl::context> tlsContext;
    if (parsed.secure) {
        tlsContext.emplace(ssl::context::tls_client);
        configureTls(*tlsContext, tls);
    }
    RequestControl control{io};
    boost::asio::steady_timer deadline(io), poll(io);
    std::optional<std::string> response;
    std::exception_ptr failure;

    deadline.expires_after(timeout);
    deadline.async_wait([&](const boost::system::error_code& ec) {
        if (!ec) {
            control.timedOut = true;
            if (control.abort) control.abort();
        }
    });
    std::function<void()> checkCancellation;
    checkCancellation = [&] {
        poll.expires_after(std::chrono::milliseconds(20));
        poll.async_wait([&](const boost::system::error_code& ec) {
            if (ec) return;
            if (cancelled.load()) {
                control.cancelled = true;
                if (control.abort) control.abort();
            } else {
                checkCancellation();
            }
        });
    };
    checkCancellation();
    boost::asio::co_spawn(io, requestAsync(control, parsed, requestJson, tlsContext ? &*tlsContext : nullptr,
                                          std::move(expectedNodeUuid)),
        [&](std::exception_ptr ex, std::string text) {
            failure = ex;
            if (!ex) response = std::move(text);
            deadline.cancel();
            poll.cancel();
            control.abort = {};
        });
    io.run();
    if (control.cancelled || cancelled.load()) throw std::runtime_error("Control request cancelled");
    if (control.timedOut) throw std::runtime_error("Control request timed out");
    if (failure) std::rethrow_exception(failure);
    if (!response) throw std::runtime_error("Control request did not complete");
    return {std::move(*response), std::move(control.certificate)};
}

} // namespace s7studio::ui
