// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "s7noise/noise_elements.hpp"
#include "s7/element_registry.hpp"
#include "s7/element_plugin.hpp"
#include "s7/metadata_json.hpp"
#include "noise_metadata.hpp"

#include <memory>

// ============================================================================
// Plugin Information
// ============================================================================

static const S7PluginInfo plugin_info{
    .api_version = S7_PLUGIN_API_VERSION,
    .element_count = 2,
    .plugin_name = "s7noise",
    .plugin_version = "1.0.0",
    .author = "S7 Project"
};

static const char plugin_manifest_json[] = R"json(
{
  "api_version": 1,
  "plugin_name": "s7noise",
  "plugin_version": "1.0.0",
  "author": "S7 Project",
  "description": "Noise source elements for the runtime.",
  "element_count": 2,
  "elements": ["WhiteNoiseGenerator", "PinkNoiseGenerator"]
}
)json";

extern "C" {

const S7PluginInfo* s7_plugin_query() {
    return &plugin_info;
}

const char* s7_plugin_manifest_json() {
    return plugin_manifest_json;
}

} // extern "C"

// ============================================================================
// Plugin Registration
// ============================================================================

extern "C" {

void s7_register_elements(void* registry_ptr) {
    auto* registry = static_cast<s7::ElementRegistry*>(registry_ptr);
    static const s7::ElementMetadataLibrary metadata(s7::resources::noise_metadata);

    // Register WhiteNoiseGenerator
    registry->register_element(s7::ElementDescriptor(
        "WhiteNoiseGenerator",
        [](const s7::ElementCreationContext& ctx) {
            double amplitude = 1.0;
            uint32_t seed = 0;

            if (ctx.has_param("amplitude")) {
                amplitude = static_cast<float>(ctx.get_real("amplitude"));
            }
            if (ctx.has_param("seed")) {
                seed = static_cast<uint32_t>(ctx.get_int("seed"));
            }

            return std::make_unique<s7noise::WhiteNoiseGenerator>(amplitude, seed);
        },
        metadata.at("WhiteNoiseGenerator")
    ));

    // Register PinkNoiseGenerator
    registry->register_element(s7::ElementDescriptor(
        "PinkNoiseGenerator",
        [](const s7::ElementCreationContext& ctx) {
            double amplitude = 1.0;
            uint32_t seed = 0;

            if (ctx.has_param("amplitude")) {
                amplitude = static_cast<float>(ctx.get_real("amplitude"));
            }
            if (ctx.has_param("seed")) {
                seed = static_cast<uint32_t>(ctx.get_int("seed"));
            }

            return std::make_unique<s7noise::PinkNoiseGenerator>(amplitude, seed);
        },
        metadata.at("PinkNoiseGenerator")
    ));
}

} // extern "C"

