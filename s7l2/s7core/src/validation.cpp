// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "s7/core.hpp"
#include "s7/rate_inference.hpp"
#include "s7/resampler.hpp"
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

using Parents = std::unordered_map<std::string, std::vector<std::string>>;

Parents parents_of(const Schema& schema) {
    Parents parents;
    for (const auto& edge : schema.connections()) parents[edge.to_element].push_back(edge.from_element);
    return parents;
}

// The same causal derivative-cone analysis serves existing implicit
// trapezoid and the new F64 regions. Only operations whose output varies
// with a trial state are replayed; independent sources stay held.
std::vector<std::string> trial_rhs(const InstanceMap& instances, const Parents& parents,
                                   const std::unordered_set<std::string>& roots,
                                   const std::unordered_set<std::string>& varying_states, bool restricted,
                                   std::vector<std::string>& errors) {
    std::vector<std::string> rhs;
    std::unordered_map<std::string, bool> varying;
    std::unordered_set<std::string> visiting;
    const auto known = [&](const std::string& id) -> std::optional<bool> {
        if (varying_states.contains(id)) return true;
        if (instances.at(id)->has_causal_memory()) return false;
        if (const auto it = varying.find(id); it != varying.end()) return it->second;
        return std::nullopt;
    };
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
            const auto parent_it = parents.find(frame.id);
            const auto count = parent_it == parents.end() ? 0 : parent_it->second.size();
            if (frame.next < count) {
                const auto parent = parent_it->second[frame.next++];
                if (const auto value = known(parent)) {
                    frame.depends = *value || frame.depends;
                } else if (!visiting.insert(parent).second) {
                    errors.emplace_back(restricted ? "Cycle detected without causal memory in coupled derivative graph."
                                                   : "Cycle detected without causal memory in TrapezoidIntegrator derivative graph.");
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
                const auto* element = instances.at(id);
                if (element->has_state()) {
                    errors.emplace_back((restricted ? "Coupled integration" : "Implicit TrapezoidIntegrator") +
                        std::string(" derivative depends on stateful direct-feedthrough element '") + id +
                        "'; only stateless feedback operations may be reevaluated.");
                } else if (restricted && !dynamic_cast<const Gain*>(element) &&
                           !dynamic_cast<const Adder*>(element) && !dynamic_cast<const Multiply*>(element)) {
                    errors.emplace_back("Coupled integration derivative contains unsupported trial element '" + id +
                                        "'; only built-in F64 arithmetic may be replayed.");
                } else {
                    rhs.push_back(id);
                }
            }
            if (!stack.empty()) stack.back().depends = depends || stack.back().depends;
        }
    };
    for (const auto& id : roots) {
        if (const auto it = parents.find(id); it != parents.end()) {
            for (const auto& parent : it->second) visit(parent);
        }
    }
    return rhs;
}

// Memory-level dependency graph, used to refuse a feedback cycle crossing
// numerical methods. A one-way other-memory input is held for this interval.
std::unordered_map<std::string, std::unordered_set<std::string>> memory_dependencies(
    const InstanceMap& instances, const Parents& parents) {
    std::unordered_map<std::string, std::unordered_set<std::string>> result;
    for (const auto& [id, element] : instances) {
        if (!element->has_causal_memory()) continue;
        auto& ancestors = result[id];
        std::unordered_set<std::string> seen;
        std::vector<std::string> stack;
        if (const auto it = parents.find(id); it != parents.end())
            stack.insert(stack.end(), it->second.begin(), it->second.end());
        while (!stack.empty()) {
            auto current = std::move(stack.back());
            stack.pop_back();
            if (!seen.insert(current).second) continue;
            if (instances.at(current)->has_causal_memory()) {
                ancestors.insert(current);
                continue;
            }
            if (const auto it = parents.find(current); it != parents.end())
                stack.insert(stack.end(), it->second.begin(), it->second.end());
        }
    }
    return result;
}

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
                const auto input = input_name(instances, id, port);
                if (counts[port] == 0) {
                    errors.emplace_back(input + " is unconnected.");
                } else if (counts[port] > 1) {
                    errors.emplace_back(input + " has multiple drivers.");
                }
            }
        }
    }

private:
    static const detail::SchemaPort* schema_port(const InstanceMap& instances, const std::string& id)
    {
        const auto it = instances.find(id);
        return it == instances.end() ? nullptr : dynamic_cast<const detail::SchemaPort*>(it->second);
    }

    // Ports named as the user wrote them: a flattened nested schema's
    // exposed ports by the schema's id and port index or name.
    static std::string input_name(const InstanceMap& instances, const std::string& id, const std::size_t port)
    {
        if (const auto* boundary = schema_port(instances, id)) {
            return boundary->input() ? "Input port " + std::to_string(boundary->index()) + " of '" + boundary->owner() + "'"
                                     : "Exposed output '" + boundary->name() + "' of '" + boundary->owner() + "'";
        }
        return "Input port " + std::to_string(port) + " of '" + id + "'";
    }

    static std::string source_label(const InstanceMap& instances, const std::string& id, const std::size_t port)
    {
        if (const auto* boundary = schema_port(instances, id)) {
            return boundary->input() ? "exposed input '" + boundary->name() + "' of '" + boundary->owner() + "'"
                                     : "'" + boundary->owner() + "' output port " + std::to_string(boundary->index());
        }
        return "'" + id + "' output port " + std::to_string(port);
    }

    static std::string target_label(const InstanceMap& instances, const std::string& id, const std::size_t port)
    {
        if (const auto* boundary = schema_port(instances, id)) {
            return boundary->input() ? "'" + boundary->owner() + "' input port " + std::to_string(boundary->index())
                                     : "exposed output '" + boundary->name() + "' of '" + boundary->owner() + "'";
        }
        return "'" + id + "' input port " + std::to_string(port);
    }

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


        errors.emplace_back("Port type mismatch: " + source_label(instances, connection.from_element, connection.from_port) +
                            " (type " + std::string{signal_type_name(from_type)} + ") -> " +
                            target_label(instances, connection.to_element, connection.to_port) + " (type " +
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

CheckedSchema check_schema(const Schema& nested_schema) {
    CheckedSchema checked;
    // Nested schemas are inlined first (ADR-024): every check below sees the
    // flattened graph, so a nested schema behaves exactly like its contents.
    auto flat = flatten_schema(nested_schema);
    checked.flat = std::move(flat.schema);
    checked.nested_outputs = std::move(flat.nested_outputs);
    if (!flat.errors.empty()) {
        checked.errors = std::move(flat.errors);
        return checked;
    }
    const Schema& schema = *checked.flat;
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
        // Rate domains (doc/S7_Sample_Rate_Model.md section 8.3): one per
        // distinct rate, joined only by Resamplers. Each Resampler learns its
        // input domain's rate. Domains must form a DAG: feedback across rate
        // domains is not supported yet.
        std::vector<Rate> rates;
        const auto domain = [&rates](const Rate& rate) {
            for (std::size_t i = 0; i < rates.size(); ++i) if (rates[i] == rate) return i;
            rates.push_back(rate);
            return rates.size() - 1;
        };
        for (const auto& spec : schema.elements()) domain(checked.rates.at(spec.id));
        std::unordered_map<std::string, std::size_t> index_of;
        for (std::size_t index = 0; index < schema.elements().size(); ++index) index_of.emplace(schema.elements()[index].id, index);
        std::vector<CheckedSchema::DomainLink> links;
        for (const auto& connection : schema.connections()) {
            const auto target = index_of.find(connection.to_element);
            if (target == index_of.end() || !checked.elements[target->second]) continue;
            auto* changing = dynamic_cast<RateChangingElement*>(checked.elements[target->second].get());
            if (!changing || connection.to_port != 0) continue;
            const auto& input = checked.rates.at(connection.from_element);
            const auto& output = checked.rates.at(connection.to_element);
            try {
                changing->adopt_input_rate(input);
            } catch (const std::invalid_argument& error) {
                checked.errors.push_back("Element '" + connection.to_element + "': " + error.what());
                continue;
            }
            if (input != output) links.push_back({connection.to_element, domain(input), domain(output)});
        }
        // Order the domains so each Resampler's input domain steps first.
        std::vector<std::size_t> pending(rates.size(), 0);
        for (const auto& link : links) ++pending[link.output];
        std::vector<std::size_t> order;
        for (std::size_t i = 0; i < rates.size(); ++i) if (pending[i] == 0) order.push_back(i);
        for (std::size_t next = 0; next < order.size(); ++next) {
            for (const auto& link : links) {
                if (link.input == order[next] && --pending[link.output] == 0) order.push_back(link.output);
            }
        }
        if (order.size() != rates.size()) {
            std::string resamplers;
            for (const auto& link : links) {
                if (pending[link.output] != 0) resamplers += (resamplers.empty() ? "'" : ", '") + link.resampler + "'";
            }
            checked.errors.push_back("Feedback across rate domains through Resampler " + resamplers +
                                     " is not supported yet; signals may only flow one way between rates.");
        } else {
            std::vector<std::size_t> position(rates.size());
            for (std::size_t i = 0; i < order.size(); ++i) {
                position[order[i]] = i;
                checked.domain_rates.push_back(rates[order[i]]);
            }
            for (auto& link : links) {
                link.input = position[link.input];
                link.output = position[link.output];
            }
            checked.domain_links = std::move(links);
        }
    }
    if (checked.errors.empty()) {
        // Find only derivative ancestors whose value varies with a trial
        // integrator state. Independent sources/other memory outputs are held
        // fixed for this interval; stateful direct-feedthrough dependencies
        // must never be stepped repeatedly by the numerical solver.
        std::unordered_set<std::string> implicit;
        for (const auto& [id, instance] : instances) {
            const auto* integration = dynamic_cast<const TrapezoidIntegration*>(instance);
            if (integration && integration->implicit_trapezoid_enabled()) implicit.insert(id);
        }
        // Group selected F64 states by method, outer rate and substep count.
        // Split disconnected states later: placing them in one dense GL
        // solve is equivalent mathematically but scales unnecessarily.
        for (const auto& spec : schema.elements()) {
            const auto* integration = dynamic_cast<const CoupledIntegration*>(instances.at(spec.id));
            if (!integration || integration->coupled_method() == CoupledMethod::None) continue;
            const auto* element = instances.at(spec.id);
            if (!element->has_causal_memory() || element->input_count() != 1 || element->output_count() != 1 ||
                element->input_ports()[0].type != SignalType::F64 || element->output_ports()[0].type != SignalType::F64) {
                checked.errors.emplace_back("Coupled integration requires one F64 state and derivative at '" + spec.id + "'.");
                continue;
            }
            const auto rate = checked.rates.find(spec.id);
            if (rate == checked.rates.end()) {
                checked.errors.emplace_back("Coupled integration requires a resolved rate at '" + spec.id + "'.");
                continue;
            }
            auto it = std::ranges::find_if(checked.coupled_regions, [&](const auto& region) {
                return region.method == integration->coupled_method() && region.rate == rate->second &&
                       region.substeps == integration->coupled_substeps();
            });
            if (it == checked.coupled_regions.end()) {
                CheckedSchema::CoupledRegion region;
                region.method = integration->coupled_method();
                region.rate = rate->second;
                region.step_seconds = integration->coupled_step_seconds();
                region.substeps = integration->coupled_substeps();
                checked.coupled_regions.push_back(std::move(region));
                it = std::prev(checked.coupled_regions.end());
            }
            it->states.push_back(spec.id);
            for (const auto& edge : schema.connections()) {
                if (edge.to_element == spec.id && edge.to_port == 0)
                    it->derivative_connections.push_back(edge);
            }
        }
        if (!implicit.empty() || !checked.coupled_regions.empty()) {
            const auto parents = parents_of(schema);
            if (!implicit.empty())
                checked.trapezoid_rhs = trial_rhs(instances, parents, implicit, implicit, false, checked.errors);
            if (!checked.coupled_regions.empty()) {
                const auto dependencies = memory_dependencies(instances, parents);
                AdjacencyMap memory_graph;
                for (const auto& [id, ancestors] : dependencies) {
                    auto& edges = memory_graph[id];
                    edges.insert(edges.end(), ancestors.begin(), ancestors.end());
                }
                const auto components = GraphAlgorithms::strongly_connected_components(memory_graph);
                std::unordered_map<std::string, std::size_t> component_of;
                for (std::size_t index = 0; index < components.size(); ++index)
                    for (const auto& id : components[index]) component_of.emplace(id, index);
                std::unordered_map<std::string, std::size_t> group_of;
                for (std::size_t index = 0; index < checked.coupled_regions.size(); ++index)
                    for (const auto& id : checked.coupled_regions[index].states) group_of.emplace(id, index);
                // One error per memory cycle that crosses a numerical policy.
                for (const auto& component : components) {
                    std::optional<std::size_t> selected;
                    std::string selected_id, external;
                    for (const auto& id : component) {
                        if (const auto it = group_of.find(id); it != group_of.end()) {
                            selected = it->second;
                            selected_id = id;
                            break;
                        }
                    }
                    if (!selected) continue;
                    for (const auto& id : component) {
                        const auto it = group_of.find(id);
                        if (it == group_of.end() || it->second != *selected) {
                            external = id;
                            break;
                        }
                    }
                    if (!external.empty())
                        checked.errors.emplace_back("Coupled integration feedback at '" + selected_id +
                            "' crosses method or region through '" + external +
                            "'; use one numerical policy for all coupled states.");
                }

                std::vector<CheckedSchema::CoupledRegion> regions;
                for (std::size_t group_index = 0; group_index < checked.coupled_regions.size(); ++group_index) {
                    const auto& group = checked.coupled_regions[group_index];
                    const auto append = [&](const std::unordered_set<std::string>& members,
                                            const std::vector<std::string>& upstream) {
                        CheckedSchema::CoupledRegion region;
                        region.method = group.method;
                        region.rate = group.rate;
                        region.step_seconds = group.step_seconds;
                        region.substeps = group.substeps;
                        region.upstream_states = upstream;
                        for (const auto& id : group.states) {
                            if (!members.contains(id)) continue;
                            region.states.push_back(id);
                            for (const auto& edge : group.derivative_connections)
                                if (edge.to_element == id) region.derivative_connections.push_back(edge);
                        }
                        regions.push_back(std::move(region));
                    };
                    if (group.method == CoupledMethod::GaussLegendre2) {
                        // Each strongly coupled block needs one dense solve. A
                        // one-way cascade is a DAG of small stage solves.
                        std::vector<std::size_t> order;
                        std::unordered_set<std::size_t> seen;
                        for (const auto& id : group.states)
                            if (const auto component = component_of.at(id); seen.insert(component).second)
                                order.push_back(component);
                        std::unordered_map<std::size_t, std::size_t> pending;
                        std::unordered_map<std::size_t, std::vector<std::size_t>> dependents;
                        for (const auto component : order) pending.emplace(component, 0);
                        for (const auto& id : group.states) {
                            const auto target = component_of.at(id);
                            for (const auto& ancestor : dependencies.at(id)) {
                                const auto other = group_of.find(ancestor);
                                if (other == group_of.end() || other->second != group_index) continue;
                                const auto source_component = component_of.at(ancestor);
                                if (source_component == target) continue;
                                auto& downstream = dependents[source_component];
                                if (std::ranges::find(downstream, target) == downstream.end()) {
                                    downstream.push_back(target);
                                    ++pending[target];
                                }
                            }
                        }
                        std::vector<std::size_t> ready;
                        for (const auto component : order)
                            if (pending[component] == 0) ready.push_back(component);
                        for (std::size_t index = 0; index < ready.size(); ++index) {
                            const auto component = ready[index];
                            std::unordered_set<std::string> members, upstream_set;
                            for (const auto& id : group.states)
                                if (component_of.at(id) == component) members.insert(id);
                            for (const auto& id : members) {
                                for (const auto& ancestor : dependencies.at(id)) {
                                    const auto other = group_of.find(ancestor);
                                    if (other != group_of.end() && other->second == group_index &&
                                        component_of.at(ancestor) != component) upstream_set.insert(ancestor);
                                }
                            }
                            std::vector<std::string> upstream;
                            for (const auto& id : group.states)
                                if (upstream_set.contains(id)) upstream.push_back(id);
                            append(members, upstream);
                            for (const auto downstream : dependents[component])
                                if (--pending[downstream] == 0) ready.push_back(downstream);
                        }
                        if (ready.size() != order.size())
                            throw std::logic_error("Memory SCC condensation must be acyclic.");
                    } else {
                        // Euler substeps need simultaneous values at every
                        // internal substep, so retain weakly connected groups.
                        const std::unordered_set<std::string> members(group.states.begin(), group.states.end());
                        std::unordered_map<std::string, std::vector<std::string>> adjacent;
                        for (const auto& id : group.states) adjacent[id];
                        for (const auto& id : group.states) {
                            for (const auto& ancestor : dependencies.at(id)) {
                                if (ancestor == id || !members.contains(ancestor)) continue;
                                adjacent[id].push_back(ancestor);
                                adjacent[ancestor].push_back(id);
                            }
                        }
                        std::unordered_set<std::string> assigned;
                        for (const auto& seed : group.states) {
                            if (!assigned.insert(seed).second) continue;
                            std::vector<std::string> queue{seed};
                            for (std::size_t index = 0; index < queue.size(); ++index)
                                for (const auto& next : adjacent.at(queue[index]))
                                    if (assigned.insert(next).second) queue.push_back(next);
                            append(std::unordered_set<std::string>(queue.begin(), queue.end()), {});
                        }
                    }
                }
                checked.coupled_regions = std::move(regions);
                for (auto& region : checked.coupled_regions) {
                    if (region.method == CoupledMethod::GaussLegendre2 && region.states.size() > 16)
                        checked.errors.emplace_back("Gauss-Legendre integration at '" + region.states.front() +
                            "' currently supports at most 16 F64 states per strongly coupled region.");
                    std::unordered_set<std::string> varying(region.states.begin(), region.states.end());
                    varying.insert(region.upstream_states.begin(), region.upstream_states.end());
                    const std::unordered_set<std::string> roots(region.states.begin(), region.states.end());
                    region.rhs = trial_rhs(instances, parents, roots, varying, true, checked.errors);
                }
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

