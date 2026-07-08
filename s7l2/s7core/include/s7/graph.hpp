#pragma once

#include "s7/element.hpp"

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace s7 {


struct ExecutableGraph {
    std::string schema_name;
    std::unordered_map<std::string, std::unique_ptr<Element>> elements;
    std::vector<std::string> causal_order;
    std::vector<std::string> memory_order;
};

} // namespace s7

