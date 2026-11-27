// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: LGPL-3.0-or-later

// A minimal plugin for the successful, failing registration, and wrong ABI
// cases in plugin_loader_smoke.
#include <memory>

#include <s7/element_plugin.hpp>
#include <s7/element_registry.hpp>
#include <s7/elements.hpp>
#if defined(S7_TEST_PLUGIN_DEPENDENCY)
extern "C" __declspec(dllimport) double s7_test_dependency_value();
#endif

namespace {

const S7PluginInfo plugin_info{
#ifdef S7_TEST_PLUGIN_WRONG_ABI
    .api_version = S7_PLUGIN_API_VERSION + 1,
#else
    .api_version = S7_PLUGIN_API_VERSION,
#endif
    .element_count = 1,
    .plugin_name = "s7testplugin",
    .plugin_version = "1.0.0",
    .author = "S7 tests",
};

} // namespace

extern "C" {

const S7PluginInfo* s7_plugin_query() { return &plugin_info; }

const char* s7_plugin_manifest_json()
{
#ifdef S7_TEST_PLUGIN_WRONG_ABI
    return R"({"plugin_name":"Wrong ABI fixture"})";
#else
    return nullptr;
#endif
}

void s7_register_elements(void* registry_ptr)
{
    auto* registry = static_cast<s7::ElementRegistry*>(registry_ptr);
    s7::ElementMetadata metadata;
    metadata.type_name = "TestPluginConstant";
    metadata.display_name = "Test Plugin Constant";
    metadata.category = "test";
    metadata.output_ports = {{"out", "the constant", s7::SignalType::F64, false}};
    registry->register_element(s7::ElementDescriptor(
        "TestPluginConstant", [](const s7::ElementCreationContext&) {
#if defined(S7_TEST_PLUGIN_DEPENDENCY)
            return std::make_unique<s7::Constant>(s7_test_dependency_value());
#else
            return std::make_unique<s7::Constant>(42.0);
#endif
        }, metadata));
#ifdef S7_TEST_PLUGIN_FAIL
    registry->register_element(s7::ElementDescriptor(
        "Constant", [](const s7::ElementCreationContext&) { return std::make_unique<s7::Constant>(0.0); }, metadata));
#endif
}

} // extern "C"
