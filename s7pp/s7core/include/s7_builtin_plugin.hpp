//
// s7_builtin_plugin.hpp — Built-in stdlib plugin interface
//
// Exposes the built-in stdlib as a pseudo-plugin via the standard plugin ABI.
// This allows the runtime to treat built-in elements identically to
// dynamically-loaded plugin elements in the element catalog system.
//

#ifndef S7_BUILTIN_PLUGIN_HPP
#define S7_BUILTIN_PLUGIN_HPP

#include "i_s7lib.hpp"

#ifdef __cplusplus
extern "C" {
#endif

// Forward declarations of the plugin entry points
// These are defined in s7_builtin_plugin.cpp

/**
 * @brief Get the built-in stdlib plugin manifest.
 *
 * Called by the runtime to discover available built-in elements.
 * The returned pointer remains valid for the lifetime of the runtime.
 */
extern const s7_plugin_manifest_t* s7_plugin_get_manifest(void);

/**
 * @brief Register built-in stdlib elements.
 *
 * Called by the runtime after validating manifest compatibility.
 * Registers all s7stdlib elements with the element registry.
 */
extern s7_status_t s7_plugin_register(const s7_plugin_register_context_t* ctx);

/**
 * @brief Optional plugin shutdown hook.
 *
 * Called by the runtime during shutdown. No-op for built-in stdlib.
 */
extern void s7_plugin_shutdown(void);

#ifdef __cplusplus
}
#endif

#endif  // S7_BUILTIN_PLUGIN_HPP

