// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include <s7/audio.hpp>
#include <pipewire/pipewire.h>
#include <spa/param/audio/format-utils.h>
#include <spa/buffer/meta.h>
#include <cassert>
#include <cerrno>
#include <iostream>
#include <map>
namespace s7::audio {std::unique_ptr<hardware::Provider> pipewire_provider();}
namespace {
struct FakeStream {
    const pw_stream_events* events;void* user;bool active=false;
    float samples[16]{};spa_chunk chunk{};spa_data data{};spa_meta_header header{};spa_meta meta{};spa_buffer spa{};pw_buffer buffer{};
    FakeStream(const pw_stream_events* e,void* u):events(e),user(u) {
        data.data=samples;data.maxsize=sizeof(samples);data.chunk=&chunk;spa.n_datas=1;spa.datas=&data;
        meta.type=SPA_META_Header;meta.size=sizeof(header);meta.data=&header;spa.n_metas=1;spa.metas=&meta;buffer.buffer=&spa;
    }
};
FakeStream* current=nullptr;int loops=0,streams=0,queued=0;bool wrong_format=false;
std::map<std::string,std::string> properties;
FakeStream& get(pw_stream* p){return *reinterpret_cast<FakeStream*>(p);}
template<class F>void rejects(F f){bool bad=false;try{f();}catch(const std::exception&){bad=true;}assert(bad);}
s7::hardware::Request request(s7::hardware::Direction d) {
    s7::hardware::Request r;r.backend="pipewire";r.direction=d;r.channels={"0","1"};r.block_frames=4;r.queue_frames=8;return r;
}
}
// Production adapter, fake PipeWire C API. This executable has no libpipewire
// dependency and cannot connect to the user's daemon/audio hardware.
extern "C" {
void pw_init(int*,char***){}
pw_thread_loop* pw_thread_loop_new(const char*,const spa_dict*) {++loops;return reinterpret_cast<pw_thread_loop*>(new int);}
void pw_thread_loop_destroy(pw_thread_loop* p){delete reinterpret_cast<int*>(p);--loops;}
pw_loop* pw_thread_loop_get_loop(pw_thread_loop* p){return reinterpret_cast<pw_loop*>(p);}
int pw_thread_loop_start(pw_thread_loop*){return 0;}
void pw_thread_loop_stop(pw_thread_loop*){}
void pw_thread_loop_lock(pw_thread_loop*){}
void pw_thread_loop_unlock(pw_thread_loop*){}
void pw_thread_loop_signal(pw_thread_loop*,bool){}
int pw_thread_loop_get_time(pw_thread_loop*,timespec*,int64_t){return 0;}
int pw_thread_loop_timed_wait_full(pw_thread_loop*,const timespec*){return -ETIMEDOUT;}
pw_properties* pw_properties_new(const char*,...){properties.clear();return reinterpret_cast<pw_properties*>(new int);}
int pw_properties_set(pw_properties*,const char* key,const char* value){properties[key]=value;return 0;}
pw_stream* pw_stream_new_simple(pw_loop*,const char*,pw_properties* props,const pw_stream_events* events,void* user){
    delete reinterpret_cast<int*>(props);current=new FakeStream(events,user);++streams;return reinterpret_cast<pw_stream*>(current);
}
int pw_stream_connect(pw_stream* p,pw_direction,uint32_t,pw_stream_flags flags,const spa_pod**,uint32_t){
    assert(flags&PW_STREAM_FLAG_INACTIVE);assert(flags&PW_STREAM_FLAG_RT_PROCESS);
    auto& s=get(p);s.events->state_changed(s.user,PW_STREAM_STATE_CONNECTING,PW_STREAM_STATE_PAUSED,nullptr);
    std::uint8_t bytes[1024];auto builder=SPA_POD_BUILDER_INIT(bytes,sizeof(bytes));spa_audio_info_raw raw{};
    raw.format=SPA_AUDIO_FORMAT_F32;raw.channels=2;raw.rate=wrong_format?44100:48000;raw.flags=SPA_AUDIO_FLAG_UNPOSITIONED;
    s.events->param_changed(s.user,SPA_PARAM_Format,spa_format_audio_raw_build(&builder,SPA_PARAM_Format,&raw));return 0;
}
void pw_stream_destroy(pw_stream* p){if(current==&get(p))current=nullptr;delete &get(p);--streams;}
int pw_stream_set_active(pw_stream* p,bool active){auto& s=get(p);s.active=active;return 0;}
pw_buffer* pw_stream_dequeue_buffer(pw_stream* p){return &get(p).buffer;}
int pw_stream_queue_buffer(pw_stream*,pw_buffer*){++queued;return 0;}
}
int main(){
    using namespace s7;using namespace s7::hardware;
    auto provider=audio::pipewire_provider();assert(loops==0 && streams==0);
    {
        auto r=request(Direction::Capture);r.device="named-monitor";r.options={{"capture_sink","true"}};
        auto stream=provider->prepare(r);
        assert(properties.at(PW_KEY_STREAM_CAPTURE_SINK)=="true");
        assert(properties.at(PW_KEY_TARGET_OBJECT)=="named-monitor");
        assert(properties.at("node.dont-fallback")=="true" && properties.at(PW_KEY_NODE_DONT_RECONNECT)=="true");
    }
    {
        auto stream=provider->prepare(request(Direction::Capture));assert(!current->active);stream->start();assert(current->active);
        for(int i=0;i<16;++i)current->samples[i]=static_cast<float>(i);
        current->header.flags=SPA_META_HEADER_FLAG_DISCONT; // Expected new-segment marker on first buffer.
        current->chunk.offset=4;current->chunk.size=24;current->chunk.stride=8;current->events->process(current->user);
        Buffer buffer(32);auto result=stream->read(buffer.bytes(),3,Deadline::max());assert(result.frames==3 && result.status==Status::Ok);
        float values[6];std::memcpy(values,buffer.bytes().data(),sizeof(values));for(int i=0;i<6;++i)assert(values[i]==i+1);
        current->header.flags=SPA_META_HEADER_FLAG_DISCONT;current->events->process(current->user);
        assert(stream->read(buffer.bytes(),4,Deadline::max()).status==Status::ContinuityLost);
    }
    assert(loops==0 && streams==0);
    {
        auto stream=provider->prepare(request(Direction::Capture));stream->start();Buffer b(32);
        current->header.flags=SPA_META_HEADER_FLAG_GAP;current->chunk.flags=SPA_CHUNK_FLAG_EMPTY;
        current->chunk.size=32;current->chunk.stride=8;current->data.data=nullptr;
        current->events->process(current->user);
        auto result=stream->read(b.bytes(),4,Deadline::max());assert(result.status==Status::Ok && result.frames==4);
        for(auto value:b.bytes())assert(value==std::byte{});
    }
    {
        auto stream=provider->prepare(request(Direction::Capture));stream->start();Buffer b(32);
        current->chunk.offset=40;current->chunk.size=32;current->chunk.stride=8;current->events->process(current->user);
        assert(stream->read(b.bytes(),4,Deadline::max()).status==Status::BackendError);
    }
    {
        auto stream=provider->prepare(request(Direction::Playback));float values[]={1,2,3,4,5,6};
        assert(stream->write(std::as_bytes(std::span(values)),3,Deadline::max()).frames==3);stream->start();
        current->buffer.requested=4;current->data.maxsize=32;current->events->process(current->user);
        for(int i=0;i<6;++i)assert(current->samples[i]==values[i]);
        assert(current->samples[6]==0 && current->samples[7]==0);
        assert(current->chunk.size==32 && current->chunk.stride==8 && stream->emergency_output_frames()==1);
        assert(stream->write(std::as_bytes(std::span(values)),3,Deadline::max()).status==Status::Underflow);
    }
    {
        auto r=request(Direction::Playback);
        r.options={{"underrun","rebuffer"}};
        auto stream=provider->prepare(r);
        const float values[]={1,2,3,4,5,6,7,8};
        const auto stereo=std::span(values);
        assert(stream->write(std::as_bytes(stereo.first(2)),1,Deadline::max()).status==Status::Ok);
        stream->start();
        current->buffer.requested=4;
        const auto process=[] { current->events->process(current->user); };
        process();
        assert(current->samples[0]==1 && current->samples[1]==2 && current->samples[2]==0);
        assert(stream->emergency_output_frames()==3);
        assert(stream->write(std::as_bytes(stereo.first(6)),3,Deadline::max()).status==Status::Ok);
        process();
        for (int i=0;i<8;++i) assert(current->samples[i]==0);
        assert(stream->emergency_output_frames()==7);
        assert(stream->write(std::as_bytes(stereo.last(2)),1,Deadline::max()).status==Status::Ok);
        process();
        for (int i=0;i<8;++i) assert(current->samples[i]==values[i]);
        assert(stream->emergency_output_frames()==7);
        current->events->state_changed(current->user,PW_STREAM_STATE_STREAMING,PW_STREAM_STATE_UNCONNECTED,"fake disconnect");
        assert(stream->write(std::as_bytes(stereo),4,Deadline::max()).status==Status::Disconnected);
    }
    {
        auto stream=provider->prepare(request(Direction::Capture));stream->start();Buffer b(32);
        current->events->state_changed(current->user,PW_STREAM_STATE_STREAMING,PW_STREAM_STATE_UNCONNECTED,"fake disconnect");
        assert(stream->read(b.bytes(),4,Deadline::max()).status==Status::Disconnected);
    }
    assert(loops==0 && streams==0);wrong_format=true;rejects([&]{provider->prepare(request(Direction::Capture));});
    auto capture=request(Direction::Capture);
    capture.options={{"underrun","rebuffer"}};
    rejects([&]{provider->prepare(capture);});
    assert(loops==0 && streams==0 && queued>=4);
    std::cout<<"production PipeWire adapter passed fake API negotiation, buffer bounds, discontinuity, output and cleanup tests\n";
}
