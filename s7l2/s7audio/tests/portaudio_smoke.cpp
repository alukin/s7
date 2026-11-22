// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include <s7/audio.hpp>
#include <portaudio.h>
#include <cassert>
#include <cstring>
#include <iostream>
namespace s7::audio { std::unique_ptr<hardware::Provider> portaudio_provider(); }
namespace {
struct FakeStream {PaStreamCallback* callback;void* user;bool stopped=true;PaStreamInfo info;};
FakeStream* active=nullptr;int initialized=0,opened=0;bool wrong_rate=false;
FakeStream& get(PaStream* s){return *reinterpret_cast<FakeStream*>(s);}
const PaDeviceInfo device{2,"Fake audio only",0,2,2,0.001,0.001,0.01,0.01,48000};
const PaHostApiInfo host{1,paInDevelopment,"Fake host",1,0,0};
template<class F>void rejects(F f){bool bad=false;try{f();}catch(const std::exception&){bad=true;}assert(bad);}
s7::hardware::Request request(s7::hardware::Direction d){s7::hardware::Request r;r.backend="portaudio";r.direction=d;r.type=s7::SignalType::I16;r.channels={"0","1"};r.block_frames=4;r.queue_frames=8;return r;}
}
// This executable compiles the production adapter against these fake API
// functions, and never links libportaudio or opens an OS audio device.
extern "C" {
PaError Pa_Initialize(){++initialized;return paNoError;}
PaError Pa_Terminate(){--initialized;return paNoError;}
const char* Pa_GetErrorText(PaError){return "fake error";}
PaDeviceIndex Pa_GetDeviceCount(){return 1;}
PaDeviceIndex Pa_GetDefaultInputDevice(){return 0;}
PaDeviceIndex Pa_GetDefaultOutputDevice(){return 0;}
const PaDeviceInfo* Pa_GetDeviceInfo(PaDeviceIndex i){return i==0?&device:nullptr;}
const PaHostApiInfo* Pa_GetHostApiInfo(PaHostApiIndex i){return i==0?&host:nullptr;}
PaError Pa_IsFormatSupported(const PaStreamParameters* in,const PaStreamParameters* out,double rate){
    const auto* p=in?in:out;assert(p && (in==nullptr)!=(out==nullptr));
    return p->sampleFormat==paInt16 && rate==48000?paFormatIsSupported:paSampleFormatNotSupported;
}
PaError Pa_OpenStream(PaStream** result,const PaStreamParameters* in,const PaStreamParameters* out,double rate,
                      unsigned long frames,PaStreamFlags flags,PaStreamCallback* callback,void* user){
    assert((in==nullptr)!=(out==nullptr));assert(frames==4 && flags==(paClipOff|paDitherOff));
    auto* stream=new FakeStream{callback,user,true,{1,0.001,0.001,rate+(wrong_rate?1:0)}};
    *result=reinterpret_cast<PaStream*>(stream);active=stream;++opened;return paNoError;
}
PaError Pa_CloseStream(PaStream* s){if(active==&get(s))active=nullptr;delete &get(s);--opened;return paNoError;}
const PaStreamInfo* Pa_GetStreamInfo(PaStream* s){return &get(s).info;}
PaError Pa_StartStream(PaStream* s){get(s).stopped=false;return paNoError;}
PaError Pa_IsStreamStopped(PaStream* s){return get(s).stopped?1:0;}
PaError Pa_AbortStream(PaStream* s){get(s).stopped=true;return paNoError;}
}
int main(){
    using namespace s7;using namespace s7::hardware;
    auto provider=audio::portaudio_provider();assert(initialized==0);
    const auto devices=provider->devices();assert(devices.size()==1 && devices[0].capture==Support::Supported && initialized==0);
    {
        auto r=request(Direction::Capture);auto stream=provider->prepare(r);assert(initialized==1 && active->stopped);
        stream->start();assert(!active->stopped);
        std::int16_t values[]={1,2,3,4,5,6};PaStreamCallbackTimeInfo time{};
        active->callback(values,nullptr,3,&time,0,active->user);Buffer b(16);
        auto result=stream->read(b.bytes(),3,Deadline::max());assert(result.frames==3 && result.status==Status::Ok && !result.time);
        assert(std::memcmp(b.bytes().data(),values,sizeof(values))==0);
        active->callback(values,nullptr,3,&time,paInputOverflow,active->user);
        result=stream->read(b.bytes(),4,Deadline::max());assert(result.frames==0 && result.status==Status::Overflow);
    }
    assert(initialized==0 && opened==0);
    {
        auto r=request(Direction::Playback);auto stream=provider->prepare(r);assert(stream->descriptor().startup_prefill_frames==8);
        std::int16_t values[]={1,2,3,4,5,6,7,8};auto b=std::as_bytes(std::span(values));assert(stream->write(b,4,Deadline::max()).frames==4);
        stream->start();std::int16_t output[12];PaStreamCallbackTimeInfo time{};
        active->callback(nullptr,output,6,&time,0,active->user);assert(std::memcmp(output,values,sizeof(values))==0);
        for(int i=8;i<12;++i)assert(output[i]==0);
        assert(stream->emergency_output_frames()==2);
        assert(stream->write(b,4,Deadline::max()).status==Status::Underflow);
    }
    {
        auto r=request(Direction::Playback);
        r.options={{"underrun","rebuffer"}};
        auto stream=provider->prepare(r);
        std::int16_t values[]={1,2,3,4,5,6,7,8};
        auto b=std::as_bytes(std::span(values));
        assert(stream->write(b,4,Deadline::max()).frames==4);
        stream->start();
        std::int16_t output[8];
        PaStreamCallbackTimeInfo time{};
        active->callback(nullptr,output,4,&time,paOutputUnderflow,active->user);
        assert(std::memcmp(output,values,sizeof(values))==0);
        active->callback(nullptr,output,4,&time,paOutputUnderflow,active->user);
        for (const auto value:output) assert(value==0);
        assert(stream->emergency_output_frames()==4);
        assert(stream->write(b,4,Deadline::max()).status==Status::Ok);
        active->callback(nullptr,output,4,&time,0,active->user);
        assert(std::memcmp(output,values,sizeof(values))==0);
    }
    assert(initialized==0 && opened==0);wrong_rate=true;
    rejects([&]{provider->prepare(request(Direction::Capture));});assert(initialized==0 && opened==0);wrong_rate=false;
    auto bad=request(Direction::Capture);bad.channels={"1","0"};rejects([&]{provider->prepare(bad);});assert(initialized==0);
    std::cout<<"production PortAudio adapter passed fake API lifecycle, callbacks, rate readback and cleanup tests\n";
}
