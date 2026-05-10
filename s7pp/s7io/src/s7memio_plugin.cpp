//
// s7memio_plugin.cpp — s7memio pseudo-plugin manifest and registration
//

#include "i_s7lib.hpp"
#include "s7factory.hpp"
#include "s7memio.hpp"

namespace {

#define S7_ELEMENT(TYPE_ID, DISPLAY_NAME, CATEGORY, SUMMARY) \
    { #TYPE_ID, DISPLAY_NAME, CATEGORY, SUMMARY, "{}" }

static const s7_element_manifest_entry_t k_memio_elements[] = {
    S7_ELEMENT(s7.io.mem.real_sink,
               "Real Mem Sink",
               "I/O/SharedMemory",
               "Write real samples into local shared-memory SPSC ring with selectable scalar encoding"),

    S7_ELEMENT(s7.io.mem.real_source,
               "Real Mem Source",
               "I/O/SharedMemory",
               "Read real samples from local shared-memory SPSC ring"),

    S7_ELEMENT(s7.io.mem.complex_sink,
               "Complex Mem Sink",
               "I/O/SharedMemory",
               "Write complex IQ samples into local shared-memory SPSC ring"),

    S7_ELEMENT(s7.io.mem.complex_source,
               "Complex Mem Source",
               "I/O/SharedMemory",
               "Read complex IQ samples from local shared-memory SPSC ring"),
};

static constexpr uint32_t k_element_count =
    sizeof(k_memio_elements) / sizeof(k_memio_elements[0]);

static const s7_kv_t k_tags[] = {
    {"library", "s7io"},
    {"sub_library", "s7memio"},
    {"capability", "shared-memory"},
    {"stability", "experimental"},
};

static const s7_plugin_manifest_t k_memio_manifest = {
    .plugin_id = "s7.io.mem",
    .plugin_name = "S7 Shared Memory I/O",
    .plugin_version = {.major = 0, .minor = 1},
    .plugin_api_version = {.major = S7_PLUGIN_API_VERSION_MAJOR,
                           .minor = S7_PLUGIN_API_VERSION_MINOR},
    .manifest_version = {.major = 0, .minor = 1},
    .vendor = "S7 Project",
    .build_target = "Linux-x86_64",
    .tags = k_tags,
    .tag_count = sizeof(k_tags) / sizeof(k_tags[0]),
    .elements = k_memio_elements,
    .element_count = k_element_count,
};

} // namespace

extern "C" {

const s7_plugin_manifest_t* s7_plugin_get_manifest_memio(void)
{
    return &k_memio_manifest;
}

s7_status_t s7_plugin_register_memio(const s7_plugin_register_context_t* ctx)
{
    if (!ctx) return 1;
    try {
        s7::registerMemIO(s7::ElementRegistry::instance());
        return 0;
    } catch (...) {
        return 1;
    }
}

void s7_plugin_shutdown_memio(void) {}

} // extern "C"


