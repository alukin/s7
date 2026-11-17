// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include <s7/sdr.hpp>
#include <SoapySDR/Device.hpp>
#include <SoapySDR/Registry.hpp>
#include <SoapySDR/Formats.hpp>
#include <SoapySDR/Constants.h>
#include <SoapySDR/Errors.h>
#include <cassert>
#include <cstring>
#include <iostream>
using namespace s7;
using namespace s7::hardware;
namespace {
struct Trace { int alive{},starts{},closes{},rx_error{},tx_error{}; double fullscale=2048; bool wrong_rate{}; std::vector<std::int16_t> tx; } trace;
class Fake final : public SoapySDR::Device {
public:
    Fake(){++trace.alive;} ~Fake() override{--trace.alive;}
    std::string getDriverKey() const override { return "s7test"; }
    std::size_t getNumChannels(int) const override {return 2;}
    std::vector<std::string> getStreamFormats(int,std::size_t) const override {return {SOAPY_SDR_CS16,SOAPY_SDR_CF32};}
    std::string getNativeStreamFormat(int,std::size_t,double& scale) const override {scale=trace.fullscale;return SOAPY_SDR_CS16;}
    void setSampleRate(int,std::size_t,double value) override {rate=value;}
    double getSampleRate(int,std::size_t) const override {return rate+(trace.wrong_rate?1:0);}
    double getFrequency(int,std::size_t) const override {return 1000000000;}
    SoapySDR::Stream* setupStream(int,const std::string&,const std::vector<std::size_t>& selected,const SoapySDR::Kwargs&) override {
        channels=selected.size();return reinterpret_cast<SoapySDR::Stream*>(this);
    }
    void closeStream(SoapySDR::Stream*) override {++trace.closes;}
    int activateStream(SoapySDR::Stream*,int,long long,std::size_t) override {++trace.starts;return 0;}
    int deactivateStream(SoapySDR::Stream*,int,long long) override {return 0;}
    int readStream(SoapySDR::Stream*,void* const* buffers,std::size_t count,int& flags,long long& time,long) override {
        if(trace.rx_error)return trace.rx_error;
        const auto n=std::min<std::size_t>(count,3);flags=SOAPY_SDR_HAS_TIME;time=static_cast<long long>(position)*1000;
        for(std::size_t c=0;c<channels;++c) for(std::size_t i=0;i<n;++i) {
            std::int16_t values[]={static_cast<std::int16_t>((position+i)*10+c),static_cast<std::int16_t>(-static_cast<int>((position+i)*10+c))};
            std::memcpy(static_cast<std::byte*>(buffers[c])+i*4,values,4);
        }
        position+=n;return static_cast<int>(n);
    }
    int writeStream(SoapySDR::Stream*,const void* const* buffers,std::size_t count,int&,long long,long) override {
        const auto n=std::min<std::size_t>(count,3);
        for(std::size_t i=0;i<n;++i)for(std::size_t c=0;c<channels;++c) {
            std::int16_t values[2];std::memcpy(values,static_cast<const std::byte*>(buffers[c])+i*4,4);
            trace.tx.push_back(values[0]);trace.tx.push_back(values[1]);
        }
        return static_cast<int>(n);
    }
    int readStreamStatus(SoapySDR::Stream*,std::size_t&,int&,long long&,long) override {return trace.tx_error?trace.tx_error:SOAPY_SDR_NOT_SUPPORTED;}
private: double rate=0;std::size_t channels=0,position=0;
};
SoapySDR::KwargsList find(const SoapySDR::Kwargs&) {return {{{"driver","s7test"},{"serial","mock"}}};}
SoapySDR::Device* make(const SoapySDR::Kwargs&) {return new Fake;}
template<class F>void rejects(F f){bool bad=false;try{f();}catch(const std::exception&){bad=true;}assert(bad);}
Request request(Direction d) {Request r;r.backend="soapy";r.device="driver=s7test,serial=mock";r.type=SignalType::CI12;r.direction=d;r.channels={"0","1"};r.block_frames=7;r.queue_frames=16;return r;}
}
int main(){
    SoapySDR::Registry registration("s7test",find,make,SOAPY_SDR_ABI_VERSION);
    auto provider=sdr::provider("soapy");
    {
        auto r=request(Direction::Capture);auto stream=provider->prepare(r);assert(trace.starts==0);
        Buffer buffer(7*r.frame_bytes());stream->start();auto result=stream->read(buffer.bytes(),7,Deadline::max());
        assert(result.frames==3 && result.status==Status::Ok && result.time && result.time->nanoseconds==0);
        std::int16_t values[12];std::memcpy(values,buffer.bytes().data(),sizeof(values));
        const std::int16_t expected[]={0,0,1,-1,10,-10,11,-11,20,-20,21,-21};assert(std::memcmp(values,expected,sizeof(values))==0);
        result=stream->read(buffer.bytes(),7,Deadline::max());assert(result.frames==3 && result.first_frame==3 && result.time->nanoseconds==3000);
        trace.rx_error=SOAPY_SDR_OVERFLOW;result=stream->read(buffer.bytes(),7,Deadline::max());assert(result.status==Status::Overflow && result.native_error==SOAPY_SDR_OVERFLOW);
        trace.rx_error=0;assert(stream->read(buffer.bytes(),7,Deadline::max()).status==Status::Overflow);
    }
    assert(trace.alive==0);
    {
        auto r=request(Direction::Playback);auto stream=provider->prepare(r);Buffer buffer(7*r.frame_bytes());
        std::int16_t values[28];for(int i=0;i<28;++i)values[i]=static_cast<std::int16_t>(i);std::memcpy(buffer.bytes().data(),values,sizeof(values));
        const auto result=stream->write(buffer.bytes(),7,Deadline::max());assert(result.frames==7 && trace.tx.empty());
        stream->start(); // Prefill exercises repeated 3-frame native writes.
        assert(trace.tx.size()==28);for(int i=0;i<28;++i)assert(trace.tx[i]==i);
        trace.tx_error=SOAPY_SDR_UNDERFLOW;
        auto partial=stream->write(buffer.bytes(),7,Deadline::max());assert(partial.frames==3 && partial.status==Status::Underflow);trace.tx_error=0;
    }
    assert(trace.alive==0);
    trace.fullscale=32768;rejects([&]{provider->prepare(request(Direction::Capture));});assert(trace.alive==0);trace.fullscale=2048;
    trace.wrong_rate=true;rejects([&]{provider->prepare(request(Direction::Capture));});assert(trace.alive==0);trace.wrong_rate=false;
    {
        auto r=request(Direction::Capture);auto stream=provider->prepare(r);stream->start();stream->cancel();Buffer b(7*r.frame_bytes());
        assert(stream->read(b.bytes(),7,Deadline::max()).status==Status::Cancelled);
    }
    std::cout<<"actual SoapySDR adapter passed fake-driver planar I/O, timing, partial writes, format evidence and fault tests\n";
}
