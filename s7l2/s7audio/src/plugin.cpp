// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: LGPL-3.0-or-later
#include <s7/audio.hpp>
#include <s7/element_plugin.hpp>
extern "C" const S7PluginInfo* s7_plugin_query() {
    static const S7PluginInfo info{S7_PLUGIN_API_VERSION,2,"s7audio","0.1.0","S7 Project"}; return &info;
}
extern "C" const char* s7_plugin_manifest_json() {
    return R"({"api_version":1,"plugin_name":"s7audio","plugin_version":"0.1.0","author":"S7 Project","description":"Explicitly activated PortAudio and native PipeWire boundaries","element_count":2,"elements":["AudioSource","AudioSink"]})";
}
extern "C" void s7_register_elements(void* registry) { s7::audio::register_elements(*static_cast<s7::ElementRegistry*>(registry)); }
