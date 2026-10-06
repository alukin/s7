#ifndef S7NET_INCLUDE_S7_NET_HPP
#define S7NET_INCLUDE_S7_NET_HPP

#include <string>
#include <cstdint>
#include <memory>
#include <vector>

#include "s7/core.hpp"
#include "s7/transport.hpp"

namespace s7 {

struct NodeEndpoint {
    std::string node_id;
    std::string address;
};

struct NetworkBoundary {
    std::string schema_name;
    std::string element_id;
    std::string direction;
};

class NetTopology {
public:
    void add_node(NodeEndpoint endpoint);
    void add_boundary(NetworkBoundary boundary);

    [[nodiscard]] const std::vector<NodeEndpoint>& nodes() const;
    [[nodiscard]] const std::vector<NetworkBoundary>& boundaries() const;

private:
    std::vector<NodeEndpoint> nodes_;
    std::vector<NetworkBoundary> boundaries_;
};

// Network boundary elements: one end of a lossless typed stream
// (S7_Network_Wire_Protocol_v1.md). The stream is opened on the runtime's
// endpoint (RuntimeNetContext) at the first step. A stream fault (send log
// overflow, underrun, gap, ...) makes step() throw: the node stops stepping
// rather than run on missing or invented values.
class NetStreamElement : public Element {
public:
    ~NetStreamElement() override;

    [[nodiscard]] const stream::StreamConfig& stream_config() const { return config_; }

protected:
    NetStreamElement(std::string_view type_name, stream::StreamConfig config, transport::StreamRole role);

    // Opens the stream if needed; false when no endpoint is configured.
    bool ensure_stream();
    [[noreturn]] void raise_fault(stream::FaultCode code) const;
    // A NetSink stamps explicit timestamps itself: PROCESSING_TIME from the
    // system clock (UTC, ns since the Unix epoch). MODEL_TIME needs a sample
    // rate and SOURCE_TIME a source that knows it; both throw here.
    static void check_stampable(const stream::StreamConfig& config);
    [[nodiscard]] static std::vector<wire::TimestampRecord> stamp(const stream::StreamConfig& config);

    stream::StreamConfig config_;
    transport::StreamRole role_;
    transport::RuntimeNetEndpointPtr endpoint_;
    transport::StreamHandle handle_{};
};

/// NetSink: publishes its input to the stream. Any type.
template <SignalType Type>
class NetSinkT final : public NetStreamElement {
public:
    using value_type = signal_storage_t<Type>;

    explicit NetSinkT(stream::StreamConfig config)
        : NetStreamElement("NetSink", with_type(std::move(config)), transport::StreamRole::Producer)
    {
        register_input(0, port_names::in, Type, nullptr);
        check_stampable(config_);
    }

    void step() override
    {
        const auto value = read_input<value_type>(0);
        if (!ensure_stream()) {
            return; // no network endpoint: a local run without peers
        }
        if (config_.explicit_timestamps()) {
            const auto times = stamp(config_);
            const auto result = endpoint_->publish(handle_, &value, 1, times);
            if (result.fault != stream::FaultCode::None) {
                raise_fault(result.fault);
            }
            return;
        }
        const auto result = endpoint_->publish(handle_, &value, 1);
        if (result.fault != stream::FaultCode::None) {
            raise_fault(result.fault);
        }
    }

private:
    static stream::StreamConfig with_type(stream::StreamConfig config)
    {
        config.type = Type;
        return config;
    }
};

/// NetSource: outputs the next value of the stream, waiting for it (up to
/// max_wait). Any type.
template <SignalType Type>
class NetSourceT final : public NetStreamElement {
public:
    using value_type = signal_storage_t<Type>;

    explicit NetSourceT(stream::StreamConfig config)
        : NetStreamElement("NetSource", with_type(std::move(config)), transport::StreamRole::Consumer)
    {
        register_output(0, port_names::out, Type, &output_value_);
    }

    void step() override
    {
        if (!ensure_stream()) {
            return;
        }
        auto result = endpoint_->receive(handle_, &output_value_, 1);
        if (result.fault != stream::FaultCode::None) {
            raise_fault(result.fault);
        }
        last_timestamps_ = std::move(result.timestamps);
    }

    // The times of the value output by the last step (empty: unknown).
    [[nodiscard]] const std::vector<wire::TimestampRecord>& last_timestamps() const { return last_timestamps_; }

private:
    static stream::StreamConfig with_type(stream::StreamConfig config)
    {
        config.type = Type;
        return config;
    }

    value_type output_value_{};
    std::vector<wire::TimestampRecord> last_timestamps_;
};

using NetSink = NetSinkT<SignalType::F64>;
using NetSource = NetSourceT<SignalType::F64>;

// A stream configuration for a stream id (UUID text) with default limits.
[[nodiscard]] stream::StreamConfig net_stream_config(const std::string& stream_id);

class RuntimeNetContext {
public:
    static void set_endpoint(transport::RuntimeNetEndpointPtr endpoint);
    [[nodiscard]] static auto endpoint() -> transport::RuntimeNetEndpointPtr;
};

void register_net_elements(ElementRegistry& registry);

} // namespace s7

#endif // S7NET_INCLUDE_S7_NET_HPP
