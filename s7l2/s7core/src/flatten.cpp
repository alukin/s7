// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: LGPL-3.0-or-later

// Flattening of nested schemas (ADR-024): a nested schema behaves exactly
// like its inlined contents. Its elements join the parent graph under path
// ids, so validation, inference, the cycle check and scheduling see every
// internal element, and a loop through internal causal memory is valid as it
// would be inlined. Flattening is local to one compilation: documents and
// deployed diagrams keep their nested schemas, and since a nested schema is
// placed on one node as a whole, it never crosses a network boundary.

#include "s7/core.hpp"

#include <deque>
#include <exception>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace s7::detail {
namespace {

// A factory whose first call returns the element created to find out whether
// it is a schema, so every element is still created only once.
ElementFactory reuse_once(std::unique_ptr<Element> created, ElementFactory factory)
{
    auto holder = std::make_shared<std::unique_ptr<Element>>(std::move(created));
    return [holder, factory = std::move(factory)]() -> std::unique_ptr<Element> {
        if (*holder) return std::move(*holder);
        return factory();
    };
}

class Flattener {
public:
    explicit Flattener(FlatSchema& flat) : flat_(flat) {}

    // Adds the elements and connections of `schema` with ids under `path`
    // ("" for the top level, else the nested schema's path id). `seed` is the
    // rate that seeds this level: the top level's own rate, inherited by every
    // nested schema flattened into it.
    void add(const Schema& schema, const std::string& path, const std::optional<Rate>& seed)
    {
        Level level{path, {}};
        for (const auto& spec : schema.elements()) {
            const auto id = level.id_of(spec.id);
            if (!spec.factory) { // generic: never a schema
                flat_.schema->add_element(ElementSpec{id, {}, spec.generic});
                continue;
            }
            std::unique_ptr<Element> created;
            try {
                created = spec.factory();
            } catch (const std::exception&) {
                // Created again by type inference, which reports the error.
                flat_.schema->add_element(ElementSpec{id, spec.factory, {}});
                continue;
            }
            auto* nested = dynamic_cast<const Schema*>(created.get());
            if (nested && (!nested->rate() || nested->rate() == seed)) {
                add(*nested, id, seed);
                level.nested.emplace(spec.id, Ports{nested->exposed_inputs(), nested->exposed_outputs()});
                continue;
            }
            flat_.schema->add_element(ElementSpec{id, reuse_once(std::move(created), spec.factory), {}});
        }
        for (const auto& connection : schema.connections()) {
            const auto from = level.source(connection.from_element, connection.from_port, flat_.errors);
            const auto to = level.target(connection.to_element, connection.to_port, flat_.errors);
            if (from && to) flat_.schema->connect(from->first, from->second, to->first, to->second);
        }
        if (path.empty()) return; // a top-level schema's exposed ports are boundary metadata only
        // The SchemaPorts of this nested schema, wired to its internal ports.
        auto& outputs = flat_.nested_outputs[path];
        for (std::size_t index = 0; index < schema.exposed_inputs().size(); ++index) {
            const auto& port = schema.exposed_inputs()[index];
            const auto id = path + ":in:" + port.name;
            flat_.schema->add_element(id, [path, port, index] {
                return std::make_unique<SchemaPort>(path, port.name, true, index, port.type);
            });
            if (const auto to = level.target(port.element, port.port, flat_.errors)) {
                flat_.schema->connect(id, 0, to->first, to->second);
            }
        }
        for (std::size_t index = 0; index < schema.exposed_outputs().size(); ++index) {
            const auto& port = schema.exposed_outputs()[index];
            const auto id = path + ":out:" + port.name;
            flat_.schema->add_element(id, [path, port, index] {
                return std::make_unique<SchemaPort>(path, port.name, false, index, port.type);
            });
            if (const auto from = level.source(port.element, port.port, flat_.errors)) {
                flat_.schema->connect(from->first, from->second, id, 0);
            }
            outputs.push_back(id);
        }
    }

private:
    using Endpoint = std::optional<std::pair<std::string, std::size_t>>;

    struct Ports {
        std::deque<Schema::ExposedPort> inputs;
        std::deque<Schema::ExposedPort> outputs;
    };

    // One schema being flattened: its path and its nested schemas that were
    // flattened, whose ports connections must be redirected to.
    struct Level {
        std::string path;
        std::unordered_map<std::string, Ports> nested;

        [[nodiscard]] std::string id_of(const std::string& local) const { return path.empty() ? local : path + "/" + local; }

        // An unknown element keeps its path id, for validation to report.
        Endpoint source(const std::string& element, const std::size_t port, std::vector<std::string>& errors) const
        {
            const auto it = nested.find(element);
            if (it == nested.end()) return std::pair{id_of(element), port};
            if (port >= it->second.outputs.size()) {
                errors.push_back("Connection from '" + id_of(element) + "' uses invalid output port " + std::to_string(port) + ".");
                return std::nullopt;
            }
            return std::pair{id_of(element) + ":out:" + it->second.outputs[port].name, std::size_t{0}};
        }

        Endpoint target(const std::string& element, const std::size_t port, std::vector<std::string>& errors) const
        {
            const auto it = nested.find(element);
            if (it == nested.end()) return std::pair{id_of(element), port};
            if (port >= it->second.inputs.size()) {
                errors.push_back("Connection to '" + id_of(element) + "' uses invalid input port " + std::to_string(port) + ".");
                return std::nullopt;
            }
            return std::pair{id_of(element) + ":in:" + it->second.inputs[port].name, std::size_t{0}};
        }
    };

    FlatSchema& flat_;
};

} // namespace

FlatSchema flatten_schema(const Schema& schema)
{
    FlatSchema flat;
    flat.schema = std::make_unique<Schema>(schema.name());
    if (const auto rate = schema.rate()) flat.schema->set_rate(*rate);
    Flattener(flat).add(schema, "", schema.rate());
    return flat;
}

} // namespace s7::detail
