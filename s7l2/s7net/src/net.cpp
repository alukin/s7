#include "s7/net.hpp"

#include <chrono>
#include <mutex>
#include <string_view>
#include <stdexcept>
#include <utility>

namespace s7 {
namespace {

std::mutex g_runtime_endpoint_mutex;
transport::RuntimeNetEndpointPtr g_runtime_endpoint;

auto make_net_metadata(const std::string& type_name,
                       const std::string& display_name,
                       const std::string& description,
                       const int input_count,
                       const int output_count) -> ElementMetadata
{
    ElementMetadata metadata;
    metadata.type_name = type_name;
    metadata.display_name = display_name;
    metadata.description = description;
    metadata.category = "network";
    metadata.version = "1.0.0";
    metadata.tags = {"network", "transport", "distributed"};
    metadata.visual = {"box", "#5C6BC0", 140, 56, ""};
    metadata.type_variables.push_back({"T", signal_type_sets::all.types(), "type"});
    metadata.parameters = {
        {"type", "type", "Pins the stream's type (otherwise inferred)", nullptr, std::nullopt, std::nullopt, {}, false},
        {"stream_id", "string", "Stream UUID; the same on both ends", nullptr, std::nullopt, std::nullopt, {}, true},
        {"max_elements_per_packet", "int", "Batch size", 1024, 1, std::nullopt, {}, false},
        {"max_batch_latency_ms", "int", "Longest wait before a partial batch is sent", 10, 0, std::nullopt, {}, false},
        {"send_log_max_packets", "int", "Unacknowledged packets kept for replay", 4096, 1, std::nullopt, {}, false},
        {"max_block_ms", "int", "Longest publish wait on a full send log before the stream faults", 2000, 0, std::nullopt, {}, false},
        {"max_wait_ms", "int", "Longest receive wait for data before the stream faults (underrun)", 2000, 0, std::nullopt, {}, false},
    };
    for (int i = 0; i < input_count; ++i) {
        metadata.input_ports.push_back({i == 0 ? "in" : "in" + std::to_string(i), "Value to publish", SignalType::F64, false, "T"});
    }
    for (int i = 0; i < output_count; ++i) {
        metadata.output_ports.push_back({i == 0 ? "out" : "out" + std::to_string(i), "Received value", SignalType::F64, false, "T"});
    }
    return metadata;
}

stream::StreamConfig config_from_params(const ElementCreationContext& ctx)
{
    if (!ctx.has_param("stream_id")) {
        throw std::invalid_argument(ctx.metadata.type_name + " needs a 'stream_id' (UUID).");
    }
    auto config = net_stream_config(ctx.get_string("stream_id"));
    config.type = ctx.type();
    auto positive = [&](const char* name, const std::int64_t fallback) {
        const auto value = ctx.has_param(name) ? ctx.get_int(name) : fallback;
        if (value < 0 || (value == 0 && std::string_view(name) != "max_batch_latency_ms" &&
                          std::string_view(name) != "max_block_ms" && std::string_view(name) != "max_wait_ms")) {
            throw std::invalid_argument(ctx.metadata.type_name + " parameter '" + name + "' is out of range.");
        }
        return value;
    };
    config.max_elements_per_packet = static_cast<std::uint32_t>(positive("max_elements_per_packet", config.max_elements_per_packet));
    config.max_batch_latency = std::chrono::milliseconds(positive("max_batch_latency_ms", config.max_batch_latency.count()));
    config.send_log_max_packets = static_cast<std::uint32_t>(positive("send_log_max_packets", config.send_log_max_packets));
    config.max_block = std::chrono::milliseconds(positive("max_block_ms", config.max_block.count()));
    config.max_wait = std::chrono::milliseconds(positive("max_wait_ms", config.max_wait.count()));
    return config;
}

} // namespace

void NetTopology::add_node(NodeEndpoint endpoint) {
    nodes_.push_back(std::move(endpoint));
}

void NetTopology::add_boundary(NetworkBoundary boundary) {
    boundaries_.push_back(std::move(boundary));
}

const std::vector<NodeEndpoint>& NetTopology::nodes() const {
    return nodes_;
}

const std::vector<NetworkBoundary>& NetTopology::boundaries() const {
    return boundaries_;
}

stream::StreamConfig net_stream_config(const std::string& stream_id)
{
    stream::StreamConfig config;
    config.stream_id = wire::parse_stream_id(stream_id);
    return config;
}

NetStreamElement::NetStreamElement(const std::string_view type_name, stream::StreamConfig config, const transport::StreamRole role)
    : Element(type_name, true)
    , config_(std::move(config))
    , role_(role)
{
}

NetStreamElement::~NetStreamElement()
{
    if (endpoint_ && handle_.valid()) {
        endpoint_->close_stream(handle_);
    }
}

bool NetStreamElement::ensure_stream()
{
    if (handle_.valid()) {
        return true;
    }
    if (!endpoint_) {
        endpoint_ = RuntimeNetContext::endpoint();
    }
    if (!endpoint_) {
        return false;
    }
    handle_ = role_ == transport::StreamRole::Producer ? endpoint_->open_producer(config_) : endpoint_->open_consumer(config_);
    return true;
}

void NetStreamElement::raise_fault(const stream::FaultCode code) const
{
    throw std::runtime_error(std::string(type_name()) + " stream " + wire::to_string(config_.stream_id) + " faulted: " +
                             stream::to_string(code) + ".");
}

void RuntimeNetContext::set_endpoint(transport::RuntimeNetEndpointPtr endpoint)
{
    std::lock_guard<std::mutex> lock(g_runtime_endpoint_mutex);
    g_runtime_endpoint = std::move(endpoint);
}

auto RuntimeNetContext::endpoint() -> transport::RuntimeNetEndpointPtr
{
    std::lock_guard<std::mutex> lock(g_runtime_endpoint_mutex);
    return g_runtime_endpoint;
}

void register_net_elements(ElementRegistry& registry)
{
    if (registry.find("NetSink") == nullptr) {
        registry.register_element(ElementDescriptor(
            "NetSink",
            [](const ElementCreationContext& ctx) {
                const auto config = config_from_params(ctx);
                return visit_signal_type(ctx.type(), [&]<SignalType T>() -> std::unique_ptr<Element> {
                    return std::make_unique<NetSinkT<T>>(config);
                });
            },
            make_net_metadata("NetSink", "Net Sink", "Publishes its input to a lossless network stream.", 1, 0)));
    }

    if (registry.find("NetSource") == nullptr) {
        registry.register_element(ElementDescriptor(
            "NetSource",
            [](const ElementCreationContext& ctx) {
                const auto config = config_from_params(ctx);
                return visit_signal_type(ctx.type(), [&]<SignalType T>() -> std::unique_ptr<Element> {
                    return std::make_unique<NetSourceT<T>>(config);
                });
            },
            make_net_metadata("NetSource", "Net Source", "Outputs the values of a lossless network stream.", 0, 1)));
    }
}

} // namespace s7
