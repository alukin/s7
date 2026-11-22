// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include <s7/hardware.hpp>
#include <s7/hardware_live_run.hpp>
#include <s7/plugin_loader.hpp>
#include <s7/compiler.hpp>
#include <cassert>
#include <sstream>
int main(int argc,char** argv) {
    assert(argc==2 || argc==3); const bool with_sdr=argc==3;
    auto& registry=s7::ElementRegistry::instance(); std::ostringstream log;
    s7::PluginLoader loader({},log); assert(loader.load_plugin(argv[1],registry));
    if(with_sdr) assert(loader.load_plugin(argv[2],registry));
    assert(loader.loaded_count()==static_cast<std::size_t>(argc-1));
    const auto create_run = reinterpret_cast<s7::hardware::CreateLiveRun>(
        loader.loaded_symbol("s7audio", s7::hardware::live_run_symbol));
    assert(create_run);
    {
        s7::Schema schema("offline loaded audio");
        schema.add_element(s7::make_element_spec("audio",*registry.find("AudioSource"),{{"type","F32"},{"backend","not-installed"}}));
        auto graph=s7::Compiler::compile(schema); auto* b=dynamic_cast<s7::hardware::Boundary*>(graph.elements.at("audio").get());
        assert(b && b->request().backend=="not-installed" && b->request().rate==s7::Rate(48000,1));
        try {
            std::unique_ptr<s7::hardware::LiveRun> invalid(create_run(graph, s7::Rate(44100,1)));
            assert(false);
        } catch (const std::invalid_argument&) {}
        std::unique_ptr<s7::hardware::LiveRun> run(create_run(graph, s7::Rate(48000,1)));
        assert(run && run->runtime().step_count() == 0);
        try {
            run->prepare();
            assert(false);
        } catch (const std::invalid_argument& error) {
            assert(std::string(error.what()).find("not-installed") != std::string::npos);
        }
    }
    assert((registry.find("SdrSink")!=nullptr)==with_sdr);
    loader.unload_all();assert(!registry.find("AudioSource") && !registry.find("SdrSink"));
}
