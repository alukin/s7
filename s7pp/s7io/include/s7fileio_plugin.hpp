//
// s7fileio_plugin.hpp — File I/O plugin entry points
//

#ifndef S7_FILEIO_PLUGIN_HPP
#define S7_FILEIO_PLUGIN_HPP

#include "i_s7lib.hpp"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Get the s7fileio plugin manifest.
 */
extern const s7_plugin_manifest_t* s7_plugin_get_manifest_fileio(void);

/**
 * @brief Register s7fileio elements.
 */
extern s7_status_t s7_plugin_register_fileio(const s7_plugin_register_context_t* ctx);

/**
 * @brief Optional s7fileio plugin shutdown hook.
 */
extern void s7_plugin_shutdown_fileio(void);

#ifdef __cplusplus
}
#endif

#endif  // S7_FILEIO_PLUGIN_HPP
