#ifndef S7CORE_INCLUDE_S7_ELEMENT_PLUGIN_HPP
#define S7CORE_INCLUDE_S7_ELEMENT_PLUGIN_HPP

#include <cstdint>

// C ABI Interface for Element Plugins
// Allows plugins to be built independently and loaded at runtime

#ifdef __cplusplus
extern "C" {
#endif

// Plugin metadata
struct S7PluginInfo {
    uint32_t api_version;           // For compatibility checking
    uint32_t element_count;         // Number of elements provided
    const char* plugin_name;        // Human-readable plugin name
    const char* plugin_version;     // Plugin version string
    const char* author;             // Plugin author
};

// Query plugin information
typedef const S7PluginInfo* (*S7PluginQueryFunc)();

// Register elements with registry
// registry_ptr is actually s7::ElementRegistry*
typedef void (*S7RegisterElementsFunc)(void* registry_ptr);

// Get plugin information
const S7PluginInfo* s7_plugin_query();

// Register all elements provided by this plugin
void s7_register_elements(void* registry_ptr);

// Plugin API version - increment on breaking changes
#define S7_PLUGIN_API_VERSION 1

#ifdef __cplusplus
}
#endif

#endif // S7CORE_INCLUDE_S7_ELEMENT_PLUGIN_HPP

