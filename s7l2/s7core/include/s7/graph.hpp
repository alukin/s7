#ifndef S7CORE_INCLUDE_S7_GRAPH_HPP
#define S7CORE_INCLUDE_S7_GRAPH_HPP

#include "s7/element.hpp"

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace s7 {


struct ExecutableGraph {
    std::string schema_name;
    std::string runtime_instance_uuid;
    std::string schema_instance_uuid;
    std::unordered_map<std::string, std::unique_ptr<Element>> elements;
    std::unordered_map<std::string, std::string> instance_uuid_to_id;
    std::unordered_map<std::string, std::string> id_to_instance_uuid;
    std::vector<std::string> causal_order;
    std::vector<std::string> memory_order;
};

} // namespace s7

#endif // S7CORE_INCLUDE_S7_GRAPH_HPP
