// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: LGPL-3.0-or-later
#include <s7/audio.hpp>
#include <s7/io.hpp>
#if defined(S7_WITH_SDR)
#include <s7/sdr.hpp>
#include "radio.hpp"
#endif
#include <s7/hardware_plugin.hpp>
#include <s7/builtin_element_registry.hpp>
#include <s7/compiler.hpp>
#include <s7/schema_library.hpp>
#include <charconv>
#include <atomic>
#include <csignal>
#include <fstream>
#include <iostream>
#include <thread>
namespace {
std::atomic<bool> interrupted{false};
static_assert(std::atomic<bool>::is_always_lock_free);
std::uint64_t positive(std::string_view s) {
    std::uint64_t n=0; auto p=std::from_chars(s.data(),s.data()+s.size(),n);
    if (p.ec!=std::errc{} || p.ptr!=s.data()+s.size() || !n) throw std::invalid_argument("--frames requires a positive integer");
    return n;
}
}
int main(int argc,char** argv) {
    try {
        std::string mode,file,backend; std::uint64_t frames=0; std::vector<std::filesystem::path> paths;
        for (int i=1;i<argc;++i) {
            const std::string arg=argv[i];
            auto value=[&]()->std::string { if (++i>=argc) throw std::invalid_argument("missing value for "+arg); return argv[i]; };
            if (arg=="--help") {
                std::cout<<"s7hw --check <schema.json>\ns7hw --run <schema.json> --frames <n> [--provider-library <path>]...\n"
                    <<"s7hw --devices <backend> [--provider-library <path>]...\n"
                    <<"--run explicitly activates capture/playback"
#if defined(S7_WITH_SDR)
                    <<"/RF TX"
#endif
                    <<". Capture can drive a lower-rate AudioSink through Resamplers.\n"
                    <<"Completion aborts the stream; playback counts mean accepted frames, not DAC presentation.\n"; return 0;
            } else if (arg=="--check" || arg=="--run" || arg=="--devices") {
                if (!mode.empty()) throw std::invalid_argument("choose one mode");
                mode=arg;
                if (arg=="--devices") backend=value(); else file=value();
            } else if (arg=="--frames") frames=positive(value());
            else if (arg=="--provider-library") paths.emplace_back(value());
            else throw std::invalid_argument("unknown argument: "+arg);
        }
        if (mode.empty() || (mode=="--run" && !frames) || (mode!="--run" && frames)) throw std::invalid_argument("see s7hw --help");
        auto& registry=s7::ElementRegistry::instance();
        s7::register_builtin_elements(registry); s7::audio::register_elements(registry);
        s7::register_io_elements(registry);
#if defined(S7_WITH_SDR)
        s7::sdr::register_elements(registry);
        s7::radio::register_elements(registry);
#endif
        std::optional<s7::ExecutableGraph> graph;
        std::uint64_t steps=frames;
        if (mode!="--devices") {
            std::ifstream input(file,std::ios::binary); if (!input) throw std::invalid_argument("cannot read schema: "+file);
            std::string json(4*1024*1024+1,'\0');
            input.read(json.data(),static_cast<std::streamsize>(json.size()));
            const auto size=static_cast<std::size_t>(input.gcount());
            if (input.bad()) throw std::runtime_error("schema read failed");
            if (size>4*1024*1024) throw std::invalid_argument("hardware schema exceeds 4 MiB");
            json.resize(size);
            const auto document = boost::json::parse(json);
            const auto* kind = document.is_object() ? document.as_object().if_contains("document_kind") : nullptr;
            const auto& payload = kind && *kind == "s7.diagram" ? document.as_object().at("schema") : document;
            auto schema = s7::SchemaLibraryLoader::build_schema(payload, registry);
            graph=s7::Compiler::compile(schema);
            const auto config=s7::hardware::graph_profile(*graph);
            if (frames % config.frames_per_step)
                throw std::invalid_argument("--frames must be a whole number of capture blocks");
            steps=frames/config.frames_per_step;
            if (mode=="--check") { std::cout<<"Graph and hardware execution profile valid; no hardware activated.\n"; return 0; }
        }
        std::vector<s7::hardware::ProviderModule> modules;
        for (const auto& path:paths) modules.emplace_back(path);
        auto resolve=[&](std::string_view name)->std::unique_ptr<s7::hardware::Provider> {
            for (const auto& b:s7::audio::backends()) if (b==name) return s7::audio::provider(name);
#if defined(S7_WITH_SDR)
            for (const auto& b:s7::sdr::backends()) if (b==name) return s7::sdr::provider(name);
#endif
            for (const auto& module:modules) if (auto p=module.create(name)) return p;
            throw std::invalid_argument("hardware backend unavailable: "+std::string(name));
        };
        if (mode=="--devices") {
            auto p=resolve(backend);
            for (const auto& d:p->devices()) std::cout<<d.backend<<"\t"<<d.selector<<"\t"<<d.label<<"\t"<<d.identity_provenance<<'\n';
            return 0;
        }
        s7::hardware::Run run(std::move(*graph),resolve);
        const auto& d=run.descriptor();
        std::cout<<"prepared backend="<<d.actual.backend<<" device="<<d.actual.device<<" rate="<<d.actual.rate.to_string()
            <<" session="<<d.session_id<<" format="<<s7::signal_type_info(d.actual.type).name<<'\n';
        for (const auto& [key,value]:d.backend_details) {
            // Requested and realized settings (ADR-028); native-format evidence too.
            if (key.starts_with("channel.")) std::cout<<key<<'='<<value<<'\n';
        }
        std::cout<<std::flush;
        std::signal(SIGINT,[](int){interrupted.store(true,std::memory_order_relaxed);});
        std::signal(SIGTERM,[](int){interrupted.store(true,std::memory_order_relaxed);});
        std::stop_source stop;
        std::jthread controller([&](std::stop_token st) {
            while (!st.stop_requested()) {
                if (interrupted.load(std::memory_order_relaxed)) { stop.request_stop(); break; }
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
        });
        const auto result=run.execute(steps,std::chrono::milliseconds(1000),stop.get_token());
        controller.request_stop();
        std::cout<<"status="<<s7::hardware::to_string(result.status)<<" graph_frames="<<result.graph_frames
                 <<" transferred_frames="<<result.transferred_frames<<" playback_frames="<<result.playback_frames
                 <<" emergency_output_frames="<<result.emergency_output_frames
                 <<" native_error="<<result.native_error<<'\n';
        return result.status==s7::hardware::Status::Ok?0:2;
    } catch (const std::exception& e) { std::cerr<<"s7hw: "<<e.what()<<'\n'; return 1; }
}
