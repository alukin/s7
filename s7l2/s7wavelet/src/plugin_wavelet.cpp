// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: MIT-0
#include "wavelet_metadata.hpp"
#include <s7/element_plugin.hpp>
#include <s7/element_registry.hpp>
#include <s7/metadata_json.hpp>
#include <s7wavelet/coefficient_element.hpp>
extern "C" {
const S7PluginInfo *s7_plugin_query() {
    static const S7PluginInfo info{S7_PLUGIN_API_VERSION, 1, "s7wavelet", "1.0.0", "S7 Project"};
    return &info;
}
const char *s7_plugin_manifest_json() {
    return R"({"api_version":1,"plugin_name":"s7wavelet","plugin_version":"1.0.0","element_count":1,"elements":["WaveletCoefficient"]})";
}
void s7_register_elements(void *ptr) {
    auto &registry = *static_cast<s7::ElementRegistry *>(ptr);
    if (registry.find("WaveletCoefficient"))
        return;
    static const s7::ElementMetadataLibrary metadata(s7::resources::wavelet_metadata);
    registry.register_element(s7::ElementDescriptor(
        "WaveletCoefficient",
        [](const s7::ElementCreationContext &ctx) {
            return std::make_unique<s7wavelet::WaveletCoefficient>(ctx.get_real("frequency_hz"),
                                                                   ctx.get_real("cycles"));
        },
        metadata.at("WaveletCoefficient")));
}
}
