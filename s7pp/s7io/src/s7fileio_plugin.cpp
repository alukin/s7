//
// s7fileio_plugin.cpp — s7fileio as a pseudo-plugin inside s7io
//
// Implements the C ABI plugin interface (i_s7lib.hpp) for s7fileio.
// The runtime element catalog treats these identically to dynamic plugins.
//

#include "i_s7lib.hpp"
#include "s7factory.hpp"
#include "s7fileio.hpp"

namespace {

#define S7_ELEMENT(TYPE_ID, DISPLAY_NAME, CATEGORY, SUMMARY) \
    { #TYPE_ID, DISPLAY_NAME, CATEGORY, SUMMARY, "{}" }

static const s7_element_manifest_entry_t k_fileio_elements[] = {
    S7_ELEMENT(s7.io.file.real_sink,
               "Real File Sink",
               "I/O/File",
               "Write N-channel real stream with selectable scalar encoding (f64/f32/s32/s16/s8/u8)"),

    S7_ELEMENT(s7.io.file.real_source,
               "Real File Source",
               "I/O/File",
               "Read N-channel real stream from s7io file with scalar decoding"),

    S7_ELEMENT(s7.io.file.complex_sink,
               "Complex File Sink",
               "I/O/File",
               "Write N-channel complex IQ stream with selectable scalar encoding"),

    S7_ELEMENT(s7.io.file.complex_source,
               "Complex File Source",
               "I/O/File",
               "Read N-channel complex IQ stream from s7io file"),
};

static constexpr uint32_t k_element_count =
    sizeof(k_fileio_elements) / sizeof(k_fileio_elements[0]);

static const s7_kv_t k_tags[] = {
    {"library",     "s7io"},
    {"sub_library", "s7fileio"},
    {"capability",  "file-io"},
    {"stability",   "experimental"},
};

static const s7_plugin_manifest_t k_fileio_manifest = {
    .plugin_id           = "s7.io.file",
    .plugin_name         = "S7 File I/O (Real/Complex)",
    .plugin_version      = {.major = 0, .minor = 1},
    .plugin_api_version  = {.major = S7_PLUGIN_API_VERSION_MAJOR,
                            .minor = S7_PLUGIN_API_VERSION_MINOR},
    .manifest_version    = {.major = 0, .minor = 1},
    .vendor              = "S7 Project",
    .build_target        = "Linux-x86_64",
    .tags                = k_tags,
    .tag_count           = sizeof(k_tags) / sizeof(k_tags[0]),
    .elements            = k_fileio_elements,
    .element_count       = k_element_count,
};

}  // namespace

extern "C" {

const s7_plugin_manifest_t* s7_plugin_get_manifest_fileio(void) {
    return &k_fileio_manifest;
}

s7_status_t s7_plugin_register_fileio(const s7_plugin_register_context_t* ctx) {
    if (!ctx) return 1;
    try {
        s7::registerFileIO(s7::ElementRegistry::instance());
        return 0;
    } catch (...) {
        return 1;
    }
}

void s7_plugin_shutdown_fileio(void) {}

}  // extern "C"

