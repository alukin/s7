//
// s7_builtin_plugin.cpp — Built-in stdlib as a pseudo-plugin
//
// This file implements the C ABI plugin interface (i_s7lib.hpp) for the
// s7stdlib library. This allows the runtime's element catalog system to
// treat built-in stdlib elements identically to dynamically-loaded plugins.
//
// The s7stdlib library is statically linked, but this interface allows it to
// participate in the same registration flow as external plugins.
//

#include "i_s7lib.hpp"
#include "s7factory.hpp"
#include "s7stdlib.hpp"

#include <cstring>
#include <vector>

namespace {

// ============================================================================
// Element manifest entries for built-in elements
// ============================================================================

#define S7_ELEMENT(TYPE_ID, DISPLAY_NAME, CATEGORY, SUMMARY) \
    { #TYPE_ID, DISPLAY_NAME, CATEGORY, SUMMARY, "{}" }

// Array of element descriptors for s7stdlib
static const s7_element_manifest_entry_t k_s7stdlib_elements[] = {
    S7_ELEMENT(s7.core.math.constant,
               "Constant",
               "Sources",
               "Emits a fixed scalar value (Real or Complex)"),

    S7_ELEMENT(s7.core.dsp.impulse,
               "Impulse Source",
               "Sources",
               "Emits a one-sample impulse then zeros"),

    S7_ELEMENT(s7.core.math.integrator,
               "Integrator",
               "Control/Dynamics",
               "Stateful accumulator: state(n+1) = state(n) + input(n)"),

    S7_ELEMENT(s7.core.math.sum,
               "Sum",
               "Math",
               "Weighted sum: output = sum_i(weight_i * input_i)"),

    S7_ELEMENT(s7.core.type.to_complex,
               "To Complex",
               "Type Conversion",
               "Combines Real inputs into Complex output"),

    S7_ELEMENT(s7.core.type.to_reals,
               "To Reals",
               "Type Conversion",
               "Splits Complex input into separate Real outputs"),

    S7_ELEMENT(s7.core.analog.vca,
               "VCA",
               "Analog/Control",
               "Voltage-Controlled Amplifier: output = signal * gain"),

    S7_ELEMENT(s7.core.dsp.fir_filter,
               "FIR Filter",
               "Filters",
               "Finite Impulse Response filter"),

    S7_ELEMENT(s7.core.dsp.iir_filter,
               "IIR Filter",
               "Filters",
               "Infinite Impulse Response filter"),

    S7_ELEMENT(s7.core.osc.digital,
               "Digital Oscillator",
               "Oscillators",
               "Digitally-synthesized oscillator using pole-placement"),

    S7_ELEMENT(s7.core.osc.harmonic,
               "Harmonic Oscillator",
               "Oscillators",
               "Analytical harmonic oscillator"),

    S7_ELEMENT(s7.core.analog.vco,
               "VCO",
               "Oscillators",
               "Voltage-Controlled Oscillator"),

    S7_ELEMENT(s7.core.analysis.energy_difference,
               "Energy Difference",
               "Analysis",
               "Computes squared difference between two complex signals"),
};

static constexpr uint32_t k_element_count =
    sizeof(k_s7stdlib_elements) / sizeof(k_s7stdlib_elements[0]);

// Plugin metadata tags
static const s7_kv_t k_plugin_tags[] = {
    {"library_type", "builtin"},
    {"domain", "core"},
    {"stability", "stable"},
};

static constexpr uint32_t k_tag_count =
    sizeof(k_plugin_tags) / sizeof(k_plugin_tags[0]);

// Plugin manifest static instance
static const s7_plugin_manifest_t k_s7stdlib_manifest = {
    .plugin_id = "s7.core.stdlib",
    .plugin_name = "S7 Core Standard Library",
    .plugin_version = {.major = 0, .minor = 1},
    .plugin_api_version = {.major = S7_PLUGIN_API_VERSION_MAJOR,
                           .minor = S7_PLUGIN_API_VERSION_MINOR},
    .manifest_version = {.major = 0, .minor = 1},
    .vendor = "S7 Project",
    .build_target = "Linux-x86_64",  // TODO: make platform-aware
    .tags = k_plugin_tags,
    .tag_count = k_tag_count,
    .elements = k_s7stdlib_elements,
    .element_count = k_element_count,
};

// ============================================================================
// Adapter class: C++ registry API  -> C ABI registry API
// ============================================================================

/**
 * Implements the registry API for the plugin system.
 * Wraps the underlying ElementRegistry to interface with the C ABI.
 */
class RegistryApiAdapter : public s7::IRegistryApi {
public:
    explicit RegistryApiAdapter(s7::ElementRegistry& registry)
        : registry_(registry) {}

    s7_status_t registerElementFactory(const char* type_id,
                                       const char* descriptor_json) override {
        if (!type_id || !descriptor_json) {
            return 1;  // Error: null pointer
        }

        try {
            // For the built-in plugin, we delegate to registerBuiltins
            // which has already registered all elements.
            // This is a no-op for the built-in case since registerBuiltins
            // is called separately. External plugins would use this differently.
            return 0;  // Success
        } catch (...) {
            return 1;  // Error
        }
    }

    void registerPluginWarning(int32_t code, const char* message) override {
        if (message) {
            // Log plugin warning (not implemented yet)
        }
    }

private:
    s7::ElementRegistry& registry_;
};

}  // namespace

// ============================================================================
// Plugin ABI entry points (C linkage)
// ============================================================================

extern "C" {

/**
 * Return the built-in stdlib plugin manifest.
 * This is called by the runtime to discover what elements are available.
 */
const s7_plugin_manifest_t* s7_plugin_get_manifest(void) {
    return &k_s7stdlib_manifest;
}

/**
 * Register the built-in stdlib elements.
 * This is called by the runtime after validating manifest compatibility.
 */
s7_status_t s7_plugin_register(const s7_plugin_register_context_t* ctx) {
    if (!ctx || !ctx->registry) {
        return 1;  // Error: invalid context
    }

    try {
        // Get the global element registry
        auto& registry = s7::ElementRegistry::instance();

        // Register all built-in elements
        // (This should have been done earlier via registerBuiltins(),
        //  but we could also do it here for completeness)
        s7::registerBuiltins(registry);

        return 0;  // Success
    } catch (...) {
        return 1;  // Error
    }
}

/**
 * Optional: plugin shutdown hook.
 * Not needed for built-in stdlib, but included for completeness.
 */
void s7_plugin_shutdown(void) {
    // Built-in plugins don't allocate dynamic resources,
    // so there's nothing to clean up here.
}

}  // extern "C"

