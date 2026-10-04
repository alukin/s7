#include "s7/net.hpp"

#include <mutex>
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
    metadata.parameters.push_back({
        "topic",
        "string",
        "Logical network stream/topic name.",
        type_name == "NetSink" ? boost::json::value("net.out") : boost::json::value("net.in"),
        std::nullopt,
        std::nullopt,
        {},
        true,
    });

    for (int i = 0; i < input_count; ++i) {
        PortSpec port;
        port.name = i == 0 ? "in" : "in" + std::to_string(i);
        port.description = "Networked scalar input.";
        port.type = SignalType::DOUBLE;
        port.optional = false;
        metadata.input_ports.push_back(std::move(port));
    }

    for (int i = 0; i < output_count; ++i) {
        PortSpec port;
        port.name = i == 0 ? "out" : "out" + std::to_string(i);
        port.description = "Networked scalar output.";
        port.type = SignalType::DOUBLE;
        port.optional = false;
        metadata.output_ports.push_back(std::move(port));
    }

    return metadata;
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

NetSink::NetSink(std::string topic)
    : Element("NetSink")
    , topic_(std::move(topic))
    , endpoint_(nullptr)
    , endpoint_handle_{}
    , sequence_(0)
{
    register_input(0, port_names::in, SignalType::DOUBLE, nullptr);
}

NetSink::~NetSink()
{
    if (endpoint_ && endpoint_handle_.valid()) {
        endpoint_->close_endpoint(endpoint_handle_);
    }
}

void NetSink::ensure_endpoint()
{
    if (endpoint_ && endpoint_handle_.valid()) {
        return;
    }

    if (!endpoint_) {
        endpoint_ = RuntimeNetContext::endpoint();
    }

    if (topic_.empty()) {
        throw std::invalid_argument("NetSink topic must not be empty.");
    }

    if (!endpoint_) {
        return;
    }

    const auto open_result = endpoint_->open_endpoint({
        .endpoint_name = "NetSink:" + topic_,
        .stream_name = topic_,
        .allow_publish = true,
        .allow_receive = false,
    });
    if (!open_result.ok) {
        throw std::runtime_error("NetSink failed to open endpoint for topic '" + topic_ + "': " + open_result.error);
    }

    endpoint_handle_ = open_result.handle;
}

void NetSink::step() {
    const auto value = read_input<RealValue>(0);
    ensure_endpoint();

    if (!endpoint_ || !endpoint_handle_.valid()) {
        return;
    }

    transport::DataPacket packet;
    packet.stream_name = topic_;
    packet.sequence = sequence_++;
    packet.data_type = transport::kPrototypeDataTypeF64;
    packet.elem_count = 1;
    // Placeholder model time: one second per packet, starting at zero (see the
    // review item on observation time semantics).
    packet.t_model_ns = packet.sequence * 1'000'000'000ULL;
    packet.payload = transport::encode_f64_payload(value);

    const auto send_result = endpoint_->publish(endpoint_handle_, packet);
    if (!send_result.ok) {
        throw std::runtime_error("NetSink failed to publish packet for topic '" + topic_ + "': " + send_result.error);
    }
    if (!send_result.delivered) {
        ++dropped_count_;
    }
}

const std::string& NetSink::topic() const {
    return topic_;
}

std::uint64_t NetSink::dropped_count() const {
    return dropped_count_;
}

NetSource::NetSource(std::string topic)
    : Element("NetSource")
    , topic_(std::move(topic))
    , endpoint_(nullptr)
    , endpoint_handle_{}
    , output_value_(0.0)
{
    register_output(0, port_names::out, SignalType::DOUBLE, &output_value_);
}

NetSource::~NetSource()
{
    if (endpoint_ && endpoint_handle_.valid()) {
        endpoint_->close_endpoint(endpoint_handle_);
    }
}

void NetSource::ensure_endpoint()
{
    if (endpoint_ && endpoint_handle_.valid()) {
        return;
    }

    if (!endpoint_) {
        endpoint_ = RuntimeNetContext::endpoint();
    }

    if (topic_.empty()) {
        throw std::invalid_argument("NetSource topic must not be empty.");
    }

    if (!endpoint_) {
        return;
    }

    const auto open_result = endpoint_->open_endpoint({
        .endpoint_name = "NetSource:" + topic_,
        .stream_name = topic_,
        .allow_publish = false,
        .allow_receive = true,
    });
    if (!open_result.ok) {
        throw std::runtime_error("NetSource failed to open endpoint for topic '" + topic_ + "': " + open_result.error);
    }

    endpoint_handle_ = open_result.handle;
}

void NetSource::step()
{
    ensure_endpoint();
    if (!endpoint_ || !endpoint_handle_.valid()) {
        return;
    }

    transport::DataPacket packet;
    const auto receive_result = endpoint_->receive(endpoint_handle_, packet);
    if (!receive_result.ok) {
        throw std::runtime_error("NetSource failed to receive packet for topic '" + topic_ + "': " + receive_result.error);
    }
    if (!receive_result.has_packet) {
        return;
    }

    output_value_ = transport::decode_f64_payload(packet.payload);
}

const std::string& NetSource::topic() const
{
    return topic_;
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
                const auto topic = ctx.has_param("topic") ? ctx.get_string("topic") : std::string{"net.out"};
                return std::make_unique<NetSink>(topic);
            },
            make_net_metadata("NetSink", "Net Sink", "Publish scalar samples to a runtime transport stream.", 1, 0)));
    }

    if (registry.find("NetSource") == nullptr) {
        registry.register_element(ElementDescriptor(
            "NetSource",
            [](const ElementCreationContext& ctx) {
                const auto topic = ctx.has_param("topic") ? ctx.get_string("topic") : std::string{"net.in"};
                return std::make_unique<NetSource>(topic);
            },
            make_net_metadata("NetSource", "Net Source", "Receive scalar samples from a runtime transport stream.", 0, 1)));
    }
}

} // namespace s7
