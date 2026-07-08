#include "s7/core.hpp"

#include <queue>
#include <sstream>
#include <stdexcept>
#include <unordered_set>
#include <unordered_map>

namespace s7 {
namespace {

struct PrototypeInfo {
    std::size_t input_count{};
    std::size_t output_count{};
    bool has_causal_memory{};
};

using PrototypeMap = std::unordered_map<std::string, PrototypeInfo>;

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
    const auto validation = Validator::validate(schema);
    if (!validation.ok()) {
        throw std::invalid_argument("Schema validation failed:\n" + join_errors(validation.errors));
    }

    ExecutableGraph executable_graph;
    executable_graph.schema_name = schema.name();

    PrototypeMap prototypes;
    for (const auto& spec : schema.elements()) {
        auto element = make_element(spec);
        prototypes.emplace(spec.id,
                           PrototypeInfo{element->input_count(),
                                         element->output_count(),
                                         element->has_causal_memory()});
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
    return executable_graph;
}

} // namespace s7

