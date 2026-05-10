//
// i_s7lib.hpp — S7 Plugin API Interface
//
// This file defines the C ABI boundary for S7 shared library plugins.
// Plugins export element libraries with custom elements that can be loaded
// and registered at runtime without recompiling the runtime.
//
// Status: experimental/internal, v0
//

#ifndef I_S7LIB_HPP
#define I_S7LIB_HPP

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

// ============================================================================
// Version structures
// ============================================================================

/**
 * @brief Semantic version representation.
 */
typedef struct {
    uint32_t major;
    uint32_t minor;
} s7_version_t;

// ============================================================================
// Key-Value pairs for plugin metadata tags
// ============================================================================

/**
 * @brief Key-value pair for plugin metadata and tags.
 */
typedef struct {
    const char* key;
    const char* value;
} s7_kv_t;

// ============================================================================
// Element descriptor structures
// ============================================================================

/**
 * @brief Element manifest entry describing one exportable element type.
 *
 * This structure provides metadata about an element that can be instantiated
 * by the runtime and displayed in the GUI palette.
 */
typedef struct {
    const char* type_id;           ///< Stable element type ID (e.g., "vendor.domain.family.element")
    const char* display_name;      ///< Human-readable display name
    const char* category;          ///< Category path (e.g., "DSP/Transforms")
    const char* summary;           ///< Short description
    const char* descriptor_json;   ///< Full element descriptor in JSON format
} s7_element_manifest_entry_t;

// ============================================================================
// Plugin manifest structures
// ============================================================================

/**
 * @brief Complete plugin manifest describing the plugin and its elements.
 *
 * This structure is returned by s7_plugin_get_manifest and provides all
 * metadata needed by the runtime to validate compatibility, display the
 * plugin in catalogs, and register its elements.
 */
typedef struct {
    const char* plugin_id;                             ///< Stable plugin identifier
    const char* plugin_name;                           ///< Human-readable plugin name
    s7_version_t plugin_version;                       ///< Semantic version of the plugin
    s7_version_t plugin_api_version;                   ///< ABI contract version
    s7_version_t manifest_version;                     ///< Descriptor schema version
    const char* vendor;                                ///< Vendor/author name
    const char* build_target;                          ///< Build target (e.g., "Linux-x86_64")
    const s7_kv_t* tags;                               ///< Optional metadata tags
    uint32_t tag_count;                                ///< Number of tags
    const s7_element_manifest_entry_t* elements;       ///< Array of element descriptors
    uint32_t element_count;                            ///< Number of elements
} s7_plugin_manifest_t;

// ============================================================================
// Registry API (runtime-provided interface to plugin)
// ============================================================================

/**
 * @brief Opaque registry API handle.
 *
 * This is provided by the runtime to the plugin during registration.
 * The plugin uses this to register element factories and report warnings.
 */
typedef struct s7_registry_api_t s7_registry_api_t;

/**
 * @brief Plugin registration context.
 *
 * This structure is provided by the runtime when calling s7_plugin_register.
 * It contains the runtime's API version and a pointer to the registry API.
 */
typedef struct {
    s7_version_t runtime_plugin_api_version;           ///< Runtime's plugin API version
    const s7_registry_api_t* registry;                 ///< Registry API interface
} s7_plugin_register_context_t;

/**
 * @brief Status/error code type.
 *
 * Plugin entry points return this to indicate success (0) or failure (non-zero).
 * Specific error codes may be defined later. Currently, 0 = success, non-zero = failure.
 */
typedef int32_t s7_status_t;

// ============================================================================
// Plugin API version
// ============================================================================

/**
 * @brief Current plugin API version.
 *
 * This should match s7_plugin_manifest_t::plugin_api_version for compatibility.
 * Major version changes indicate ABI-incompatible updates.
 * Minor version changes may be supported with explicit runtime configuration.
 */
#define S7_PLUGIN_API_VERSION_MAJOR 0
#define S7_PLUGIN_API_VERSION_MINOR 1

// ============================================================================
// Plugin entry point function pointers
// ============================================================================

/**
 * @brief Get plugin manifest.
 *
 * Called by the runtime to query the plugin's metadata and element descriptors.
 * The returned pointer must remain valid for the lifetime of the plugin.
 *
 * @return Pointer to the plugin manifest, or NULL if manifest is invalid.
 */
typedef const s7_plugin_manifest_t* (*s7_plugin_get_manifest_fn)(void);

/**
 * @brief Register plugin elements.
 *
 * Called by the runtime after validating manifest compatibility.
 * The plugin should register its element factories via the registry API.
 * The registration context is valid only during this call.
 *
 * @param ctx    Pointer to registration context (non-NULL).
 * @return       0 on success, non-zero error code on failure.
 */
typedef s7_status_t (*s7_plugin_register_fn)(const s7_plugin_register_context_t* ctx);

/**
 * @brief Optional plugin shutdown hook.
 *
 * Called by the runtime during plugin unload or runtime shutdown.
 * The plugin should clean up any resources it allocated.
 */
typedef void (*s7_plugin_shutdown_fn)(void);

// ============================================================================
// Required exported symbols
// ============================================================================

/**
 * @brief Get the plugin manifest.
 *
 * This symbol MUST be exported by every S7 plugin.
 * Name: s7_plugin_get_manifest
 */
// extern const s7_plugin_manifest_t* s7_plugin_get_manifest(void);

/**
 * @brief Register the plugin's elements.
 *
 * This symbol MUST be exported by every S7 plugin.
 * Name: s7_plugin_register
 */
// extern s7_status_t s7_plugin_register(const s7_plugin_register_context_t* ctx);

/**
 * @brief Optional plugin shutdown.
 *
 * This symbol MAY be exported by plugins that need cleanup on unload.
 * Name: s7_plugin_shutdown
 */
// extern void s7_plugin_shutdown(void);

// ============================================================================
// Registry API operations (C++ only)
// ============================================================================

#ifdef __cplusplus

namespace s7 {

/**
 * @brief C++ wrapper for the registry API.
 *
 * This is used internally by the S7 runtime to expose registry operations
 * to plugins. Plugins should not directly include this; it is documented
 * here for completeness.
 */
class IRegistryApi {
public:
    virtual ~IRegistryApi() = default;

    /**
     * @brief Register an element factory with the runtime.
     *
     * @param type_id          Stable element type identifier
     * @param descriptor_json  Full element descriptor in JSON format
     * @return                 0 on success, non-zero on error (e.g., duplicate type)
     */
    virtual s7_status_t registerElementFactory(const char* type_id,
                                                const char* descriptor_json) = 0;

    /**
     * @brief Register a warning or diagnostic message from the plugin.
     *
     * @param code             Diagnostic code (plugin-defined)
     * @param message          Human-readable message
     */
    virtual void registerPluginWarning(int32_t code, const char* message) = 0;
};

} // namespace s7

#endif // __cplusplus

#ifdef __cplusplus
}
#endif

#endif // I_S7LIB_HPP

