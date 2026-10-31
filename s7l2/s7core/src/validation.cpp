// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "s7/core.hpp"
#include "s7/rate_inference.hpp"
#include "s7/type_inference.hpp"
#include "s7/trapezoid.hpp"

#include <algorithm>
#include <optional>
#include <unordered_set>

namespace s7 {
namespace {

struct PrototypeInfo {
    std::size_t input_count{};
    std::size_t output_count{};
    bool has_causal_memory{};
};

using PrototypeMap = std::unordered_map<std::string, PrototypeInfo>;
using AdjacencyMap = std::unordered_map<std::string, std::vector<std::string>>;

// Elements by id, each created once for the whole check.
using InstanceMap = std::unordered_map<std::string, const Element*>;

PrototypeMap prototypes_of(const InstanceMap& instances) {
    PrototypeMap prototypes;
    for (const auto& [id, element] : instances) {
        prototypes.emplace(id, PrototypeInfo{element->input_count(), element->output_count(), element->has_causal_memory()});
    }
    return prototypes;
}

class GraphAlgorithms {
public:
    static AdjacencyMap build_feedthrough_adjacency(const Schema& schema, const PrototypeMap& prototypes) {
        AdjacencyMap adjacency;

        // Every cycle must cross causal memory. One memory element somewhere
        // in an SCC does not break other, direct-feedthrough cycles in it.
        for (const auto& [id, info] : prototypes) {
            if (!info.has_causal_memory) {
                adjacency[id] = {};
            }
        }

        for (const auto& connection : schema.connections()) {
            if (adjacency.contains(connection.from_element) && adjacency.contains(connection.to_element)) {
                adjacency[connection.from_element].push_back(connection.to_element);
            }
        }

        return adjacency;
    }

    static std::vector<std::vector<std::string>> strongly_connected_components(const AdjacencyMap& adjacency) {
        std::unordered_set<std::string> visited;
        std::vector<std::string> order;
        order.reserve(adjacency.size());

        // Kosaraju with explicit stacks (same visit order as the recursive
        // form), so a long chain of elements cannot exhaust the call stack.
        struct Frame {
            const std::string* node;
            std::size_t next;
        };
        const auto neighbours = [](const AdjacencyMap& graph, const std::string& node) -> const std::vector<std::string>* {
            const auto it = graph.find(node);
            return it != graph.end() ? &it->second : nullptr;
        };
        std::vector<Frame> stack;
        for (const auto& [root, _] : adjacency) {
            if (!visited.insert(root).second) continue;
            stack.push_back({&root, 0});
            while (!stack.empty()) {
                auto& frame = stack.back();
                const auto* next = neighbours(adjacency, *frame.node);
                if (next && frame.next < next->size()) {
                    const auto& child = (*next)[frame.next++];
                    if (visited.insert(child).second) stack.push_back({&child, 0});
                    continue;
                }
                order.push_back(*frame.node);
                stack.pop_back();
            }
        }

        const auto reversed = reverse_graph(adjacency);
        visited.clear();

        std::vector<std::vector<std::string>> components;
        for (auto it = order.rbegin(); it != order.rend(); ++it) {
            if (!visited.insert(*it).second) {
                continue;
            }

            std::vector<std::string> component{*it};
            stack.push_back({&*it, 0});
            while (!stack.empty()) {
                auto& frame = stack.back();
                const auto* next = neighbours(reversed, *frame.node);
                if (next && frame.next < next->size()) {
                    const auto& child = (*next)[frame.next++];
                    if (visited.insert(child).second) {
                        component.push_back(child);
                        stack.push_back({&child, 0});
                    }
                    continue;
                }
                stack.pop_back();
            }
            components.push_back(std::move(component));
        }

        return components;
    }

    static bool has_self_loop(const std::string& id, const AdjacencyMap& adjacency) {
        const auto it = adjacency.find(id);
        if (it == adjacency.end()) {
            return false;
        }

        return std::ranges::find(it->second, id) != it->second.end();
    }

private:
    static AdjacencyMap reverse_graph(const AdjacencyMap& adjacency) {
        AdjacencyMap reversed;

        for (const auto& [node, _] : adjacency) {
            reversed[node] = {};
        }

        for (const auto& [from, targets] : adjacency) {
            for (const auto& to : targets) {
                reversed[to].push_back(from);
            }
        }

        return reversed;
    }
};

class ConnectionRulesValidator {
public:
    static void validate(const Schema& schema,
                         const PrototypeMap& prototypes,
                         const InstanceMap& instances,
                         std::vector<std::string>& errors) {
        std::unordered_map<std::string, std::vector<int>> input_connection_count;
        for (const auto& [id, info] : prototypes) {
            input_connection_count.emplace(id, std::vector<int>(info.input_count, 0));
        }

        for (const auto& connection : schema.connections()) {
            const auto from_it = prototypes.find(connection.from_element);
            if (from_it == prototypes.end()) {
                errors.emplace_back("Connection references unknown source element '" + connection.from_element + "'.");
                continue;
            }

            const auto to_it = prototypes.find(connection.to_element);
            if (to_it == prototypes.end()) {
                errors.emplace_back("Connection references unknown target element '" + connection.to_element + "'.");
                continue;
            }

            if (connection.from_port >= from_it->second.output_count) {
                errors.emplace_back("Connection from '" + connection.from_element + "' uses invalid output port " +
                                    std::to_string(connection.from_port) + ".");
            }

            if (connection.to_port >= to_it->second.input_count) {
                errors.emplace_back("Connection to '" + connection.to_element + "' uses invalid input port " +
                                    std::to_string(connection.to_port) + ".");
                continue;
            }

            validate_port_types(connection, instances, errors);

            input_connection_count[connection.to_element][connection.to_port] += 1;
        }

        for (const auto& [id, counts] : input_connection_count) {
            for (std::size_t port = 0; port < counts.size(); ++port) {
                if (counts[port] == 0) {
                    errors.emplace_back("Input port " + std::to_string(port) + " of '" + id + "' is unconnected.");
                } else if (counts[port] > 1) {
                    errors.emplace_back("Input port " + std::to_string(port) + " of '" + id + "' has multiple drivers.");
                }
            }
        }
    }

private:
    static void validate_port_types(const Connection& connection,
                                    const InstanceMap& instances,
                                    std::vector<std::string>& errors) {
        const auto from_element = instances.find(connection.from_element);
        const auto to_element = instances.find(connection.to_element);
        if (from_element == instances.end() || to_element == instances.end()) return;

        const auto from_outputs = from_element->second->output_ports();
        const auto to_inputs = to_element->second->input_ports();

        if (connection.from_port >= from_outputs.size() || connection.to_port >= to_inputs.size()) {
            return;
        }

        const auto from_type = from_outputs[connection.from_port].type;
        const auto to_type = to_inputs[connection.to_port].type;

        // There is no implicit conversion: a type change is an explicit Convert element.
        if (from_type == to_type) {
            return;
        }


        errors.emplace_back("Port type mismatch: '" + connection.from_element + "' output port " +
                            std::to_string(connection.from_port) + " (type " +
                            std::string{signal_type_name(from_type)} + ") -> '" + connection.to_element +
                            "' input port " + std::to_string(connection.to_port) + " (type " +
                            std::string{signal_type_name(to_type)} + ").");
    }
};

class CycleRulesValidator {
public:
    static void validate(const Schema& schema, const PrototypeMap& prototypes, std::vector<std::string>& errors) {
        const auto adjacency = GraphAlgorithms::build_feedthrough_adjacency(schema, prototypes);
        const auto components = GraphAlgorithms::strongly_connected_components(adjacency);

        for (const auto& component : components) {
            const bool cyclic =
                component.size() > 1 || (component.size() == 1 && GraphAlgorithms::has_self_loop(component.front(), adjacency));
            if (cyclic) {
                errors.emplace_back("Cycle detected without causal memory.");
            }
        }
    }
};

} // namespace

namespace detail {

CheckedSchema check_schema(const Schema& schema) {
    CheckedSchema checked;
    // Generic elements get their types first; the checks below then see
    // concrete elements only.
    auto resolved = TypeResolver::resolve(schema);
    if (!resolved.ok()) {
        checked.errors = std::move(resolved.errors);
        return checked;
    }

    // Every element is created once: the instance type inference already
    // made (concrete elements), or by its resolved factory.
    InstanceMap instances;
    checked.elements.resize(schema.elements().size());
    for (std::size_t index = 0; index < schema.elements().size(); ++index) {
        const auto& spec = schema.elements()[index];
        if (spec.id.empty()) {
            checked.errors.emplace_back("Schema contains an element with an empty id.");
            continue;
        }
        if (instances.contains(spec.id)) {
            checked.errors.emplace_back("Duplicate element id: '" + spec.id + "'.");
            continue;
        }
        try {
            auto element = index < resolved.instances.size() && resolved.instances[index] ? std::move(resolved.instances[index])
                                                                                         : resolved.factories[index]();
            if (!element) {
                throw std::invalid_argument("Element factory for '" + spec.id + "' returned null.");
            }
            instances.emplace(spec.id, element.get());
            checked.elements[index] = std::move(element);
        } catch (const std::exception& ex) {
            checked.errors.emplace_back(ex.what());
        }
    }

    const auto prototypes = prototypes_of(instances);
    ConnectionRulesValidator::validate(schema, prototypes, instances, checked.errors);
    if (checked.errors.empty()) {
        CycleRulesValidator::validate(schema, prototypes, checked.errors);
    }
    if (checked.errors.empty()) {
        auto rates = RateResolver::resolve(schema, instances);
        if (!rates.ok()) {
            checked.errors.insert(checked.errors.end(), rates.errors.begin(), rates.errors.end());
        } else {
            checked.rates = std::move(rates.rates);
            // Each element adopts its resolved rate here, so a parameter with no
            // exact meaning at that rate (a duration that is not a whole number
            // of samples) is a validation error, not a later compile failure.
            for (std::size_t index = 0; index < schema.elements().size(); ++index) {
                const auto& id = schema.elements()[index].id;
                const auto rate = checked.rates.find(id);
                if (rate == checked.rates.end() || !checked.elements[index]) continue;
                try {
                    checked.elements[index]->adopt_rate(rate->second);
                } catch (const std::invalid_argument& error) {
                    checked.errors.push_back("Element '" + id + "' at " + rate->second.to_string() + ": " + error.what());
                }
            }
        }
    }
    if (checked.errors.empty()) {
        // Find only derivative ancestors whose value varies with a trial
        // integrator state. Independent sources/other memory outputs are held
        // fixed for this interval; stateful direct-feedthrough dependencies
        // must never be stepped repeatedly by the numerical solver.
        std::unordered_set<std::string> implicit;
        std::unordered_map<std::string, std::vector<std::string>> parents;
        for (const auto& [id, instance] : instances) {
            const auto* integration = dynamic_cast<const TrapezoidIntegration*>(instance);
            if (integration && integration->implicit_trapezoid_enabled()) implicit.insert(id);
        }
        if (!implicit.empty()) {
            for (const auto& connection : schema.connections()) {
                parents[connection.to_element].push_back(connection.from_element);
            }
            std::unordered_map<std::string, bool> varying;
            std::unordered_set<std::string> visiting;
            // Known without a walk: integrators vary, causal memory and
            // already classified elements do not need revisiting.
            const auto known = [&](const std::string& id) -> std::optional<bool> {
                if (implicit.contains(id)) return true;
                if (instances.at(id)->has_causal_memory()) return false;
                if (const auto it = varying.find(id); it != varying.end()) return it->second;
                return std::nullopt;
            };
            // Post-order walk over derivative parents with an explicit stack,
            // so a long derivative chain cannot exhaust the call stack.
            struct Frame {
                std::string id;
                std::size_t next{};
                bool depends{};
            };
            const auto visit = [&](const std::string& start) {
                if (known(start)) return;
                std::vector<Frame> stack;
                visiting.insert(start);
                stack.push_back({start});
                while (!stack.empty()) {
                    auto& frame = stack.back();
                    const auto& inputs = parents[frame.id];
                    if (frame.next < inputs.size()) {
                        const auto parent = inputs[frame.next++];
                        if (const auto value = known(parent)) {
                            frame.depends = *value || frame.depends;
                        } else if (!visiting.insert(parent).second) {
                            checked.errors.emplace_back("Cycle detected without causal memory in TrapezoidIntegrator derivative graph.");
                        } else {
                            stack.push_back({parent});
                        }
                        continue;
                    }
                    const auto id = std::move(frame.id);
                    const bool depends = frame.depends;
                    stack.pop_back();
                    visiting.erase(id);
                    varying[id] = depends;
                    if (depends) {
                        if (instances.at(id)->has_state()) {
                            checked.errors.emplace_back("Implicit TrapezoidIntegrator derivative depends on stateful "
                                "direct-feedthrough element '" + id + "'; only stateless feedback operations may be reevaluated.");
                        } else {
                            checked.trapezoid_rhs.push_back(id);
                        }
                    }
                    if (!stack.empty()) stack.back().depends = depends || stack.back().depends;
                }
            };
            for (const auto& id : implicit) {
                for (const auto& parent : parents[id]) visit(parent);
            }
        }
    }
    return checked;
}

} // namespace detail

ValidationResult Validator::validate(const Schema& schema) {
    ValidationResult result;
    result.errors = detail::check_schema(schema).errors;
    return result;
}

} // namespace s7

