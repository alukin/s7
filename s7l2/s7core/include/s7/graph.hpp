// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: LGPL-3.0-or-later

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
    // Stateless derivative dependencies replayed only during an implicit solve.
    std::vector<std::string> trapezoid_rhs;
    // Compiled dispatch records parallel to the ID orders above. The map owns
    // each heap-allocated Element; moving this graph does not move its elements.
    std::vector<Element*> causal_execution;
    std::vector<Element*> memory_execution;
    std::vector<Element*> trapezoid_rhs_execution;
};

} // namespace s7

#endif // S7CORE_INCLUDE_S7_GRAPH_HPP
