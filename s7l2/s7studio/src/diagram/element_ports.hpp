#pragma once

#include <boost/json.hpp>

#include <QString>

#include <string_view>
#include <vector>

namespace s7studio::diagram {

struct ElementPorts {
    std::vector<QString> inputs;  // port names, by index
    std::vector<QString> outputs;
};

// The ports an element shows: from its nested schema's exposed ports, the
// element registry's metadata, and the params that size variadic elements
// (Adder `weights`, Multiply `factors`, Scope `channel_count`).
[[nodiscard]] auto elementPorts(std::string_view type, const boost::json::object& params, const boost::json::value& nestedSchema)
    -> ElementPorts;

} // namespace s7studio::diagram
