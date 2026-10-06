#pragma once

// The element types a schema needs and the plugins that provide them
// (`requires` in the runtime schema; docs/STUDIO_DIAGRAM_EDITOR.md,
// "Element types and plugins").

#include <boost/json.hpp>

#include <QString>

#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace s7 {
class ElementRegistry;
class PluginLoader;
}

namespace s7studio {

// The `requires` list for a schema: one entry per loaded plugin that provides
// element types the schema (or a nested schema) uses, with the plugin's
// version and those types. Types not listed are base elements, which every
// S7 runtime provides under the same name.
[[nodiscard]] auto pluginRequirements(const boost::json::object& schema, const s7::PluginLoader& loader) -> boost::json::array;

struct ElementCheck {
    std::vector<QString> errors;   // what is missing
    std::vector<QString> warnings; // e.g. another plugin version than the schema was saved with
    // Top-level element id -> why it cannot be built.
    std::map<std::string, QString> elementProblems;
};

// Makes the schema's element types available: when a plugin the schema
// requires is not loaded, or a type is not registered, the plugin search
// paths (and `extraPlugins`) are scanned again and what is found is loaded.
// Reports what is still missing.
[[nodiscard]] auto ensureElementTypes(const boost::json::object& schema, s7::PluginLoader& loader, s7::ElementRegistry& registry,
                                      const std::vector<std::filesystem::path>& extraPlugins = {}) -> ElementCheck;

} // namespace s7studio
