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

// Configure a fixed-step graph and its embedded schema elements together.
// Registry-only schema elements are materialized into independent JSON payloads.
// This changes dt/rate parameters, not element update rules or execution order.
bool configure_schema_discretization(boost::json::value& schema, double dt,
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

