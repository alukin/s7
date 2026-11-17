// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later
// Opt-in laboratory probe for the production provider API. Not registered with
// CTest: requests explicitly open/activate devices. This is not a graph runner
// or a new S7 network protocol. Raw stdin/stdout may be carried by an SSH pipe.
#include <s7/audio.hpp>
#include <s7/sdr.hpp>
#include <s7/hardware_plugin.hpp>
#include <s7/float_types.hpp>
#include <boost/json.hpp>
#include <algorithm>
#include <atomic>
#include <charconv>
#include <csignal>
#include <cstring>
#include <fstream>
#include <filesystem>
#include <iostream>
#include <thread>

namespace hw=s7::hardware;
namespace {
std::atomic<bool> interrupted{};
static_assert(std::atomic<bool>::is_always_lock_free);
std::uint64_t integer(std::string_view s) {
    std::uint64_t n=0;const auto p=std::from_chars(s.data(),s.data()+s.size(),n);
    if(p.ec!=std::errc{} || p.ptr!=s.data()+s.size())throw std::invalid_argument("invalid integer argument");
    return n;
}
hw::Request request(const std::string& path) {
    std::ifstream f(path);if(!f)throw std::invalid_argument("cannot open request");
    std::string json(65537,'\0');f.read(json.data(),static_cast<std::streamsize>(json.size()));
    if(f.gcount()>65536)throw std::invalid_argument("request exceeds 64 KiB");
    json.resize(static_cast<std::size_t>(f.gcount()));
    const auto root=boost::json::parse(json).as_object();hw::Request r;
    r.backend=std::string(root.at("backend").as_string());r.device=std::string(root.at("device").as_string());
    const auto direction=root.at("direction").as_string();
    if(direction!="capture" && direction!="playback")throw std::invalid_argument("direction must be capture or playback");
    r.direction=direction=="capture"?hw::Direction::Capture:hw::Direction::Playback;
    const auto type=root.at("type").as_string();
    if(type=="F32")r.type=s7::SignalType::F32;else if(type=="I16")r.type=s7::SignalType::I16;
    else if(type=="I32")r.type=s7::SignalType::I32;else if(type=="C32")r.type=s7::SignalType::C32;
    else if(type=="CI12")r.type=s7::SignalType::CI12;else if(type=="CI16")r.type=s7::SignalType::CI16;
    else throw std::invalid_argument("unsupported probe type");
    const auto& rate=root.at("rate").as_array();
    if(rate.size()!=2)throw std::invalid_argument("rate must have two positive integers");
    auto number=[](const boost::json::value& v)->std::uint64_t {
        if(v.is_uint64())return v.as_uint64();
        if(v.is_int64() && v.as_int64()>0)return static_cast<std::uint64_t>(v.as_int64());
        throw std::invalid_argument("invalid positive integer");
    };
    r.rate=s7::Rate::reduced(number(rate[0]),number(rate[1]));r.channels.clear();
    for(const auto& c:root.at("channels").as_array())r.channels.emplace_back(c.as_string());
    r.block_frames=number(root.at("block_frames"));r.queue_frames=number(root.at("queue_frames"));
    if(auto* opts=root.if_contains("options"))for(const auto& kv:opts->as_object())r.options.emplace(std::string(kv.key()),std::string(kv.value().as_string()));
    r.validate();return r;
}
}
int main(int argc,char** argv) {
    try {
        std::string req_path,output,input,summary_path,module_path,start_gate,ready_file;
        std::uint64_t frames=0,cancel_ms=0,stall_at=0,stall_ms=0;double tone=0,amplitude=0.125;
        bool token_only=false;
        for(int i=1;i<argc;++i) {
            const std::string arg=argv[i];auto value=[&](){if(++i>=argc)throw std::invalid_argument("missing option value");return std::string(argv[i]);};
            if(arg=="--request")req_path=value();else if(arg=="--frames")frames=integer(value());
            else if(arg=="--output")output=value();else if(arg=="--input")input=value();
            else if(arg=="--summary")summary_path=value();else if(arg=="--provider-library")module_path=value();
            else if(arg=="--cancel-after-ms")cancel_ms=integer(value());else if(arg=="--stall-after-frames")stall_at=integer(value());
            else if(arg=="--stall-ms")stall_ms=integer(value());else if(arg=="--tone-hz")tone=std::stod(value());
            else if(arg=="--amplitude")amplitude=std::stod(value());
            else if(arg=="--cancel-token-only")token_only=true;
            else if(arg=="--start-gate")start_gate=value();else if(arg=="--ready-file")ready_file=value();
            else throw std::invalid_argument("unknown argument: "+arg);
        }
        if(req_path.empty() || !frames)throw std::invalid_argument("need --request and positive --frames");
        auto r=request(req_path);
        if(!std::isfinite(tone) || !std::isfinite(amplitude) || amplitude<0 || amplitude>=1 || std::abs(tone)>=r.rate.to_hz()/2)
            throw std::invalid_argument("invalid tone/amplitude");
        std::optional<hw::ProviderModule> module;
        if(!module_path.empty())module.emplace(module_path);
        std::unique_ptr<hw::Provider> provider;
        if(module)provider=module->create(r.backend);
        if(!provider && (r.backend=="pipewire" || r.backend=="portaudio"))provider=s7::audio::provider(r.backend);
        if(!provider && r.backend=="soapy")provider=s7::sdr::provider(r.backend);
        if(!provider)throw std::invalid_argument("unavailable backend");
        std::ifstream file_in;std::ofstream file_out;
        std::istream* in=nullptr;std::ostream* out=nullptr;
        if(!input.empty()){if(input=="-")in=&std::cin;else{file_in.open(input,std::ios::binary);if(!file_in)throw std::runtime_error("input open failed");in=&file_in;}}
        if(!output.empty()){if(output=="-")out=&std::cout;else{file_out.open(output,std::ios::binary);if(!file_out)throw std::runtime_error("output open failed");out=&file_out;}}
        auto stream=provider->prepare(r);hw::Buffer block(r.block_frames*r.frame_bytes());
        std::signal(SIGINT,[](int){interrupted.store(true);});std::signal(SIGTERM,[](int){interrupted.store(true);});
        if(!ready_file.empty()){std::ofstream ready(ready_file);ready<<"prepared\n";if(!ready)throw std::runtime_error("ready file write failed");}
        if(!start_gate.empty()) {
            const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(15);
            while(!std::filesystem::exists(start_gate)) {
                if(interrupted.load() || std::chrono::steady_clock::now()>=deadline)throw std::runtime_error("start gate cancelled or timed out");
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        }
        const auto begin=std::chrono::steady_clock::now();std::atomic<std::int64_t> cancel_ns{-1};
        std::stop_source stop;
        std::jthread controller([&](std::stop_token st){
            while(!st.stop_requested()) {
                const auto now=std::chrono::steady_clock::now();
                if(interrupted.load() || (cancel_ms && now-begin>=std::chrono::milliseconds(cancel_ms))) {
                    cancel_ns.store(std::chrono::duration_cast<std::chrono::nanoseconds>(now-begin).count());stop.request_stop();if(!token_only)stream->cancel();return;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        });
        std::uint64_t done=0,blocks=0;std::size_t prefilled=0;bool started=false,stalled=false;
        std::int64_t first_transfer_ns=0;std::size_t first_transfer_frames=0;
        hw::Status status=hw::Status::Ok;int native=0;double minimum=0,maximum=0;bool first=true;std::uint64_t nonfinite=0;
        const auto component_size=r.type==s7::SignalType::F32 || r.type==s7::SignalType::C32 || r.type==s7::SignalType::I32?4u:2u;
        auto observe=[&](std::span<const std::byte> b,std::size_t n){
            const auto count=n*r.frame_bytes()/component_size;
            for(std::size_t i=0;i<count;++i){double v;
                if(r.type==s7::SignalType::F32 || r.type==s7::SignalType::C32){float value;std::memcpy(&value,b.data()+i*4,4);v=value;}
                else if(component_size==4){std::int32_t value;std::memcpy(&value,b.data()+i*4,4);v=value;}
                else{std::int16_t value;std::memcpy(&value,b.data()+i*2,2);v=value;}
                if(!std::isfinite(v)){++nonfinite;continue;}if(first){minimum=maximum=v;first=false;}else{minimum=std::min(minimum,v);maximum=std::max(maximum,v);}
            }
        };
        auto activate=[&]{stream->start();started=true;std::cerr<<"probe.active "<<r.backend<<" "<<r.device<<'\n';};
        if(r.direction==hw::Direction::Capture)activate();
        while(done<frames && !stop.stop_requested()) {
            if(stall_ms && !stalled && done>=stall_at){stalled=true;std::this_thread::sleep_for(std::chrono::milliseconds(stall_ms));}
            auto n=static_cast<std::size_t>(std::min<std::uint64_t>(r.block_frames,frames-done));
            if(r.direction==hw::Direction::Playback && !started)n=std::min(n,stream->descriptor().startup_prefill_frames-prefilled);
            auto bytes=block.bytes().first(n*r.frame_bytes());
            if(r.direction==hw::Direction::Capture) {
                auto result=stream->read(bytes,n,std::chrono::steady_clock::now()+std::chrono::seconds(2),stop.get_token());
                if(result.frames && !first_transfer_frames){first_transfer_ns=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();first_transfer_frames=result.frames;}
                if(result.first_frame!=done)throw std::runtime_error("software position mismatch");
                observe(bytes,result.frames);if(out && result.frames)out->write(reinterpret_cast<const char*>(bytes.data()),static_cast<std::streamsize>(result.frames*r.frame_bytes()));
                if(out && !*out)throw std::runtime_error("output write failed");
                done+=result.frames;++blocks;
                if(result.status!=hw::Status::Ok || !result.frames){status=result.status==hw::Status::Ok?hw::Status::BackendError:result.status;native=result.native_error;break;}
            } else {
                if(in){in->read(reinterpret_cast<char*>(bytes.data()),static_cast<std::streamsize>(bytes.size()));if(static_cast<std::size_t>(in->gcount())!=bytes.size())throw std::runtime_error("input ended before requested frame count");}
                else {
                    if(r.type!=s7::SignalType::C32 && r.type!=s7::SignalType::F32)throw std::invalid_argument("generated probe tone needs C32/F32");
                    for(std::size_t i=0;i<n;++i){const double phase=6.2831853071795864769*tone*static_cast<double>(done+i)/r.rate.to_hz();
                        const float re=static_cast<float>(amplitude*s7::math::cos(phase)),im=static_cast<float>(amplitude*s7::math::sin(phase));
                        for(std::size_t c=0;c<r.channels.size();++c){auto* p=bytes.data()+(i*r.channels.size()+c)*(r.type==s7::SignalType::C32?8:4);std::memcpy(p,&re,4);if(r.type==s7::SignalType::C32)std::memcpy(p+4,&im,4);}
                    }
                }
                observe(bytes,n);std::size_t sent=0;const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(2);
                while(sent<n){auto result=stream->write(bytes.subspan(sent*r.frame_bytes()),n-sent,deadline,stop.get_token());
                    if(result.first_frame!=done)throw std::runtime_error("software position mismatch");
                    done+=result.frames;sent+=result.frames;
                    if(result.status!=hw::Status::Ok || !result.frames){status=result.status==hw::Status::Ok?hw::Status::BackendError:result.status;native=result.native_error;break;}
                }
                ++blocks;if(status!=hw::Status::Ok)break;
                if(!started){prefilled+=sent;if(prefilled==stream->descriptor().startup_prefill_frames || done==frames)activate();}
            }
        }
        const auto returned=std::chrono::steady_clock::now();controller.request_stop();controller.join();stream->stop();
        if(stop.stop_requested())status=hw::Status::Cancelled;
        const double elapsed=std::chrono::duration<double>(returned-begin).count();
        boost::json::object result{{"backend",r.backend},{"device",r.device},{"type",s7::signal_type_info(r.type).name},
            {"channels",r.channels.size()},{"frames",done},{"blocks",blocks},{"status",hw::to_string(status)},
            {"native_error",native},{"elapsed_seconds",elapsed},{"frames_per_second",done/elapsed},
            {"minimum",minimum},{"maximum",maximum},{"nonfinite",nonfinite},{"emergency_output_frames",stream->emergency_output_frames()}};
        result["first_transfer_steady_ns"]=first_transfer_ns;result["first_transfer_frames"]=first_transfer_frames;
        result["first_transfer_evidence"]="host steady-clock read completion; not hardware acquisition time";
        if(cancel_ns.load()>=0)result["cancel_return_ms"]=(std::chrono::duration<double,std::milli>(returned-begin).count()-static_cast<double>(cancel_ns.load())/1e6);
        result["details"]=boost::json::value_from(stream->descriptor().backend_details);
        const auto text=boost::json::serialize(result)+"\n";
        if(!summary_path.empty()){std::ofstream file(summary_path);file<<text;if(!file)throw std::runtime_error("summary write failed");}
        std::cerr<<text;return status==hw::Status::Ok?0:2;
    }catch(const std::exception& e){std::cerr<<"probe.error "<<e.what()<<'\n';return 1;}
}
