// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "s7/core.hpp"
#include "s7/type_inference.hpp"

#include <queue>
#include <sstream>
#include <stdexcept>
#include <unordered_set>
#include <unordered_map>

#include <boost/uuid/name_generator_sha1.hpp>
#include <boost/uuid/random_generator.hpp>
#include <boost/uuid/string_generator.hpp>
#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_io.hpp>

namespace s7 {
namespace {

struct PrototypeInfo {
    std::size_t input_count{};
    std::size_t output_count{};
    bool has_causal_memory{};
};

using PrototypeMap = std::unordered_map<std::string, PrototypeInfo>;

std::string join_errors(const std::vector<std::string>& errors) {
    std::ostringstream builder;
    for (std::size_t i = 0; i < errors.size(); ++i) {
        builder << " - " << errors[i];
        if (i + 1 < errors.size()) {
            builder << '\n';
        }
    }
    return builder.str();
}

std::string validated_or_generated_uuid(const std::string& candidate) {
    if (candidate.empty()) {
        return boost::uuids::to_string(boost::uuids::random_generator()());
    }

    try {
        const boost::uuids::string_generator generator;
        return boost::uuids::to_string(generator(candidate));
    } catch (const std::exception&) {
        throw std::invalid_argument("Invalid UUID string provided to compiler identity metadata.");
    }
}

std::string element_instance_uuid_for(const std::string& schema_instance_uuid,
                                      const std::string& element_id) {
    const boost::uuids::string_generator generator;
    const auto schema_uuid = generator(schema_instance_uuid);
    const boost::uuids::name_generator_sha1 name_generator(schema_uuid);
    return boost::uuids::to_string(name_generator(element_id));
}

class CausalScheduler {
public:
    static std::vector<std::string> schedule(const Schema& schema,
                                             const PrototypeMap& prototypes,
                                             const std::vector<std::string>& causal_nodes) {
        std::unordered_map<std::string, int> indegree;
        std::unordered_map<std::string, std::vector<std::string>> adjacency;

        for (const auto& id : causal_nodes) {
            indegree[id] = 0;
            adjacency[id] = {};
        }

        for (const auto& connection : schema.connections()) {
            const auto& from_info = prototypes.at(connection.from_element);
            const auto& to_info = prototypes.at(connection.to_element);

            if (!from_info.has_causal_memory && !to_info.has_causal_memory) {
                adjacency[connection.from_element].push_back(connection.to_element);
                indegree[connection.to_element] += 1;
            }
        }

        std::queue<std::string> ready;
        for (const auto& id : causal_nodes) {
            if (indegree[id] == 0) {
                ready.push(id);
            }
        }

        std::vector<std::string> order;
        order.reserve(indegree.size());

        while (!ready.empty()) {
            auto id = ready.front();
            ready.pop();
            order.push_back(id);

            for (const auto& next : adjacency[id]) {
                indegree[next] -= 1;
                if (indegree[next] == 0) {
                    ready.push(next);
                }
            }
        }

        if (order.size() != causal_nodes.size()) {
            throw std::runtime_error("Compiler failed to produce a causal schedule for stateless elements.");
        }

        return order;
    }
};

} // namespace

ExecutableGraph Compiler::compile(const Schema& schema) {
    return compile(schema, {}, {});
}

ExecutableGraph Compiler::compile(const Schema& schema,
                                  std::string runtime_instance_uuid,
                                  std::string schema_instance_uuid) {
    // Validation creates every element once; the graph is built from them.
    auto checked = detail::check_schema(schema);
    if (!checked.errors.empty()) {
        throw std::invalid_argument("Schema validation failed:\n" + join_errors(checked.errors));
    }

    ExecutableGraph executable_graph;
    executable_graph.schema_name = schema.name();
    executable_graph.runtime_instance_uuid = validated_or_generated_uuid(runtime_instance_uuid);
    executable_graph.schema_instance_uuid = validated_or_generated_uuid(schema_instance_uuid);

    PrototypeMap prototypes;
    for (std::size_t index = 0; index < schema.elements().size(); ++index) {
        const auto& spec = schema.elements()[index];
        auto element = std::move(checked.elements[index]);
        const auto element_instance_uuid = element_instance_uuid_for(executable_graph.schema_instance_uuid, spec.id);
        if (executable_graph.instance_uuid_to_id.contains(element_instance_uuid)) {
            throw std::invalid_argument("Duplicate generated element instance UUID for element id: '" + spec.id + "'.");
        }
        prototypes.emplace(spec.id,
                           PrototypeInfo{element->input_count(),
                                         element->output_count(),
                                         element->has_causal_memory()});
        executable_graph.instance_uuid_to_id.emplace(element_instance_uuid, spec.id);
        executable_graph.id_to_instance_uuid.emplace(spec.id, element_instance_uuid);
        executable_graph.elements.emplace(spec.id, std::move(element));
    }

    for (const auto& spec : schema.elements()) {
        const auto& info = prototypes.at(spec.id);
        if (info.has_causal_memory) {
            executable_graph.memory_order.push_back(spec.id);
        } else {
            executable_graph.causal_order.push_back(spec.id);
        }
    }

    std::unordered_set<std::string> driven_inputs;
    driven_inputs.reserve(schema.connections().size());

    std::unordered_map<std::string, std::vector<InputBinding>> element_inputs;
    for (const auto& spec : schema.elements()) {
        const auto& info = prototypes.at(spec.id);
        element_inputs.emplace(spec.id, std::vector<InputBinding>(info.input_count));
    }

    for (const auto& connection : schema.connections()) {
        const auto source = executable_graph.elements.at(connection.from_element)->outputs();
        if (connection.from_port >= source.size()) {
            throw std::runtime_error("Connection from '" + connection.from_element + "' uses invalid output port " +
                                     std::to_string(connection.from_port) + ".");
        }

        const auto key = connection.to_element + ":" + std::to_string(connection.to_port);
        if (!driven_inputs.insert(key).second) {
            throw std::runtime_error("Input port " + std::to_string(connection.to_port) + " of '" +
                                     connection.to_element + "' has multiple drivers.");
        }

        element_inputs[connection.to_element][connection.to_port] = source[connection.from_port];
    }

    // Validation gave every element its resolved rate (adopt_rate) before
    // binding: a nested schema compiles on binding and checks against it.

    for (auto& [id, bindings] : element_inputs) {
        for (std::size_t port = 0; port < bindings.size(); ++port) {
            if (bindings[port] == nullptr) {
                throw std::runtime_error("Compiler produced an unresolved input binding for element '" + id +
                                         "' at input port " + std::to_string(port) + ".");
            }
        }

        executable_graph.elements.at(id)->bind_inputs(bindings);
    }

    executable_graph.causal_order =
        CausalScheduler::schedule(schema, prototypes, executable_graph.causal_order);
    const std::unordered_set<std::string> trapezoid_rhs(checked.trapezoid_rhs.begin(), checked.trapezoid_rhs.end());
    for (const auto& id : executable_graph.causal_order) {
        if (trapezoid_rhs.contains(id)) executable_graph.trapezoid_rhs.push_back(id);
    }
    const auto resolve = [&elements = executable_graph.elements](const std::vector<std::string>& order,
                                                                  std::vector<Element*>& execution) {
        execution.reserve(order.size());
        for (const auto& id : order) execution.push_back(elements.at(id).get());
    };
    resolve(executable_graph.causal_order, executable_graph.causal_execution);
    resolve(executable_graph.memory_order, executable_graph.memory_execution);
    resolve(executable_graph.trapezoid_rhs, executable_graph.trapezoid_rhs_execution);
    // Split the execution records by rate domain, keeping their order.
    for (const auto& rate : checked.domain_rates) {
        RateDomain domain;
        domain.rate = rate;
        const auto split = [&](const std::vector<std::string>& ids, std::vector<Element*>& records) {
            for (const auto& id : ids) {
                if (checked.rates.at(id) == rate) records.push_back(executable_graph.elements.at(id).get());
            }
        };
        split(executable_graph.causal_order, domain.causal_execution);
        split(executable_graph.memory_order, domain.memory_execution);
        split(executable_graph.trapezoid_rhs, domain.trapezoid_rhs_execution);
        executable_graph.domains.push_back(std::move(domain));
    }
    for (const auto& link : checked.domain_links) {
        executable_graph.domains[link.input].captures.push_back(
            dynamic_cast<RateChangingElement*>(executable_graph.elements.at(link.resampler).get()));
        executable_graph.domain_links.emplace_back(link.input, link.output);
    }
    executable_graph.rates = std::move(checked.rates);
    for (const auto& checked_region : checked.coupled_regions) {
        ExecutableGraph::CoupledRegion region;
        region.method = checked_region.method;
        region.rate = checked_region.rate;
        region.step_seconds = checked_region.step_seconds;
        region.substeps = checked_region.substeps;
        region.state_ids = checked_region.states;
        region.upstream_state_ids = checked_region.upstream_states;
        for (const auto& id : checked_region.states) {
            auto* element = executable_graph.elements.at(id).get();
            region.states.push_back(static_cast<double*>(element->output_ports()[0].value_ptr));
        }
        for (const auto& edge : checked_region.derivative_connections) {
            const auto* source = executable_graph.elements.at(edge.from_element).get();
            region.derivatives.push_back(static_cast<const double*>(source->output_ports()[edge.from_port].value_ptr));
        }
        const std::unordered_set<std::string> rhs(checked_region.rhs.begin(), checked_region.rhs.end());
        for (const auto& id : executable_graph.causal_order) {
            if (rhs.contains(id)) region.rhs_execution.push_back(executable_graph.elements.at(id).get());
        }
        executable_graph.coupled_regions.push_back(std::move(region));
    }
    return executable_graph;
}

} // namespace s7

