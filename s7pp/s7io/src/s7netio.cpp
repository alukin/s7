#include "s7netio.hpp"
#include "s7io_json_dto.hpp"

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <boost/json.hpp>
#include <optional>
#include <stdexcept>
#include <thread>
#include <unordered_map>
#include <utility>

#include <boost/asio/connect.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/strand.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/websocket.hpp>

namespace s7 {
namespace {

namespace asio = boost::asio;
namespace beast = boost::beast;
namespace http = beast::http;
namespace websocket = beast::websocket;
using tcp = asio::ip::tcp;
using json = boost::json::value;

struct EndpointUri {
    std::string scheme;
    std::string host;
    std::string port;
    std::string path;
    std::string authority;
    std::string hostport_key;
};

using WsFrameRequest = s7::io::dto::WsFrameRequest;
using WsFrameResponse = s7::io::dto::WsFrameResponse;

auto normalizeScheme(std::string scheme) -> std::string
{
    if (scheme.empty() || scheme == "net") {
        return "ws";
    }
    if (scheme == "ws" || scheme == "wss") {
        return scheme;
    }
    throw std::runtime_error("s7netio: unsupported endpoint scheme '" + scheme + "'");
}

auto parseEndpoint(const std::string& endpoint) -> EndpointUri
{
    const auto scheme_pos = endpoint.find("://");
    const std::string scheme = normalizeScheme(
        scheme_pos == std::string::npos ? std::string{} : endpoint.substr(0, scheme_pos));
    const std::string remainder =
        scheme_pos == std::string::npos ? endpoint : endpoint.substr(scheme_pos + 3);

    if (remainder.empty()) {
        throw std::runtime_error("s7netio: endpoint must not be empty");
    }

    const auto path_pos = remainder.find('/');
    const std::string authority = path_pos == std::string::npos ? remainder : remainder.substr(0, path_pos);
    std::string path = path_pos == std::string::npos ? "/" : remainder.substr(path_pos);
    if (path.empty()) {
        path = "/";
    }

    std::string host;
    std::string port = "5577";
    if (!authority.empty() && authority.front() == '[') {
        const auto close = authority.find(']');
        if (close == std::string::npos) {
            throw std::runtime_error("s7netio: invalid IPv6 authority in endpoint: " + endpoint);
        }
        host = authority.substr(1, close - 1);
        if (close + 1 < authority.size()) {
            if (authority[close + 1] != ':') {
                throw std::runtime_error("s7netio: invalid IPv6 port syntax in endpoint: " + endpoint);
            }
            port = authority.substr(close + 2);
        }
    } else {
        const auto colon = authority.rfind(':');
        if (colon != std::string::npos && authority.find(':') == colon) {
            host = authority.substr(0, colon);
            port = authority.substr(colon + 1);
        } else {
            host = authority;
        }
    }

    if (host.empty()) {
        throw std::runtime_error("s7netio: endpoint host must not be empty");
    }
    if (port.empty()) {
        throw std::runtime_error("s7netio: endpoint port must not be empty");
    }

    EndpointUri uri;
    uri.scheme = scheme;
    uri.host = host;
    uri.port = port;
    uri.path = path;
    uri.authority = authority;
    uri.hostport_key = host + ":" + port;
    return uri;
}

void validateConfig(const NetIOConfig& cfg)
{
    if (cfg.endpoint.empty()) {
        throw std::runtime_error("s7netio: endpoint must not be empty");
    }
    if (cfg.num_channels == 0) {
        throw std::runtime_error("s7netio: num_channels must be > 0");
    }
    if (cfg.queue_capacity == 0) {
        throw std::runtime_error("s7netio: queue_capacity must be > 0");
    }
}

struct ChannelState {
    std::size_t frame_width = 0;
    std::size_t queue_capacity = 0;
    std::deque<std::vector<double>> queue;
};

class WebSocketEndpointServer : public std::enable_shared_from_this<WebSocketEndpointServer> {
public:
    explicit WebSocketEndpointServer(EndpointUri uri)
        : uri_(std::move(uri)), resolver_(ioc_), acceptor_(ioc_)
    {
        auto results = resolver_.resolve(uri_.host, uri_.port);
        auto endpoint = results.begin()->endpoint();
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

    ~WebSocketEndpointServer()
    {
        stop();
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

    void ensureChannel(const std::string& path, std::size_t frame_width, std::size_t capacity)
    {
        std::scoped_lock lock(mu_);
        auto& ch = channels_[path];
        if (ch.frame_width == 0) {
            ch.frame_width = frame_width;
            ch.queue_capacity = std::max<std::size_t>(1, capacity);
            return;
        }
        if (ch.frame_width != frame_width) {
            throw std::runtime_error(
                "s7netio: websocket endpoint frame-width mismatch for path '" + path + "'");
        }
    }

    bool push(const std::string& path, std::vector<double> sample, bool& dropped)
    {
        std::scoped_lock lock(mu_);
        auto it = channels_.find(path);
        if (it == channels_.end()) {
            throw std::runtime_error("s7netio: websocket path not initialized: " + path);
        }
        auto& ch = it->second;
        if (sample.size() != ch.frame_width) {
            throw std::runtime_error("s7netio: websocket sample width mismatch");
        }
        dropped = false;
        if (ch.queue.size() >= ch.queue_capacity) {
            ch.queue.pop_front();
            dropped = true;
        }
        ch.queue.push_back(std::move(sample));
        return true;
    }

    bool pop(const std::string& path, std::vector<double>& out)
    {
        std::scoped_lock lock(mu_);
        auto it = channels_.find(path);
        if (it == channels_.end() || it->second.queue.empty()) {
            return false;
        }
        out = std::move(it->second.queue.front());
        it->second.queue.pop_front();
        return true;
    }

private:
    auto makeError(const std::string& message) const -> WsFrameResponse
    {
        return WsFrameResponse::errorResponse(message);
    }

    auto handlePush(const std::string& path, const WsFrameRequest& request) -> WsFrameResponse
    {
        if (request.sample.empty()) {
            return makeError("missing array field 'sample'");
        }
        bool dropped = false;
        push(path, request.sample, dropped);
        WsFrameResponse response;
        response.ok = true;
        response.dropped = dropped;
        return response;
    }

    auto handlePull(const std::string& path) -> WsFrameResponse
    {
        std::vector<double> frame;
        if (!pop(path, frame)) {
            WsFrameResponse response;
            response.ok = true;
            response.empty = true;
            return response;
        }
        WsFrameResponse response;
        response.ok = true;
        response.sample = std::move(frame);
        return response;
    }

    json dispatch(const std::string& path, const json& request)
    {
        try {
            const auto frame = WsFrameRequest::fromJson(request);
            if (frame.op == "push") {
                return handlePush(path, frame).toJson();
            }
            if (frame.op == "pull") {
                return handlePull(path).toJson();
            }
            return makeError("unknown websocket op: " + frame.op).toJson();
        } catch (const std::exception& ex) {
            return makeError(ex.what()).toJson();
        }
    }

    void session(tcp::socket socket)
    {
        try {
            beast::flat_buffer buffer;
            http::request<http::string_body> req;
            http::read(socket, buffer, req);
            const std::string target = std::string(req.target());

            {
                std::lock_guard<std::mutex> lock(mu_);
                auto it = channels_.find(target);
                if (it == channels_.end()) {
                    return;
                }
            }

            websocket::stream<tcp::socket> ws(std::move(socket));
            ws.accept(req);

            for (;;) {
                beast::flat_buffer msg;
                ws.read(msg);
                const auto payload = beast::buffers_to_string(msg.data());
                json request = boost::json::parse(payload);
                json response = dispatch(target, request);
                ws.text(true);
                ws.write(asio::buffer(boost::json::serialize(response)));
            }
        } catch (const std::exception&) {
            return;
        }
    }

    void run()
    {
        while (!stopped_.load(std::memory_order_acquire)) {
            beast::error_code ec;
            tcp::socket socket(ioc_);
            acceptor_.accept(socket, ec);
            if (ec) {
                if (stopped_.load(std::memory_order_acquire)) {
                    break;
                }
                continue;
            }
            std::thread([self = shared_from_this(), socket = std::move(socket)]() mutable {
                self->session(std::move(socket));
            }).detach();
        }
    }

    EndpointUri uri_;
    asio::io_context ioc_{1};
    tcp::resolver resolver_;
    tcp::acceptor acceptor_;
    std::thread thread_;
    std::atomic_bool stopped_{false};
    mutable std::mutex mu_;
    std::unordered_map<std::string, ChannelState> channels_;
};

class WebSocketEndpointClient {
public:
    explicit WebSocketEndpointClient(const EndpointUri& uri)
        : uri_(uri), resolver_(ioc_), ws_(ioc_)
    {
        auto results = resolver_.resolve(uri_.host, uri_.port);
        asio::connect(ws_.next_layer(), results.begin(), results.end());
        ws_.handshake(uri_.authority, uri_.path);
    }

    ~WebSocketEndpointClient()
    {
        beast::error_code ec;
        if (ws_.is_open()) {
            ws_.close(websocket::close_code::normal, ec);
        }
    }

    json transact(const json& request)
    {
        std::lock_guard<std::mutex> lock(mu_);
        ws_.text(true);
        const auto payload = boost::json::serialize(request);
        ws_.write(asio::buffer(payload));
        beast::flat_buffer buffer;
        ws_.read(buffer);
        return boost::json::parse(beast::buffers_to_string(buffer.data()));
    }

private:
    EndpointUri uri_;
    asio::io_context ioc_{1};
    tcp::resolver resolver_;
    websocket::stream<tcp::socket> ws_;
    std::mutex mu_;
};

struct EndpointHandle {
    EndpointUri uri;
    std::shared_ptr<WebSocketEndpointServer> server;
    std::shared_ptr<WebSocketEndpointClient> client;
};

class TransportHub {
public:
    static TransportHub& instance()
    {
        static TransportHub hub;
        return hub;
    }

    std::shared_ptr<EndpointHandle> ensure(const NetIOConfig& cfg, std::size_t frame_width)
    {
        const auto uri = parseEndpoint(cfg.endpoint);
        const auto key = uri.hostport_key;

        std::lock_guard<std::mutex> lock(mu_);
        auto& handle = endpoints_[key];
        if (!handle) {
            handle = std::make_shared<EndpointHandle>();
            handle->uri = uri;
            handle->server = std::make_shared<WebSocketEndpointServer>(uri);
            handle->server->ensureChannel(uri.path, frame_width, cfg.queue_capacity);
            handle->server->start();
            handle->client = std::make_shared<WebSocketEndpointClient>(uri);
            return handle;
        }

        if (handle->uri.path != uri.path) {
            handle->server->ensureChannel(uri.path, frame_width, cfg.queue_capacity);
            auto new_client = std::make_shared<WebSocketEndpointClient>(uri);
            return std::make_shared<EndpointHandle>(EndpointHandle{uri, handle->server, new_client});
        }

        handle->server->ensureChannel(uri.path, frame_width, cfg.queue_capacity);
        return handle;
    }

private:
    std::mutex mu_;
    std::unordered_map<std::string, std::shared_ptr<EndpointHandle>> endpoints_;
};

} // namespace

class RealNetSink::Impl {
public:
    explicit Impl(NetIOConfig cfg)
        : cfg_(std::move(cfg)), handle_(TransportHub::instance().ensure(cfg_, cfg_.num_channels))
    {
        validateConfig(cfg_);
    }

    NetIOConfig cfg_;
    std::shared_ptr<EndpointHandle> handle_;
};

class RealNetSource::Impl {
public:
    explicit Impl(NetIOConfig cfg)
        : cfg_(std::move(cfg)), handle_(TransportHub::instance().ensure(cfg_, cfg_.num_channels))
    {
        validateConfig(cfg_);
    }

    NetIOConfig cfg_;
    std::shared_ptr<EndpointHandle> handle_;
};

class ComplexNetSink::Impl {
public:
    explicit Impl(NetIOConfig cfg)
        : cfg_(std::move(cfg)), handle_(TransportHub::instance().ensure(cfg_, cfg_.num_channels * 2u))
    {
        validateConfig(cfg_);
    }

    NetIOConfig cfg_;
    std::shared_ptr<EndpointHandle> handle_;
};

class ComplexNetSource::Impl {
public:
    explicit Impl(NetIOConfig cfg)
        : cfg_(std::move(cfg)), handle_(TransportHub::instance().ensure(cfg_, cfg_.num_channels * 2u))
    {
        validateConfig(cfg_);
    }

    NetIOConfig cfg_;
    std::shared_ptr<EndpointHandle> handle_;
};

RealNetSink::RealNetSink(const std::string& id, const NetIOConfig& cfg)
    : Element(id), impl_(std::make_unique<Impl>(cfg))
{
    setTraits(kIsStateful, kHasDirectFeedthrough);
    inputs_.reserve(cfg.num_channels);
    for (uint32_t i = 0; i < cfg.num_channels; ++i) {
        inputs_.push_back(addInput<Real>());
    }
}

RealNetSink::~RealNetSink() = default;

void RealNetSink::tick()
{
    std::vector<double> sample;
    sample.reserve(inputs_.size());
    for (const auto& in : inputs_) {
        sample.push_back(in->value());
    }

    WsFrameRequest request;
    request.op = "push";
    request.sample = std::move(sample);
    const auto response = WsFrameResponse::fromJson(
        impl_->handle_->client->transact(request.toJson()),
        "s7netio websocket push failed");
    if (!response.ok) {
        throw std::runtime_error(response.error.value_or("s7netio websocket push failed"));
    }

    ++pushed_samples_;
    if (response.dropped.value_or(false)) {
        ++dropped_samples_;
    }
}

RealNetSource::RealNetSource(const std::string& id, const NetIOConfig& cfg)
    : Element(id), impl_(std::make_unique<Impl>(cfg)), values_(cfg.num_channels, 0.0)
{
    setTraits(kIsStateful, kHasDirectFeedthrough);
    for (auto& v : values_) {
        addOutput<Real>(v);
    }
}

RealNetSource::~RealNetSource() = default;

void RealNetSource::tick()
{
    WsFrameRequest request;
    request.op = "pull";
    const auto response = WsFrameResponse::fromJson(
        impl_->handle_->client->transact(request.toJson()),
        "s7netio websocket pull failed");
    if (!response.ok) {
        throw std::runtime_error(response.error.value_or("s7netio websocket pull failed"));
    }

    if (response.empty.value_or(false)) {
        ++underruns_;
        if (impl_->cfg_.zero_on_underrun) {
            std::fill(values_.begin(), values_.end(), 0.0);
        }
        return;
    }

    auto sample = response.sample.value_or(std::vector<double>{});
    if (sample.size() != values_.size()) {
        throw std::runtime_error("s7netio: received sample width mismatch");
    }

    for (std::size_t i = 0; i < values_.size(); ++i) {
        values_[i] = sample[i];
    }
    ++pulled_samples_;
}

ComplexNetSink::ComplexNetSink(const std::string& id, const NetIOConfig& cfg)
    : Element(id), impl_(std::make_unique<Impl>(cfg))
{
    setTraits(kIsStateful, kHasDirectFeedthrough);
    inputs_.reserve(cfg.num_channels);
    for (uint32_t i = 0; i < cfg.num_channels; ++i) {
        inputs_.push_back(addInput<Complex>());
    }
}

ComplexNetSink::~ComplexNetSink() = default;

void ComplexNetSink::tick()
{
    std::vector<double> sample;
    sample.reserve(inputs_.size() * 2u);
    for (const auto& in : inputs_) {
        const auto v = in->value();
        sample.push_back(v.real());
        sample.push_back(v.imag());
    }

    WsFrameRequest request;
    request.op = "push";
    request.sample = std::move(sample);
    const auto response = WsFrameResponse::fromJson(
        impl_->handle_->client->transact(request.toJson()),
        "s7netio websocket push failed");
    if (!response.ok) {
        throw std::runtime_error(response.error.value_or("s7netio websocket push failed"));
    }

    ++pushed_samples_;
    if (response.dropped.value_or(false)) {
        ++dropped_samples_;
    }
}

ComplexNetSource::ComplexNetSource(const std::string& id, const NetIOConfig& cfg)
    : Element(id), impl_(std::make_unique<Impl>(cfg)), values_(cfg.num_channels, Complex{0.0, 0.0})
{
    setTraits(kIsStateful, kHasDirectFeedthrough);
    for (auto& v : values_) {
        addOutput<Complex>(v);
    }
}

ComplexNetSource::~ComplexNetSource() = default;

void ComplexNetSource::tick()
{
    WsFrameRequest request;
    request.op = "pull";
    const auto response = WsFrameResponse::fromJson(
        impl_->handle_->client->transact(request.toJson()),
        "s7netio websocket pull failed");
    if (!response.ok) {
        throw std::runtime_error(response.error.value_or("s7netio websocket pull failed"));
    }

    if (response.empty.value_or(false)) {
        ++underruns_;
        if (impl_->cfg_.zero_on_underrun) {
            std::fill(values_.begin(), values_.end(), Complex{0.0, 0.0});
        }
        return;
    }

    auto sample = response.sample.value_or(std::vector<double>{});
    if (sample.size() != values_.size() * 2u) {
        throw std::runtime_error("s7netio: received complex sample width mismatch");
    }

    for (std::size_t i = 0; i < values_.size(); ++i) {
        values_[i] = Complex{sample[2u * i], sample[2u * i + 1u]};
    }
    ++pulled_samples_;
}

} // namespace s7


