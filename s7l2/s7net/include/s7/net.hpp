#pragma once

#include <string>
#include <vector>

#include "s7/core.hpp"

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

} // namespace s7

