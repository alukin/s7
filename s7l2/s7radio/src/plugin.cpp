// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: LGPL-3.0-or-later
#include "radio.hpp"
#include <s7/element_plugin.hpp>
#include <s7/element_registry.hpp>

extern "C" const S7PluginInfo* s7_plugin_query() {
    static const S7PluginInfo info{S7_PLUGIN_API_VERSION, 3, "s7radio", "1.0.0", "S7 Project"};
    return &info;
}
extern "C" const char* s7_plugin_manifest_json() {
    return R"({"api_version":1,"plugin_name":"s7radio","plugin_version":"1.0.0","element_count":3,"elements":["FmDiscriminator","FmModulator","AmModulator"]})";
}
extern "C" void s7_register_elements(void* registry) {
    s7::radio::register_elements(*static_cast<s7::ElementRegistry*>(registry));
}
