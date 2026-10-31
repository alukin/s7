// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: LGPL-3.0-or-later

#ifndef S7CORE_INCLUDE_S7_SCHEMA_LIBRARY_HPP
#define S7CORE_INCLUDE_S7_SCHEMA_LIBRARY_HPP

#include "s7/element_metadata.hpp"
#include "s7/element_registry.hpp"
#include "s7/schema.hpp"

#include <boost/json.hpp>

#include <filesystem>
#include <string>
#include <vector>

namespace s7 {

struct SchemaLibraryElementDefinition {
    std::string type_name;
    std::string display_name;
    std::string description;
    std::string category;
    std::string version;
    boost::json::value schema;
};

struct SchemaLibraryDefinition {
    std::string name;
    std::vector<SchemaLibraryElementDefinition> elements;

    [[nodiscard]] auto to_json() const -> boost::json::value;
};

// Configure a fixed-step graph and its embedded schema elements together: sets
// the schema-level "dt" seed of the graph and every nested schema, which the
// rate-following elements adopt. Registry-only schema elements are
// materialized into independent JSON payloads. Element parameters, update
// rules and execution order are not changed.
bool configure_schema_discretization(boost::json::value& schema, double dt,
                                    const ElementRegistry& registry = ElementRegistry::instance());

// Records the design rate of rate-designed elements (those whose metadata
// declares a "design_rate_hz" parameter, e.g. FirFilter and IirFilter) that
// have none: the rate of the schema they were written in, its own "dt" if it
// has one, otherwise `dt`. Nested schemas use their own "dt". Call it with a
// document's saved dt when the document is opened, never with a run's dt,
// which is not the rate the coefficients were designed for. Returns whether
// anything was recorded.
bool record_design_rates(boost::json::value& schema, double dt,
                         const ElementRegistry& registry = ElementRegistry::instance());

class SchemaLibraryLoader {
public:
    [[nodiscard]] static auto load_from_file(const std::filesystem::path& file_path) -> SchemaLibraryDefinition;
    [[nodiscard]] static auto load_from_json(const boost::json::value& json_value) -> SchemaLibraryDefinition;

    static void register_library(const SchemaLibraryDefinition& definition, ElementRegistry& registry);
    static void register_library_file(const std::filesystem::path& file_path, ElementRegistry& registry);

    [[nodiscard]] static auto build_schema(const boost::json::value& schema_json,
                                           const ElementRegistry& registry = ElementRegistry::instance()) -> Schema;
};

} // namespace s7

#endif // S7CORE_INCLUDE_S7_SCHEMA_LIBRARY_HPP

