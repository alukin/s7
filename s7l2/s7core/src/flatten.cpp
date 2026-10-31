// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: LGPL-3.0-or-later

// Flattening of nested schemas (ADR-024): a nested schema behaves exactly
// like its inlined contents. Its elements join the parent graph under path
// ids, so validation, inference, the cycle check and scheduling see every
// internal element, and a loop through internal causal memory is valid as it
// would be inlined. A nested schema with a rate of its own keeps it: its
// ports declare it, as the schema element did, and it seeds its elements.
// Flattening is local to one compilation: documents and
// deployed diagrams keep their nested schemas, and since a nested schema is
// placed on one node as a whole, it never crosses a network boundary.

#include "s7/core.hpp"

#include <deque>
#include <exception>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
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
    explicit Flattener(FlatSchema& flat, std::optional<Rate> top_seed) : flat_(flat), top_seed_(top_seed) {}

    // Adds the elements and connections of `schema` with ids under `path`
    // ("" for the top level, else the nested schema's path id). `seed` is the
    // rate seeding this level: the top level's own rate, inherited by nested
    // schemas without a rate of their own. `pin` is this nested schema's own
    // rate when it differs from the seed around it.
    void add(const Schema& schema, const std::string& path, const std::optional<Rate>& seed,
             const std::optional<Rate>& pin = std::nullopt)
    {
        Level level{path, {}};
        std::unordered_set<std::string> ids;
        for (const auto& spec : schema.elements()) {
            // Checked here, before a nested schema's id disappears into the
            // path ids of its contents.
            if (!spec.id.empty() && !ids.insert(spec.id).second) {
                flat_.errors.push_back("Duplicate element id: '" + level.id_of(spec.id) + "'.");
                continue;
            }
            const auto id = level.id_of(spec.id);
            if (!spec.factory) { // generic: never a schema
                add_spec(ElementSpec{id, {}, spec.generic}, seed);
                continue;
            }
            std::unique_ptr<Element> created;
            try {
                created = spec.factory();
            } catch (const std::exception&) {
                // Created again by type inference, which reports the error.
                add_spec(ElementSpec{id, spec.factory, {}}, seed);
                continue;
            }
            if (const auto* nested = dynamic_cast<const Schema*>(created.get())) {
                const bool own_rate = nested->rate() && nested->rate() != seed;
                add(*nested, id, own_rate ? nested->rate() : seed, own_rate ? nested->rate() : std::nullopt);
                level.nested.emplace(spec.id, Ports{nested->exposed_inputs(), nested->exposed_outputs()});
                continue;
            }
            add_spec(ElementSpec{id, reuse_once(std::move(created), spec.factory), {}}, seed);
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
            flat_.schema->add_element(id, [path, port, index, pin] {
                return std::make_unique<SchemaPort>(path, port.name, true, index, port.type, pin);
            });
            if (const auto to = level.target(port.element, port.port, flat_.errors)) {
                flat_.schema->connect(id, 0, to->first, to->second);
            }
        }
        for (std::size_t index = 0; index < schema.exposed_outputs().size(); ++index) {
            const auto& port = schema.exposed_outputs()[index];
            const auto id = path + ":out:" + port.name;
            flat_.schema->add_element(id, [path, port, index, pin] {
                return std::make_unique<SchemaPort>(path, port.name, false, index, port.type, pin);
            });
            if (const auto from = level.source(port.element, port.port, flat_.errors)) {
                flat_.schema->connect(from->first, from->second, id, 0);
            }
            outputs.push_back(id);
        }
    }

private:
    using Endpoint = std::optional<std::pair<std::string, std::size_t>>;

    void add_spec(ElementSpec spec, const std::optional<Rate>& seed)
    {
        if (seed && seed != top_seed_) flat_.seeds.emplace(spec.id, *seed);
        flat_.schema->add_element(std::move(spec));
    }

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
    std::optional<Rate> top_seed_;
};

} // namespace

FlatSchema flatten_schema(const Schema& schema)
{
    FlatSchema flat;
    flat.schema = std::make_unique<Schema>(schema.name());
    if (const auto rate = schema.rate()) flat.schema->set_rate(*rate);
    Flattener(flat, schema.rate()).add(schema, "", schema.rate());
    return flat;
}

} // namespace s7::detail
