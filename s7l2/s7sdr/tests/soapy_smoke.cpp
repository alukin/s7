// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include <s7/sdr.hpp>
#include <s7/builtin_element_registry.hpp>
#include <s7/compiler.hpp>
#include <s7/schema_library.hpp>
#include "radio.hpp"
#include <array>
#include <chrono>
#include <fstream>
#include <numbers>
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
struct Trace { int alive{},starts{},closes{},rx_error{},tx_error{}; double fullscale=2048; bool wrong_rate{}, fm{}, automatic{}; std::vector<std::int16_t> tx; } trace;
class Fake final : public SoapySDR::Device {
public:
    Fake(){++trace.alive;} ~Fake() override{--trace.alive;}
    std::string getDriverKey() const override { return "s7test"; }
    std::size_t getNumChannels(int) const override {return 2;}
    std::vector<std::string> getStreamFormats(int,std::size_t) const override {return {SOAPY_SDR_CS16,SOAPY_SDR_CF32};}
    std::string getNativeStreamFormat(int,std::size_t,double& scale) const override {scale=trace.fullscale;return SOAPY_SDR_CS16;}
    void setSampleRate(int,std::size_t,double value) override {rate=value;}
    double getSampleRate(int,std::size_t) const override {return rate+(trace.wrong_rate?1:0);}
    void setFrequency(int, std::size_t, double value, const SoapySDR::Kwargs&) override { frequency = value; }
    double getFrequency(int, std::size_t) const override { return frequency; }
    void setBandwidth(int, std::size_t, double value) override { bandwidth = value; }
    double getBandwidth(int, std::size_t) const override { return bandwidth; }
    void setAntenna(int, std::size_t, const std::string& value) override { antenna = value; }
    std::string getAntenna(int, std::size_t) const override { return antenna; }
    bool hasGainMode(int direction, std::size_t) const override { return direction == SOAPY_SDR_RX; }
    void setGainMode(int, std::size_t, bool automatic) override { trace.automatic = automatic; }
    bool getGainMode(int, std::size_t) const override { return trace.automatic; }
    std::size_t getStreamMTU(SoapySDR::Stream*) const override { return trace.fm ? 1101 : 3; }
    SoapySDR::Stream* setupStream(int,const std::string&,const std::vector<std::size_t>& selected,const SoapySDR::Kwargs&) override {
        channels=selected.size();return reinterpret_cast<SoapySDR::Stream*>(this);
    }
    void closeStream(SoapySDR::Stream*) override {++trace.closes;}
    int activateStream(SoapySDR::Stream*,int,long long,std::size_t) override {++trace.starts;return 0;}
    int deactivateStream(SoapySDR::Stream*,int,long long) override {return 0;}
    int readStream(SoapySDR::Stream*,void* const* buffers,std::size_t count,int& flags,long long& time,long) override {
        if (count > getStreamMTU(nullptr)) throw std::runtime_error("Soapy read exceeds native MTU");
        if(trace.rx_error)return trace.rx_error;
        if (trace.fm) {
            constexpr double pi = std::numbers::pi;
            auto* output = static_cast<float*>(buffers[0]);
            for (std::size_t i = 0; i < count; ++i) {
                const double t = static_cast<double>(position++) / rate;
                const double left = 0.4 * std::sin(2 * pi * 1000 * t);
                const double right = 0.4 * std::sin(2 * pi * 2300 * t);
                const double pilot = 2 * pi * 19001 * t + 0.7;
                const double multiplex = t < 0.5
                    ? (left + right) / 2 + (left - right) / 2 * std::sin(2 * pilot) + 0.09 * std::sin(pilot)
                    : left;
                phase += 2 * pi * (1200 + 75000 * multiplex) / rate;
                output[2*i] = static_cast<float>(std::cos(phase));
                output[2*i+1] = static_cast<float>(std::sin(phase));
            }
            flags = 0;
            return static_cast<int>(count);
        }
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
private:
    double rate{}, bandwidth{}, frequency{1000000000}, phase{};
    std::size_t channels{}, position{};
    std::string antenna;
};
SoapySDR::KwargsList find(const SoapySDR::Kwargs&) {return {{{"driver","s7test"},{"serial","mock"}}};}
SoapySDR::Device* make(const SoapySDR::Kwargs&) {return new Fake;}
template<class F>void rejects(F f){bool bad=false;try{f();}catch(const std::exception&){bad=true;}assert(bad);}
Request request(Direction d) {Request r;r.backend="soapy";r.device="driver=s7test,serial=mock";r.type=SignalType::CI12;r.direction=d;r.channels={"0","1"};r.block_frames=7;r.queue_frames=16;return r;}
struct StereoCapture {
    std::vector<std::array<float, 2>> samples;
    std::size_t prefill_at_start{};
    std::size_t starts{};
};

class TestPlayback final : public Stream {
public:
    TestPlayback(const Request& request, StereoCapture& captured)
        : Stream(describe(request)), captured_(captured) {}
private:
    static Descriptor describe(const Request& request) {
        Descriptor descriptor;
        descriptor.actual = request;
        descriptor.startup_prefill_frames = 69;
        return descriptor;
    }
    void do_start() override {
        captured_.prefill_at_start = captured_.samples.size();
        ++captured_.starts;
    }
    void do_stop() noexcept override {}
    Transfer do_write(std::span<const std::byte> bytes, std::size_t count, Deadline, std::stop_token) override {
        if (state() == State::Prepared && captured_.samples.size() + count > descriptor().startup_prefill_frames)
            throw std::runtime_error("Receiver writes past audio prefill");
        count = std::min<std::size_t>(count, 29);
        for (std::size_t i = 0; i < count; ++i) {
            std::array<float, 2> sample;
            std::memcpy(sample.data(), bytes.data() + i * sizeof(sample), sizeof(sample));
            captured_.samples.push_back(sample);
        }
        return {count, Status::Ok, 0, {}};
    }
    StereoCapture& captured_;
};

class TestAudioProvider final : public Provider {
public:
    explicit TestAudioProvider(StereoCapture& captured) : captured_(captured) {}
    std::string_view id() const noexcept override { return "pipewire"; }
    std::vector<DeviceInfo> devices() override { return {}; }
    std::unique_ptr<Stream> prepare(const Request& request) override {
        if (request.rate != Rate::from_hz(48000) || request.type != SignalType::F32 ||
            request.channels != std::vector<std::string>{"0", "1"})
            throw std::runtime_error("Receiver did not request stereo PCM at 48 kHz");
        return std::make_unique<TestPlayback>(request, captured_);
    }
private:
    StereoCapture& captured_;
};

void test_fm_receiver() {
    auto& registry = ElementRegistry::instance();
    register_builtin_elements(registry);
    radio::register_elements(registry);
    sdr::register_elements(registry);
    register_elements(registry,"s7audio","AudioSource","AudioSink",{SignalType::F32},"pipewire");

    std::ifstream diagram(S7_FM_EXAMPLE);
    const std::string json((std::istreambuf_iterator<char>(diagram)), {});
    auto document = boost::json::parse(json);
    auto& schema = document.as_object().at("schema");
    auto& elements = schema.as_object().at("elements").as_array();
    elements.front().as_object().at("params").as_object()["device"] = "driver=s7test,serial=mock";
    // Exercise partial native reads and audio blocks, including final RF ticks
    // that do not fill an audio frame interval.
    elements.back().as_object().at("params").as_object()["block_frames"] = 97;
    const auto frames = elements.front().as_object().at("params").as_object()
        .at("frame_rate").as_array()[0].to_number<std::size_t>() - 7;
    StereoCapture captured;
    trace.fm = true;
    {
        auto graph = Compiler::compile(SchemaLibraryLoader::build_schema(schema, registry));
        Run run(std::move(graph), [&](std::string_view name) -> std::unique_ptr<Provider> {
            if (name == "pipewire") return std::make_unique<TestAudioProvider>(captured);
            return sdr::provider(name);
        });
        const auto result = run.execute(frames, std::chrono::milliseconds(1000));
        if (result.status != Status::Ok || result.graph_frames != frames || result.transferred_frames != frames ||
            result.playback_frames != 48000 || captured.samples.size() != result.playback_frames ||
            captured.starts != 1 || captured.prefill_at_start != 69 || !trace.automatic)
            throw std::runtime_error("FM receiver did not complete stereo Soapy capture and playback");
    }
    trace.fm = false;
    auto amplitude = [&](std::size_t channel, double hz, std::size_t start) {
        constexpr std::size_t count = 4800;
        const double omega = 2 * std::numbers::pi * hz / 48000;
        std::complex<double> tone{};
        for (std::size_t i = 0; i < count; ++i)
            tone += static_cast<double>(captured.samples[start+i][channel]) * std::polar(1.0, -omega * i);
        return 2 * std::abs(tone) / count;
    };
    constexpr std::size_t stereo = 18000, mono = 42000;
    if (amplitude(0, 1000, stereo) < 0.32 || amplitude(1, 2300, stereo) < 0.25 ||
        amplitude(1, 1000, stereo) > 0.012 || amplitude(0, 2300, stereo) > 0.012 ||
        amplitude(0, 19001, stereo) > 0.002 || amplitude(1, 19001, stereo) > 0.002)
        throw std::runtime_error("FM stereo channel separation or pilot rejection failed");
    if (amplitude(0, 1000, mono) < 0.32 || amplitude(1, 1000, mono) < 0.32 ||
        std::abs(amplitude(0, 1000, mono) - amplitude(1, 1000, mono)) > 0.001)
        throw std::runtime_error("FM receiver did not blend to mono after pilot loss");
}

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
    trace.wrong_rate=true;
    rejects([&]{provider->prepare(request(Direction::Capture));});
    assert(trace.alive==0);
    {
        auto r=request(Direction::Capture);
        r.options["rate_tolerance_ppm"]="25";
        auto stream=provider->prepare(r);
        assert(stream->descriptor().actual.rate==r.rate);
        assert(std::stod(stream->descriptor().backend_details.at("channel.0.sample_rate_hz"))==48001);
        r.options["gain_mode"]="auto";
        r.options["gain_db"]="15";
        rejects([&]{provider->prepare(r);});
    }
    trace.wrong_rate=false;
    {
        auto r=request(Direction::Capture);auto stream=provider->prepare(r);stream->start();stream->cancel();Buffer b(7*r.frame_bytes());
        assert(stream->read(b.bytes(),7,Deadline::max()).status==Status::Cancelled);
    }
    test_fm_receiver();
    std::cout<<"actual SoapySDR adapter passed fake-driver planar I/O, timing, partial writes, format evidence and fault tests\n";
}
