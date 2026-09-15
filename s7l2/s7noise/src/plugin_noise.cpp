#include "s7noise/noise_elements.hpp"
#include "s7/element_registry.hpp"
#include "s7/element_plugin.hpp"
#include "s7/element_metadata.hpp"

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

extern "C" {

const S7PluginInfo* s7_plugin_query() {
    return &plugin_info;
}

} // extern "C"

// ============================================================================
// Element Metadata
// ============================================================================

namespace {

s7::ElementMetadata create_white_noise_metadata() {
    s7::ElementMetadata meta;
    meta.type_name = "WhiteNoiseGenerator";
    meta.display_name = "White Noise";
    meta.description = "Generates white noise with normal (Gaussian) distribution";
    meta.category = "simulation";
    meta.version = "1.0.0";
    meta.tags = {"noise", "source", "stateful", "random"};

    meta.output_ports.push_back({
        "out", "White noise output", s7::SignalType::DOUBLE, false
    });

    meta.parameters = {
        {"amplitude", "real", "Standard deviation of noise", 1.0, 0.0, 100.0, {}, false},
        {"seed", "int", "Random seed (0 = random)", 0, 0, 2147483647, {}, false}
    };

    meta.visual = {"circle", "#FFEB3B", 100, 50, "noise"};
    return meta;
}

s7::ElementMetadata create_pink_noise_metadata() {
    s7::ElementMetadata meta;
    meta.type_name = "PinkNoiseGenerator";
    meta.display_name = "Pink Noise";
    meta.description = "Generates pink noise (1/f noise) using Voss-McCartney algorithm";
    meta.category = "simulation";
    meta.version = "1.0.0";
    meta.tags = {"noise", "source", "stateful", "random", "pink"};

    meta.output_ports.push_back({
        "out", "Pink noise output", s7::SignalType::DOUBLE, false
    });

    meta.parameters = {
        {"amplitude", "real", "Standard deviation of noise", 1.0, 0.0, 100.0, {}, false},
        {"seed", "int", "Random seed (0 = random)", 0, 0, 2147483647, {}, false}
    };

    meta.visual = {"circle", "#FFEB3B", 100, 50, "noise"};
    return meta;
}

} // anonymous namespace

// ============================================================================
// Plugin Registration
// ============================================================================

extern "C" {

void s7_register_elements(void* registry_ptr) {
    auto* registry = static_cast<s7::ElementRegistry*>(registry_ptr);

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
        create_white_noise_metadata()
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
        create_pink_noise_metadata()
    ));
}

} // extern "C"

