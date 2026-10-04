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

class NetSink final : public Element {
public:
    explicit NetSink(std::string topic = {});
    ~NetSink() override;

    void step() override;

    [[nodiscard]] const std::string& topic() const;
    // Packets that had no receiver when published.
    [[nodiscard]] std::uint64_t dropped_count() const;

private:
    void ensure_endpoint();

    std::string topic_;
    transport::RuntimeNetEndpointPtr endpoint_;
    transport::EndpointHandle endpoint_handle_{};
    std::uint64_t sequence_{};
    std::uint64_t dropped_count_{};
};

class NetSource final : public Element {
public:
    explicit NetSource(std::string topic = {});
    ~NetSource() override;

    void step() override;

    [[nodiscard]] const std::string& topic() const;

private:
    void ensure_endpoint();

    std::string topic_;
    transport::RuntimeNetEndpointPtr endpoint_;
    transport::EndpointHandle endpoint_handle_{};
    RealValue output_value_{};
};

class RuntimeNetContext {
public:
    static void set_endpoint(transport::RuntimeNetEndpointPtr endpoint);
    [[nodiscard]] static auto endpoint() -> transport::RuntimeNetEndpointPtr;
};

void register_net_elements(ElementRegistry& registry);

} // namespace s7

#endif // S7NET_INCLUDE_S7_NET_HPP
