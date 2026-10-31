// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: LGPL-3.0-or-later

#ifndef S7CORE_INCLUDE_S7_VALIDATION_HPP
#define S7CORE_INCLUDE_S7_VALIDATION_HPP

#include "s7/schema.hpp"
#include "s7/coupled_integration.hpp"

#include <cstddef>
#include <memory>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

namespace s7 {

struct ValidationResult {
    std::vector<std::string> errors;
    std::vector<std::string> warnings;

    [[nodiscard]] bool ok() const;
};

class Validator {
public:
    static ValidationResult validate(const Schema& schema);
};

namespace detail {

// One exposed port of a flattened nested schema (ADR-024): a wire of the
// declared type between the parent's connection and the internal port.
// Validation, type and rate inference and the cycle check treat it like any
// direct-feedthrough element; the compiler never steps it, binding its
// consumers to its source instead.
class SchemaPort final : public Element {
public:
    SchemaPort(std::string owner, std::string name, bool input, std::size_t index, SignalType type)
        : Element("SchemaPort"), owner_(std::move(owner)), name_(std::move(name)), input_(input), index_(index)
    {
        register_input(0, port_names::in, type, nullptr);
        register_output(0, port_names::out, type, storage_);
    }

    void step() override { throw std::logic_error("SchemaPort '" + owner_ + "' is bound through, never stepped."); }

    // Path id of the nested schema, the exposed port's name and index, and
    // whether it is an exposed input (otherwise an exposed output).
    [[nodiscard]] const std::string& owner() const { return owner_; }
    [[nodiscard]] const std::string& name() const { return name_; }
    [[nodiscard]] bool input() const { return input_; }
    [[nodiscard]] std::size_t index() const { return index_; }

private:
    std::string owner_;
    std::string name_;
    bool input_;
    std::size_t index_;
    alignas(16) std::byte storage_[32]{}; // never read: consumers bind to the source
};

// The schema compiled and checked in place of a schema with nested schemas:
// each nested schema with no rate of its own, or the rate of the schema
// containing it, is replaced by its elements (ids "nested/inner") and a
// SchemaPort per exposed port (ids "nested:in:name", "nested:out:name"), so
// it behaves exactly like its inlined contents. A nested schema at another
// rate stays one element. A Schema is never copied: specs are.
struct FlatSchema {
    std::unique_ptr<Schema> schema;
    std::vector<std::string> errors;
    // Path id of every flattened nested schema -> the SchemaPort id of each of
    // its exposed outputs, by output index.
    std::unordered_map<std::string, std::vector<std::string>> nested_outputs;
};
[[nodiscard]] FlatSchema flatten_schema(const Schema& schema);

// Validation that keeps what it created: every element is created exactly
// once (null where creation failed). The compiler builds the graph from these
// instances and the flattened schema.
struct CheckedSchema {
    struct CoupledRegion {
        CoupledMethod method{CoupledMethod::None};
        Rate rate;
        double step_seconds{};
        int substeps{1};
        std::vector<std::string> states;
        std::vector<Connection> derivative_connections;
        std::vector<std::string> upstream_states;
        std::vector<std::string> rhs;
    };
    std::vector<std::string> errors;
    // The flattened schema everything below refers to (elements[i] belongs to
    // flat->elements()[i]); null only if flattening failed.
    std::unique_ptr<Schema> flat;
    std::unordered_map<std::string, std::vector<std::string>> nested_outputs;
    std::vector<std::unique_ptr<Element>> elements;
    std::vector<std::string> trapezoid_rhs;
    // Resolved rate of each element, when validation succeeded.
    std::unordered_map<std::string, Rate> rates;
    // Rate domains (ADR-022): one per distinct rate, in step order (every
    // Resampler's input domain before its output domain).
    std::vector<Rate> domain_rates;
    // Resamplers that change the rate: element id, input domain, output domain
    // (indices into domain_rates).
    struct DomainLink {
        std::string resampler;
        std::size_t input{};
        std::size_t output{};
    };
    std::vector<DomainLink> domain_links;
    std::vector<CoupledRegion> coupled_regions;
};
[[nodiscard]] CheckedSchema check_schema(const Schema& schema);

} // namespace detail

} // namespace s7

#endif // S7CORE_INCLUDE_S7_VALIDATION_HPP
