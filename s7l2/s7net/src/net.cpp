// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "s7/net.hpp"
#include "s7/metadata_json.hpp"
#include "net_metadata.hpp"

#include <algorithm>
#include <chrono>
#include <mutex>
#include <string_view>
#include <stdexcept>
#include <utility>

namespace s7 {
namespace {

std::mutex g_runtime_endpoint_mutex;
transport::RuntimeNetEndpointPtr g_runtime_endpoint;

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
    if (const auto* node = ctx.params.if_contains("producer_node"); node && node->is_string()) {
        config.producer_node = std::string(node->as_string());
    }
    if (const auto* latency = ctx.params.if_contains("max_latency_ms"); latency && !latency->is_null()) {
        config.max_latency = std::chrono::milliseconds(latency->to_number<std::int64_t>());
    }
    if (const auto* rate = ctx.params.if_contains("sample_rate"); rate && !rate->is_null()) {
        const auto& pair = rate->as_array();
        const auto positive_int = [](const boost::json::value& value) {
            return (value.is_int64() && value.as_int64() > 0) || (value.is_uint64() && value.as_uint64() > 0);
        };
        if (pair.size() != 2 || !positive_int(pair[0]) || !positive_int(pair[1])) {
            throw std::invalid_argument(ctx.metadata.type_name + " parameter 'sample_rate' must be [numerator, denominator], both positive.");
        }
        config.sample_rate = stream::SampleRate{pair[0].to_number<std::uint64_t>(), pair[1].to_number<std::uint64_t>()};
    }
    if (const auto* roles = ctx.params.if_contains("timestamps"); roles && !roles->is_null()) {
        for (const auto& role : roles->as_array()) {
            if (!role.is_string()) {
                throw std::invalid_argument(ctx.metadata.type_name + " parameter 'timestamps' must list role names.");
            }
            config.timestamp_roles.push_back(wire::parse_timestamp_role(std::string(role.as_string())));
        }
        std::ranges::sort(config.timestamp_roles);
        if (std::ranges::adjacent_find(config.timestamp_roles) != config.timestamp_roles.end()) {
            throw std::invalid_argument(ctx.metadata.type_name + " parameter 'timestamps' repeats a role.");
        }
    }
    if (const auto* nodes = ctx.params.if_contains("consumer_nodes"); nodes && !nodes->is_null()) {
        for (const auto& node : nodes->as_array()) {
            if (!node.is_string()) {
                throw std::invalid_argument(ctx.metadata.type_name + " parameter 'consumer_nodes' must list node UUIDs.");
            }
            config.consumer_nodes.emplace_back(node.as_string());
        }
    }
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

void NetStreamElement::check_stampable(const stream::StreamConfig& config)
{
    if (!config.explicit_timestamps()) {
        return;
    }
    for (const auto role : config.timestamp_roles) {
        if (role != wire::TimestampRole::ProcessingTime) {
            throw std::invalid_argument("NetSink stream " + wire::to_string(config.stream_id) + ": " + wire::to_string(role) +
                                        " without a sample_rate is not supported (a NetSink stamps PROCESSING_TIME only).");
        }
    }
}

std::vector<wire::TimestampRecord> NetStreamElement::stamp(const stream::StreamConfig& config)
{
    const auto now = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    std::vector<wire::TimestampRecord> times;
    for (const auto role : config.timestamp_roles) {
        times.push_back(wire::TimestampRecord{role, static_cast<std::int64_t>(now)});
    }
    return times;
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
    static const ElementMetadataLibrary metadata(resources::net_metadata);
    if (registry.find("NetSink") == nullptr) {
        registry.register_element(ElementDescriptor(
            "NetSink",
            [](const ElementCreationContext& ctx) {
                const auto config = config_from_params(ctx);
                return visit_signal_type(ctx.type(), [&]<SignalType T>() -> std::unique_ptr<Element> {
                    return std::make_unique<NetSinkT<T>>(config);
                });
            },
            metadata.at("NetSink")));
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
            metadata.at("NetSource")));
    }
}

} // namespace s7
