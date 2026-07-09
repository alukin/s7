#include "s7/net.hpp"

#include <utility>

namespace s7 {

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
    : Element("NetSink"), topic_(std::move(topic))
{
    register_input(0, port_names::in, SignalType::DOUBLE, nullptr);
}

void NetSink::step() {
    (void)read_input<RealValue>(0);
}

const std::string& NetSink::topic() const {
    return topic_;
}

} // namespace s7
