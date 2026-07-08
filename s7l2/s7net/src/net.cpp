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

} // namespace s7

