#include "s7/core.hpp"

#include <algorithm>
#include <functional>
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

struct PrototypeCatalog {
    PrototypeMap prototypes;
    std::vector<std::string> errors;
};

std::unique_ptr<Element> make_element(const ElementSpec& spec) {
    if (!spec.factory) {
        throw std::invalid_argument("Element '" + spec.id + "' has no factory.");
    }

    auto element = spec.factory();
    if (!element) {
        throw std::invalid_argument("Element factory for '" + spec.id + "' returned null.");
    }

    return element;
}

class PrototypeCatalogBuilder {
public:
    static PrototypeCatalog build(const Schema& schema) {
        PrototypeCatalog catalog;

        for (const auto& spec : schema.elements()) {
            if (spec.id.empty()) {
                catalog.errors.emplace_back("Schema contains an element with an empty id.");
                continue;
            }

            if (catalog.prototypes.contains(spec.id)) {
                catalog.errors.emplace_back("Duplicate element id: '" + spec.id + "'.");
                continue;
            }

            try {
                auto element = make_element(spec);
                catalog.prototypes.emplace(spec.id,
                                           PrototypeInfo{element->input_count(),
                                                         element->output_count(),
                                                         element->has_causal_memory()});
            } catch (const std::exception& ex) {
                catalog.errors.emplace_back(ex.what());
            }
        }

        return catalog;
    }
};

class GraphAlgorithms {
public:
    static AdjacencyMap build_adjacency(const Schema& schema, const PrototypeMap& prototypes) {
        AdjacencyMap adjacency;

        for (const auto& [id, _] : prototypes) {
            adjacency[id] = {};
        }

        for (const auto& connection : schema.connections()) {
            if (prototypes.contains(connection.from_element) && prototypes.contains(connection.to_element)) {
                adjacency[connection.from_element].push_back(connection.to_element);
            }
        }

        return adjacency;
    }

    static std::vector<std::vector<std::string>> strongly_connected_components(const AdjacencyMap& adjacency) {
        std::unordered_set<std::string> visited;
        std::vector<std::string> order;
        order.reserve(adjacency.size());

        std::function<void(const std::string&)> dfs1 = [&](const std::string& node) {
            if (!visited.insert(node).second) {
                return;
            }

            const auto it = adjacency.find(node);
            if (it != adjacency.end()) {
                for (const auto& next : it->second) {
                    dfs1(next);
                }
            }

            order.push_back(node);
        };

        for (const auto& [node, _] : adjacency) {
            dfs1(node);
        }

        const auto reversed = reverse_graph(adjacency);
        visited.clear();

        std::vector<std::vector<std::string>> components;
        std::function<void(const std::string&, std::vector<std::string>&)> dfs2 =
            [&](const std::string& node, std::vector<std::string>& component) {
                if (!visited.insert(node).second) {
                    return;
                }

                component.push_back(node);

                const auto it = reversed.find(node);
                if (it != reversed.end()) {
                    for (const auto& next : it->second) {
                        dfs2(next, component);
                    }
                }
            };

        for (auto it = order.rbegin(); it != order.rend(); ++it) {
            if (visited.contains(*it)) {
                continue;
            }

            std::vector<std::string> component;
            dfs2(*it, component);
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
                         std::vector<std::string>& errors,
                         std::vector<std::string>& warnings) {
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

            validate_port_types(connection, schema, errors, warnings);

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
                                    const Schema& schema,
                                    std::vector<std::string>& errors,
                                    std::vector<std::string>& warnings) {
        auto find_element_spec = [&schema](const std::string& id) -> const ElementSpec* {
            for (const auto& spec : schema.elements()) {
                if (spec.id == id) return &spec;
            }
            return nullptr;
        };

        const auto* from_spec = find_element_spec(connection.from_element);
        const auto* to_spec = find_element_spec(connection.to_element);
        if (!from_spec || !to_spec) return;

        auto from_element = make_element(*from_spec);
        auto to_element = make_element(*to_spec);

        auto from_outputs = from_element->output_ports();
        auto to_inputs = to_element->input_ports();

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
        const auto adjacency = GraphAlgorithms::build_adjacency(schema, prototypes);
        const auto components = GraphAlgorithms::strongly_connected_components(adjacency);

        for (const auto& component : components) {
            const bool cyclic =
                component.size() > 1 || (component.size() == 1 && GraphAlgorithms::has_self_loop(component.front(), adjacency));
            if (!cyclic) {
                continue;
            }

            bool has_memory = false;

            for (const auto& id : component) {
                const auto& info = prototypes.at(id);
                has_memory = has_memory || info.has_causal_memory;
            }

            if (!has_memory) {
                errors.emplace_back("Cycle detected without causal memory.");
            }
        }
    }
};

} // namespace

ValidationResult Validator::validate(const Schema& schema) {
    ValidationResult result;
    auto catalog = PrototypeCatalogBuilder::build(schema);
    result.errors = std::move(catalog.errors);

    ConnectionRulesValidator::validate(schema, catalog.prototypes, result.errors, result.warnings);

    if (!result.ok()) {
        return result;
    }

    CycleRulesValidator::validate(schema, catalog.prototypes, result.errors);

    return result;
}

} // namespace s7

