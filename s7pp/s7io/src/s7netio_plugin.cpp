//
// s7netio_plugin.cpp — s7netio pseudo-plugin manifest and registration
//

#include "i_s7lib.hpp"
#include "s7factory.hpp"
#include "s7netio.hpp"

namespace {

#define S7_ELEMENT(TYPE_ID, DISPLAY_NAME, CATEGORY, SUMMARY) \
    { #TYPE_ID, DISPLAY_NAME, CATEGORY, SUMMARY, "{}" }

static const s7_element_manifest_entry_t k_netio_elements[] = {
    S7_ELEMENT(s7.io.net.real_sink,
               "Real Net Sink",
               "I/O/Network",
               "Publish real samples into the S7 net stream transport (RSocket channel model)"),

    S7_ELEMENT(s7.io.net.real_source,
               "Real Net Source",
               "I/O/Network",
               "Consume real samples from the S7 net stream transport"),

    S7_ELEMENT(s7.io.net.complex_sink,
               "Complex Net Sink",
               "I/O/Network",
               "Publish complex IQ samples into the S7 net stream transport"),

    S7_ELEMENT(s7.io.net.complex_source,
               "Complex Net Source",
               "I/O/Network",
               "Consume complex IQ samples from the S7 net stream transport"),
};

static constexpr uint32_t k_element_count =
    sizeof(k_netio_elements) / sizeof(k_netio_elements[0]);

static const s7_kv_t k_tags[] = {
    {"library", "s7io"},
    {"sub_library", "s7netio"},
    {"capability", "network-stream"},
    {"stability", "experimental"},
};

static const s7_plugin_manifest_t k_netio_manifest = {
    .plugin_id = "s7.io.net",
    .plugin_name = "S7 Network I/O",
    .plugin_version = {.major = 0, .minor = 1},
    .plugin_api_version = {.major = S7_PLUGIN_API_VERSION_MAJOR,
                           .minor = S7_PLUGIN_API_VERSION_MINOR},
    .manifest_version = {.major = 0, .minor = 1},
    .vendor = "S7 Project",
    .build_target = "Linux-x86_64",
    .tags = k_tags,
    .tag_count = sizeof(k_tags) / sizeof(k_tags[0]),
    .elements = k_netio_elements,
    .element_count = k_element_count,
};

} // namespace

extern "C" {

const s7_plugin_manifest_t* s7_plugin_get_manifest_netio(void)
{
    return &k_netio_manifest;
}

s7_status_t s7_plugin_register_netio(const s7_plugin_register_context_t* ctx)
{
    if (!ctx) return 1;
    try {
        s7::registerNetIO(s7::ElementRegistry::instance());
        return 0;
    } catch (...) {
        return 1;
    }
}

void s7_plugin_shutdown_netio(void) {}

} // extern "C"

