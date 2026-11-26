// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: LGPL-3.0-or-later
#include <s7/sdr.hpp>
#include <s7/element_plugin.hpp>
extern "C" const S7PluginInfo* s7_plugin_query() {
    static const S7PluginInfo info{S7_PLUGIN_API_VERSION,2,"s7sdr","0.1.0","S7 Project"}; return &info;
}
extern "C" const char* s7_plugin_manifest_json() {
    return R"({"plugin_name":"s7sdr","plugin_version":"0.1.0","author":"S7 Project","description":"Explicitly activated SDR boundaries and optional SoapySDR provider","element_count":2,"elements":["SdrSource","SdrSink"]})";
}
extern "C" void s7_register_elements(void* registry) { s7::sdr::register_elements(*static_cast<s7::ElementRegistry*>(registry)); }
