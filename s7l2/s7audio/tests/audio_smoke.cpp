// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include <s7/audio.hpp>
#include "callback_stream.hpp"
#include <cassert>
#include <iostream>
using namespace s7;
using namespace s7::hardware;
using namespace s7::audio::detail;
namespace {
Request request(Direction d) { Request r; r.backend="mock-audio"; r.direction=d; r.type=SignalType::I32; r.block_frames=4; r.queue_frames=4; return r; }
class Audio final : public CallbackStream {
public:
    explicit Audio(Request r):CallbackStream(audio_descriptor(r)){}
    ~Audio() override { stop(); }
    void do_start() override {}
    void do_stop() noexcept override {}
};
}
int main() {
    { auto r=request(Direction::Capture); Audio stream(r); stream.start();
      std::int32_t values[]={10,11,12,13,14,15}; stream.capture(values,6);
      Buffer b(4*sizeof(std::int32_t)); auto result=stream.read(b.bytes(),4,Deadline::max()); assert(result.frames==4 && result.status==Status::Ok);
      assert(std::memcmp(b.bytes().data(),values,4*sizeof(std::int32_t))==0);
      result=stream.read(b.bytes(),4,Deadline::max()); assert(result.frames==0 && result.status==Status::Overflow); }
    { auto r=request(Direction::Playback); Audio stream(r); std::int32_t values[]={11,12};
      auto bytes=std::as_bytes(std::span(values)); assert(stream.write(bytes,2,Deadline::max()).frames==2);
      stream.start(); std::int32_t output[4]; stream.playback(output,4);
      assert(output[0]==11 && output[1]==12 && output[2]==0 && output[3]==0); assert(stream.emergency_output_frames()==2);
      assert(stream.write(bytes,2,Deadline::max()).status==Status::Underflow); }
    { Audio stream(request(Direction::Capture)); stream.start(); Buffer b(16);
      auto result=stream.read(b.bytes(),4,std::chrono::steady_clock::now()+std::chrono::milliseconds(2)); assert(result.status==Status::Timeout && result.frames==0);
      std::jthread controller([&]{ std::this_thread::sleep_for(std::chrono::milliseconds(2)); stream.cancel(); });
      result=stream.read(b.bytes(),4,Deadline::max()); assert(result.status==Status::Cancelled); }
    { auto r=request(Direction::Capture); r.queue_frames=127; Ring ring(r);
      constexpr std::uint32_t count=200000;
      std::jthread producer([&]{ for (std::uint32_t n=0;n<count;) { auto bytes=std::as_bytes(std::span(&n,1)); if(ring.push(bytes.data(),1)) ++n; else std::this_thread::yield(); } });
      for (std::uint32_t n=0;n<count;) { std::uint32_t v=0; if(ring.pop(reinterpret_cast<std::byte*>(&v),1)) { assert(v==n); ++n; } else std::this_thread::yield(); } }
    for(int round=0;round<30;++round) {
        auto r=request(Direction::Capture);r.queue_frames=64;Audio stream(r);stream.start();
        std::uint32_t admitted=0;
        std::jthread producer([&]{
            for(std::uint32_t i=0;i<10000;++i) {
                stream.capture(&i,1);
                if(stream.callback_status()!=Status::Ok)break;
                ++admitted;
            }
            stream.fault(Status::Disconnected);
        });
        Buffer b(16);std::uint32_t received=0;
        while(true) {
            auto result=stream.read(b.bytes(),4,Deadline::max());
            for(std::size_t i=0;i<result.frames;++i){std::uint32_t v;std::memcpy(&v,b.bytes().data()+i*4,4);assert(v==received++);}
            if(result.status!=Status::Ok){assert(result.status==Status::Overflow || result.status==Status::Disconnected);break;}
        }
        producer.join();assert(received==admitted);
    }
    audio::register_elements(ElementRegistry::instance());
    for (const auto name:{"AudioSource","AudioSink"}) assert(ElementRegistry::instance().find(name));
    bool unavailable=false; try { audio::provider("missing"); } catch(const std::invalid_argument&) { unavailable=true; } assert(unavailable);
    std::cout<<"audio bounded callback handoff, overflow, silence evidence and cancellation passed (no hardware opened)\n";
}
