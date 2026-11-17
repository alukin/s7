// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include <s7/hardware.hpp>
#include <s7/compiler.hpp>
#include <s7/elements.hpp>
#include <cassert>
#include <cstring>
#include <iostream>
using namespace s7;
using namespace s7::hardware;
namespace {
template<class F> void rejects(F f) { bool threw=false; try { f(); } catch (const std::exception&) { threw=true; } assert(threw); }
struct Trace { int prepare{},start{},stop{}; std::size_t limit=100,prefill{},accepted_at_start{}; Status last=Status::Ok; std::vector<float> accepted; bool mismatch{}; };
Request request(Direction d=Direction::Capture) {
    Request r; r.backend="mock"; r.direction=d; r.channels={"0","1"}; r.block_frames=7; r.queue_frames=32; return r;
}
class MockStream final : public Stream {
public:
    MockStream(Request r,std::shared_ptr<Trace> t) : Stream([&] { Descriptor d; d.actual=r;d.startup_prefill_frames=t->prefill; if (t->mismatch) d.actual.rate={96000,1}; return d; }()),trace(t) {}
    ~MockStream() override { stop(); }
    void do_start() override { ++trace->start;trace->accepted_at_start=trace->accepted.size()/2; }
    void do_stop() noexcept override { ++trace->stop; }
    Transfer do_read(std::span<std::byte> b,std::size_t n,Deadline,std::stop_token) override {
        n=std::min(n,trace->limit-position);
        for (std::size_t i=0;i<n;++i) for (std::size_t c=0;c<2;++c) {
            float value=static_cast<float>((position+i)*2+c); std::memcpy(b.data()+(i*2+c)*sizeof(float),&value,sizeof(value));
        }
        position+=n; return {n,position==trace->limit?trace->last:Status::Ok,0,{}};
    }
    Transfer do_write(std::span<const std::byte> b,std::size_t n,Deadline,std::stop_token) override {
        // Exercise arbitrary valid prefixes: owner must submit only the suffix.
        n=std::min<std::size_t>({n,3,trace->limit-position});
        for (std::size_t i=0;i<n*2;++i) { float v; std::memcpy(&v,b.data()+i*sizeof(float),sizeof(v)); trace->accepted.push_back(v); }
        position+=n; return {n,position==trace->limit?trace->last:Status::Ok,0,{}};
    }
private: std::shared_ptr<Trace> trace; std::size_t position{};
};
class MockProvider final : public Provider {
public:
    explicit MockProvider(std::shared_ptr<Trace> t):trace(std::move(t)){}
    std::string_view id() const noexcept override { return "mock"; }
    std::vector<DeviceInfo> devices() override { return {}; }
    std::unique_ptr<Stream> prepare(const Request& r) override { ++trace->prepare; return std::make_unique<MockStream>(r,trace); }
private: std::shared_ptr<Trace> trace;
};
class Sequence final : public Element {
public:
    Sequence() : Element("Sequence",true) { register_output(0,"a",SignalType::F32,&a); register_output(1,"b",SignalType::F32,&b); }
    void step() override { a=static_cast<float>(2*n); b=a+1; ++n; }
private: float a{},b{}; unsigned n{};
};
class Tap final : public Element {
public:
    explicit Tap(bool fail=false):Element("Tap",true),fail_(fail) {register_input(0,"in",SignalType::F32,nullptr);}
    void step() override {if(fail_ && values.size()==3)throw std::runtime_error("injected graph failure");values.push_back(read_input<float>(0));}
    std::vector<float> values;
private:bool fail_;
};
ExecutableGraph graph(Request r) {
    Schema s("hardware test");
    if (r.direction==Direction::Capture) s.add_element("io",[r]{ return std::make_unique<Source>(r); });
    else {
        s.add_element("sequence",[]{return std::make_unique<Sequence>();});
        s.add_element("io",[r]{return std::make_unique<Sink>(r);});
        s.connect("sequence",0,"io",0); s.connect("sequence",1,"io",1);
    }
    return Compiler::compile(s);
}
Run::Resolve resolve(std::shared_ptr<Trace> t) { return [t](std::string_view){return std::make_unique<MockProvider>(t);}; }
}
int main() {
    auto r=request();
    { auto bad=r; bad.channels={"0","0"}; rejects([&]{bad.validate();}); bad=r; bad.rate={0,1}; rejects([&]{bad.validate();});
      bad=r; bad.queue_frames=1; rejects([&]{bad.validate();}); bad=r; bad.queue_frames=16777217; rejects([&]{bad.validate();});
      bad=r;bad.device=std::string("default\0other",13);rejects([&]{bad.validate();}); }
    { auto t=std::make_shared<Trace>(); auto g=graph(r); assert(t->prepare==0); // Offline compilation never resolves/opens hardware.
      NodeRuntime rt(std::move(g)); rejects([&]{rt.step();}); rejects([&]{rt.step();}); }
    { auto t=std::make_shared<Trace>(); Run run(graph(r),resolve(t)); assert(t->prepare==1 && t->start==0);
      assert(!run.descriptor().session_id.empty());
      const auto result=run.execute(23,std::chrono::milliseconds(10));
      assert(result.status==Status::Ok && result.graph_frames==23 && result.transferred_frames==23);
      assert(run.runtime().output_of<SignalType::F32>("io",0)==44); assert(run.runtime().output_of<SignalType::F32>("io",1)==45);
      rejects([&]{run.execute(1,std::chrono::milliseconds(10));}); }
    { auto t=std::make_shared<Trace>(); t->limit=10; t->last=Status::Overflow;
      Run run(graph(r),resolve(t)); const auto result=run.execute(50,std::chrono::milliseconds(10));
      assert(result.status==Status::Overflow && result.graph_frames==10 && result.transferred_frames==10); }
    { auto t=std::make_shared<Trace>(); Run run(graph(request(Direction::Playback)),resolve(t));
      const auto result=run.execute(23,std::chrono::milliseconds(10)); assert(result.status==Status::Ok && result.transferred_frames==23);
      assert(t->accepted.size()==46); for (std::size_t i=0;i<46;++i) assert(t->accepted[i]==static_cast<float>(i)); }
    { auto t=std::make_shared<Trace>();t->prefill=10;Run run(graph(request(Direction::Playback)),resolve(t));
      const auto result=run.execute(23,std::chrono::milliseconds(10));assert(result.status==Status::Ok && t->accepted_at_start==10);
      for(std::size_t i=0;i<46;++i)assert(t->accepted[i]==static_cast<float>(i)); }
    for(const auto block:{1u,3u,7u,16u}) {
        auto t=std::make_shared<Trace>();auto cfg=r;cfg.block_frames=block;
        Schema s("delay scalar equivalence");s.add_element("io",[cfg]{return std::make_unique<Source>(cfg);});
        s.add_element("delay",[]{return std::make_unique<DelayT<SignalType::F32>>(-123);});
        s.add_element("tap",[]{return std::make_unique<Tap>();});s.connect("io",0,"delay",0);s.connect("delay",0,"tap",0);
        auto g=Compiler::compile(s);auto* tap=static_cast<Tap*>(g.elements.at("tap").get());Run run(std::move(g),resolve(t));
        assert(run.execute(27,std::chrono::milliseconds(10)).status==Status::Ok);assert(tap->values.size()==27);
        for(std::size_t i=0;i<27;++i)assert(tap->values[i]==(i==0?-123.0f:static_cast<float>((i-1)*2)));
    }
    { auto t=std::make_shared<Trace>();Schema s("irreversible boundary failure");s.add_element("io",[r]{return std::make_unique<Source>(r);});
      s.add_element("tap",[]{return std::make_unique<Tap>(true);});s.connect("io",0,"tap",0);
      Run run(Compiler::compile(s),resolve(t));rejects([&]{run.execute(27,std::chrono::milliseconds(10));});
      rejects([&]{run.runtime().step();});assert(t->start==1 && t->prepare==1 && t->stop>0); }
    { auto t=std::make_shared<Trace>(); t->limit=9; t->last=Status::Underflow;
      Run run(graph(request(Direction::Playback)),resolve(t)); auto result=run.execute(23,std::chrono::milliseconds(10));
      assert(result.status==Status::Underflow && result.transferred_frames==9); assert(t->accepted.size()==18); }
    { auto t=std::make_shared<Trace>(); t->mismatch=true; rejects([&]{Run run(graph(r),resolve(t));}); assert(t->start==0); }
    { auto t=std::make_shared<Trace>(); Run run(graph(r),resolve(t)); std::stop_source stop; stop.request_stop();
      auto result=run.execute(23,std::chrono::milliseconds(10),stop.get_token()); assert(result.status==Status::Cancelled && result.graph_frames==0 && t->start==0); }
    { auto t=std::make_shared<Trace>(); MockStream s(r,t); Buffer b(r.block_frames*r.frame_bytes());
      rejects([&]{s.read(b.bytes(),1,Deadline::max());}); s.start();
      rejects([&]{s.read(b.bytes().first(1),1,Deadline::max());}); s.cancel();
      assert(s.read(b.bytes(),1,Deadline::max()).status==Status::Cancelled); s.stop(); rejects([&]{s.start();}); }
    { auto t=std::make_shared<Trace>(); Schema s("two clocks"); s.add_element("a",[r]{return std::make_unique<Source>(r);});
      s.add_element("b",[r]{return std::make_unique<Source>(r);}); rejects([&]{Run run(Compiler::compile(s),resolve(t));}); assert(t->prepare==0); }
    std::cout<<"hardware contract, scalar equivalence, partial I/O and faults passed\n";
}
