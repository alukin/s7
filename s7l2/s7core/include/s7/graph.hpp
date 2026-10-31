// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: LGPL-3.0-or-later

#ifndef S7CORE_INCLUDE_S7_GRAPH_HPP
#define S7CORE_INCLUDE_S7_GRAPH_HPP

#include "s7/element.hpp"
#include "s7/rate.hpp"

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace s7 {

class RateChangingElement;

// The elements of one rate domain (ADR-022), in the same orders as the whole
// graph's execution records, plus the Resamplers whose input it feeds.
struct RateDomain {
    Rate rate;
    std::vector<Element*> causal_execution;
    std::vector<Element*> memory_execution;
    std::vector<Element*> trapezoid_rhs_execution;
    std::vector<RateChangingElement*> captures;
};


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
    // Rate inference result: the rate of every element, declared, seeded or
    // inferred (doc/S7_Sample_Rate_Model.md). Model time must agree with it.
    std::unordered_map<std::string, Rate> rates;
    // One domain per distinct rate, in step order: a Resampler's input domain
    // comes before its output domain. A single-rate graph has one.
    std::vector<RateDomain> domains;
    // Rate-changing links between domains (indices into domains).
    std::vector<std::pair<std::size_t, std::size_t>> domain_links;
};

} // namespace s7

#endif // S7CORE_INCLUDE_S7_GRAPH_HPP
