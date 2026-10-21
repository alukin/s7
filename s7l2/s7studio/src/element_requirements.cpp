// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-S7-plugin-permission

#include "element_requirements.hpp"

#include "json_utils.hpp"

#include <QStringList>

#include <s7/element_registry.hpp>
#include <s7/plugin_loader.hpp>

#include <algorithm>
#include <set>

namespace s7studio {

namespace {

struct TypeUse {
    std::string type;
    std::string topLevelElement; // the schema element using it (directly or in its nested schema)
};

// Every element type the schema builds from the registry, nested schemas included.
void collectTypes(const boost::json::object& schema, const std::string* topLevel, std::vector<TypeUse>& uses)
{
    const auto* elements = json_utils::findObjectValue(schema, "elements");
    if (!elements || !elements->is_array()) {
        return;
    }
    for (const auto& value : elements->as_array()) {
        if (!value.is_object()) {
            continue;
        }
        const auto& element = value.as_object();
        const auto id = json_utils::stringValue(element, "id");
        const auto& owner = topLevel ? *topLevel : id;
        if (const auto* nested = json_utils::findObjectValue(element, "schema"); nested && nested->is_object()) {
            // A schema-backed element is built from its nested schema.
            collectTypes(nested->as_object(), &owner, uses);
            continue;
        }
        const auto type = json_utils::stringValue(element, "type");
        if (!type.empty()) {
            uses.push_back({type, owner});
        }
    }
}

auto providerOf(const std::string& type, const std::vector<s7::PluginLoader::LoadedPluginInfo>& plugins)
    -> const s7::PluginLoader::LoadedPluginInfo*
{
    for (const auto& plugin : plugins) {
        if (std::ranges::find(plugin.elements, type) != plugin.elements.end()) {
            return &plugin;
        }
    }
    return nullptr;
}

auto findPlugin(const std::string& name, const std::vector<s7::PluginLoader::LoadedPluginInfo>& plugins)
    -> const s7::PluginLoader::LoadedPluginInfo*
{
    for (const auto& plugin : plugins) {
        if (plugin.name == name) {
            return &plugin;
        }
    }
    return nullptr;
}

auto q(const std::string& text) -> QString
{
    return QString::fromStdString(text);
}

} // namespace

auto pluginRequirements(const boost::json::object& schema, const s7::PluginLoader& loader) -> boost::json::array
{
    std::vector<TypeUse> uses;
    collectTypes(schema, nullptr, uses);
    const auto plugins = loader.loaded_plugin_info();
    std::map<std::string, std::set<std::string>> needed; // plugin -> types
    for (const auto& use : uses) {
        if (const auto* plugin = providerOf(use.type, plugins)) {
            needed[plugin->name].insert(use.type);
        }
    }
    boost::json::array requirements;
    for (const auto& [name, types] : needed) {
        boost::json::array elements;
        for (const auto& type : types) {
            elements.emplace_back(type);
        }
        requirements.push_back(
            boost::json::object{{"plugin", name}, {"version", findPlugin(name, plugins)->version}, {"elements", std::move(elements)}});
    }
    return requirements;
}

auto ensureElementTypes(const boost::json::object& schema, s7::PluginLoader& loader, s7::ElementRegistry& registry,
                        const std::vector<std::filesystem::path>& extraPlugins) -> ElementCheck
{
    struct Requirement {
        std::string plugin;
        std::string version;
        std::vector<std::string> elements;
    };
    std::vector<Requirement> requirements;
    ElementCheck check;
    if (const auto* list = json_utils::findObjectValue(schema, "requires")) {
        if (!list->is_array()) {
            check.errors.push_back(QStringLiteral("The schema's \"requires\" must be a list."));
        } else {
            for (const auto& value : list->as_array()) {
                const auto& object = value.is_object() ? value.as_object() : boost::json::object{};
                Requirement requirement{json_utils::stringValue(object, "plugin"), json_utils::stringValue(object, "version"), {}};
                if (const auto* elements = json_utils::findObjectValue(object, "elements"); elements && elements->is_array()) {
                    for (const auto& element : elements->as_array()) {
                        if (element.is_string()) {
                            requirement.elements.emplace_back(element.as_string());
                        }
                    }
                }
                if (requirement.plugin.empty()) {
                    check.errors.push_back(QStringLiteral("A \"requires\" entry of the schema names no plugin."));
                    continue;
                }
                requirements.push_back(std::move(requirement));
            }
        }
    }
    std::vector<TypeUse> uses;
    collectTypes(schema, nullptr, uses);

    // Look for plugins only when something is missing.
    auto missing = [&] {
        const auto plugins = loader.loaded_plugin_info();
        return std::ranges::any_of(requirements, [&](const Requirement& r) { return !findPlugin(r.plugin, plugins); }) ||
               std::ranges::any_of(uses, [&](const TypeUse& use) { return !registry.find(use.type); });
    };
    if (missing()) {
        loader.load_plugins(registry);
        for (const auto& path : extraPlugins) {
            if (missing() && std::filesystem::exists(path)) {
                loader.load_plugin(path, registry);
            }
        }
    }

    const auto plugins = loader.loaded_plugin_info();
    std::set<std::string> reported;
    for (const auto& requirement : requirements) {
        const auto* plugin = findPlugin(requirement.plugin, plugins);
        if (!plugin) {
            QStringList types;
            for (const auto& type : requirement.elements) {
                types << q(type);
                reported.insert(type);
            }
            check.errors.push_back(QStringLiteral("Plugin '%1' (for %2) was not found in the plugin search paths.")
                                       .arg(q(requirement.plugin), types.isEmpty() ? QStringLiteral("no listed element") : types.join(", ")));
            for (const auto& use : uses) {
                if (std::ranges::find(requirement.elements, use.type) != requirement.elements.end()) {
                    check.elementProblems.emplace(use.topLevelElement,
                                                  QStringLiteral("Plugin '%1' for %2 is not available.").arg(q(requirement.plugin), q(use.type)));
                }
            }
            continue;
        }
        if (!requirement.version.empty() && requirement.version != plugin->version) {
            check.warnings.push_back(QStringLiteral("Plugin '%1' is version %2; the diagram was saved with version %3.")
                                         .arg(q(plugin->name), q(plugin->version), q(requirement.version)));
        }
        for (const auto& type : requirement.elements) {
            if (std::ranges::find(plugin->elements, type) == plugin->elements.end() || !registry.find(type)) {
                reported.insert(type);
                check.errors.push_back(QStringLiteral("Plugin '%1' %2 does not provide element type '%3'.")
                                           .arg(q(plugin->name), q(plugin->version), q(type)));
                for (const auto& use : uses) {
                    if (use.type == type) {
                        check.elementProblems.emplace(use.topLevelElement,
                                                      QStringLiteral("Plugin '%1' does not provide '%2'.").arg(q(plugin->name), q(type)));
                    }
                }
            }
        }
    }
    for (const auto& use : uses) {
        if (registry.find(use.type)) {
            continue;
        }
        check.elementProblems.emplace(use.topLevelElement,
                                      QStringLiteral("Element type '%1' is neither built in nor provided by a loaded plugin.").arg(q(use.type)));
        if (reported.insert(use.type).second) {
            check.errors.push_back(QStringLiteral("Element type '%1' (element '%2') is neither built in nor provided by a plugin found in "
                                                  "the plugin search paths.")
                                       .arg(q(use.type), q(use.topLevelElement)));
        }
    }
    return check;
}

} // namespace s7studio
