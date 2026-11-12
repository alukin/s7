// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: LGPL-3.0-or-later
#include <s7/net_frames.hpp>
#include <s7/metadata_json.hpp>
#include "net_metadata.hpp"
#include <cstring>

namespace s7 {
namespace {
transport::FrameStreamConfig config_from_params(const ElementCreationContext& ctx) {
    transport::FrameStreamConfig config;
    auto& d = config.descriptor;
    d.stream_id = wire::parse_stream_id(ctx.get_string("stream_id"));
    d.type = ctx.type(); d.channels.clear();
    for (const auto& channel : ctx.params.at("channels").as_array()) {
        if (!channel.is_string()) throw std::invalid_argument("channels must contain unique nonempty names");
        d.channels.emplace_back(channel.as_string());
    }
    const auto& rate = ctx.params.at("frame_rate").as_array();
    auto positive = [](const boost::json::value& v) -> std::uint64_t {
        if (v.is_uint64() && v.as_uint64()) return v.as_uint64();
        if (v.is_int64() && v.as_int64() > 0) return v.as_int64();
        throw std::invalid_argument("frame_rate must be two positive integers [numerator, denominator]");
    };
    if (rate.size() != 2) throw std::invalid_argument("frame_rate must be [numerator, denominator]");
    d.frame_rate = {positive(rate[0]), positive(rate[1])};
    d.max_frames_per_packet = ctx.get_int("max_frames_per_packet");
    d.window_packets = ctx.get_int("window_packets");
    d.window_frames = std::uint64_t(d.window_packets) * d.max_frames_per_packet;
    const auto encoding = ctx.get_string("encoding");
    d.encoding = encoding == "lz4" ? frames::Encoding::Lz4 : encoding == "zstd" ? frames::Encoding::Zstd : frames::Encoding::Raw;
    config.max_batch_latency = std::chrono::milliseconds(ctx.get_int("max_batch_latency_ms"));
    config.max_block = std::chrono::milliseconds(ctx.get_int("max_block_ms"));
    config.max_wait = std::chrono::milliseconds(ctx.get_int("max_wait_ms"));
    if (ctx.has_param("producer_node")) config.producer_node = ctx.get_string("producer_node");
    if (ctx.has_param("consumer_nodes")) for (const auto& node : ctx.params.at("consumer_nodes").as_array()) {
        if (!node.is_string()) throw std::invalid_argument("consumer_nodes must contain node UUIDs");
        static_cast<void>(wire::parse_stream_id(std::string(node.as_string())));
        config.consumer_nodes.emplace_back(node.as_string());
    }
    if (!config.producer_node.empty()) static_cast<void>(wire::parse_stream_id(config.producer_node));
    d.validate();
    return config;
}
}

NetFrameElement::NetFrameElement(std::string_view name, transport::FrameStreamConfig config, transport::StreamRole role)
    : Element(name, true), config_(std::move(config)), role_(role)
{
    config_.descriptor.validate();
    visit_signal_type(config_.descriptor.type, [&]<SignalType T>() {
        using Value = signal_storage_t<T>;
        storage_ = std::shared_ptr<void>(new Value[config_.descriptor.channels.size()]{},
            [](void* values) { delete[] static_cast<Value*>(values); });
    });
}
NetFrameElement::~NetFrameElement() {
    if (endpoint_ && handle_.valid()) endpoint_->close_stream(handle_);
}
std::optional<Rate> NetFrameElement::declared_rate() const {
    const auto& rate = config_.descriptor.frame_rate;
    return Rate::reduced(rate.numerator, rate.denominator);
}
void NetFrameElement::ensure_stream() {
    if (failed_) throw std::runtime_error(std::string(type_name()) + " boundary fault is latched; create a new graph and stream_id before resuming");
    if (handle_.valid()) return;
    endpoint_ = RuntimeNetContext::endpoint();
    if (!endpoint_) throw std::runtime_error(std::string(type_name()) + " requires a runtime network endpoint");
    handle_ = role_ == transport::StreamRole::Producer ? endpoint_->open_frame_producer(config_) : endpoint_->open_frame_consumer(config_);
}
void NetFrameElement::on_step_failure() noexcept {
    failed_ = true;
    if (endpoint_ && handle_.valid()) {
        try { endpoint_->fault_frames(handle_); } catch (...) {}
    }
}
void NetFrameElement::fail(frames::Status status) {
    on_step_failure();
    throw std::runtime_error(std::string(type_name()) + " stream " + wire::to_string(config_.descriptor.stream_id) +
        " frame boundary failed: " + frames::to_string(status));
}
NetFrameSink::NetFrameSink(transport::FrameStreamConfig config)
    : NetFrameElement("NetFrameSink", std::move(config), transport::StreamRole::Producer) {
    for (std::size_t i = 0; i < config_.descriptor.channels.size(); ++i)
        register_input(i, config_.descriptor.channels[i], config_.descriptor.type, nullptr);
}
void NetFrameSink::step() {
    ensure_stream();
    const auto stride = signal_type_info(config_.descriptor.type).storage_bytes;
    for (std::size_t i = 0; i < input_count(); ++i)
        std::memcpy(reinterpret_cast<std::uint8_t*>(storage_.get()) + i * stride, input_binding(i), stride);
    const auto result = endpoint_->publish_frames(handle_, storage_.get(), 1);
    if (result.frames != 1 || result.status != frames::Status::Ok) fail(result.status);
}
NetFrameSource::NetFrameSource(transport::FrameStreamConfig config)
    : NetFrameElement("NetFrameSource", std::move(config), transport::StreamRole::Consumer) {
    const auto stride = signal_type_info(config_.descriptor.type).storage_bytes;
    for (std::size_t i = 0; i < config_.descriptor.channels.size(); ++i)
        register_output(i, config_.descriptor.channels[i], config_.descriptor.type,
            reinterpret_cast<std::uint8_t*>(storage_.get()) + i * stride);
}
void NetFrameSource::step() {
    ensure_stream();
    const auto result = endpoint_->receive_frames(handle_, storage_.get(), 1);
    // A valid final frame precedes an ordered terminal; deliver that prefix.
    if (result.frames != 1) fail(result.status);
    last_info_ = result.info;
}
void register_net_frame_elements(ElementRegistry& registry) {
    static const ElementMetadataLibrary metadata(resources::net_metadata);
    for (const std::string name : {"NetFrameSink", "NetFrameSource"}) {
        if (registry.find(name)) continue;
        registry.register_element(ElementDescriptor(name, [name](const ElementCreationContext& ctx) -> std::unique_ptr<Element> {
            auto config = config_from_params(ctx);
            if (name == "NetFrameSink") return std::make_unique<NetFrameSink>(std::move(config));
            return std::make_unique<NetFrameSource>(std::move(config));
        }, metadata.at(name)));
    }
}
} // namespace s7
