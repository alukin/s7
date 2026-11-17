// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include <s7/hardware.hpp>
#include <s7/plugin_loader.hpp>
#include <s7/compiler.hpp>
#include <cassert>
#include <sstream>
int main(int argc,char** argv) {
    assert(argc==3); auto& registry=s7::ElementRegistry::instance(); std::ostringstream log;
    s7::PluginLoader loader({},log); assert(loader.load_plugin(argv[1],registry)); assert(loader.load_plugin(argv[2],registry));
    assert(loader.loaded_count()==2);
    {
        s7::Schema schema("offline loaded audio");
        schema.add_element(s7::make_element_spec("audio",*registry.find("AudioSource"),{{"type","F32"},{"backend","not-installed"}}));
        auto graph=s7::Compiler::compile(schema); auto* b=dynamic_cast<s7::hardware::Boundary*>(graph.elements.at("audio").get());
        assert(b && b->request().backend=="not-installed" && b->request().rate==s7::Rate(48000,1));
    }
    assert(registry.find("SdrSink"));loader.unload_all();assert(!registry.find("AudioSource") && !registry.find("SdrSink"));
}
