// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: LGPL-3.0-or-later

// A minimal plugin for plugin_loader_smoke. Built twice: as is, and with
// S7_TEST_PLUGIN_FAIL, where registration fails after one element was
// registered (a duplicate type name).
#include <memory>

#include <s7/element_plugin.hpp>
#include <s7/element_registry.hpp>
#include <s7/elements.hpp>

namespace {

const S7PluginInfo plugin_info{
    .api_version = S7_PLUGIN_API_VERSION,
    .element_count = 1,
    .plugin_name = "s7testplugin",
    .plugin_version = "1.0.0",
    .author = "S7 tests",
};

} // namespace

extern "C" {

const S7PluginInfo* s7_plugin_query() { return &plugin_info; }

const char* s7_plugin_manifest_json() { return nullptr; }

void s7_register_elements(void* registry_ptr)
{
    auto* registry = static_cast<s7::ElementRegistry*>(registry_ptr);
    s7::ElementMetadata metadata;
    metadata.type_name = "TestPluginConstant";
    metadata.display_name = "Test Plugin Constant";
    metadata.category = "test";
    metadata.output_ports = {{"out", "the constant", s7::SignalType::F64, false}};
    registry->register_element(s7::ElementDescriptor(
        "TestPluginConstant", [](const s7::ElementCreationContext&) { return std::make_unique<s7::Constant>(42.0); }, metadata));
#ifdef S7_TEST_PLUGIN_FAIL
    registry->register_element(s7::ElementDescriptor(
        "Constant", [](const s7::ElementCreationContext&) { return std::make_unique<s7::Constant>(0.0); }, metadata));
#endif
}

} // extern "C"
