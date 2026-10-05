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

