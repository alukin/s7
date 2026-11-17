// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: LGPL-3.0-or-later
#include "callback_stream.hpp"
#include <portaudio.h>
#include <charconv>
#include <mutex>

namespace s7::audio {
using namespace hardware;
namespace {
// PortAudio's API requires process-wide initialize/terminate pairing. This
// guard serializes only that API bookkeeping, never owns devices or graph state.
class Context {
public:
    Context() { std::lock_guard lock(mutex()); check(Pa_Initialize()); }
    ~Context() { std::lock_guard lock(mutex()); Pa_Terminate(); }
    static void check(PaError err) { if (err!=paNoError) throw std::runtime_error(std::string("PortAudio: ")+Pa_GetErrorText(err)); }
private:
    static std::mutex& mutex() { static std::mutex m; return m; }
};
PaDeviceIndex select_device(const Request& r) {
    if (r.device=="default") return r.direction==Direction::Capture?Pa_GetDefaultInputDevice():Pa_GetDefaultOutputDevice();
    int value=-1;const auto p=std::from_chars(r.device.data(),r.device.data()+r.device.size(),value);
    if (p.ec!=std::errc{} || p.ptr!=r.device.data()+r.device.size() || value<0)
        throw std::invalid_argument("PortAudio device must be default or an enumeration index");
    return value;
}
Descriptor describe(const Request& r,PaDeviceIndex index) {
    auto d=detail::audio_descriptor(r);
    const auto* info=Pa_GetDeviceInfo(index);
    if (!info) throw std::invalid_argument("PortAudio device unavailable");
    d.backend_details["selected_device_index"]=std::to_string(index);
    d.backend_details["selected_device_name"]=info->name;
    d.backend_details["identity_provenance"]="OS enumeration index; physical identity unknown";
    if (const auto* host=Pa_GetHostApiInfo(info->hostApi)) d.backend_details["host_api"]=host->name;
    return d;
}
class PaStreamOwner final : public detail::CallbackStream {
public:
    PaStreamOwner(const Request& r,std::shared_ptr<Context> context,PaDeviceIndex index)
        : CallbackStream(describe(r,index)), context_(std::move(context)) {
        detail::validate_audio(r);
        if (!r.options.empty()) throw std::invalid_argument("PortAudio v1 has no backend options");
        const auto* info=Pa_GetDeviceInfo(index);
        if (!info) throw std::invalid_argument("PortAudio device unavailable");
        const bool capture=r.direction==Direction::Capture;
        if (r.channels.size()>static_cast<std::size_t>(capture?info->maxInputChannels:info->maxOutputChannels))
            throw std::invalid_argument("PortAudio channel selection exceeds device capacity");
        PaStreamParameters p{}; p.device=index; p.channelCount=static_cast<int>(r.channels.size());
        p.sampleFormat=r.type==SignalType::F32?paFloat32:r.type==SignalType::I16?paInt16:paInt32;
        p.suggestedLatency=capture?info->defaultLowInputLatency:info->defaultLowOutputLatency;
        Context::check(Pa_IsFormatSupported(capture?&p:nullptr,capture?nullptr:&p,r.rate.to_hz()));
        Context::check(Pa_OpenStream(&stream_,capture?&p:nullptr,capture?nullptr:&p,r.rate.to_hz(),
                                    static_cast<unsigned long>(r.block_frames),paClipOff|paDitherOff,&callback,this));
        const auto* actual=Pa_GetStreamInfo(stream_);
        if (!actual || actual->sampleRate!=r.rate.to_hz()) {
            Pa_CloseStream(stream_); stream_=nullptr;
            throw std::invalid_argument("PortAudio negotiated a different nominal rate");
        }
    }
    ~PaStreamOwner() override { stop(); if (stream_) Pa_CloseStream(stream_); }
protected:
    void do_start() override { Context::check(Pa_StartStream(stream_)); }
    void do_stop() noexcept override { if (stream_ && Pa_IsStreamStopped(stream_)==0) Pa_AbortStream(stream_); }
private:
    static int callback(const void* in,void* out,unsigned long frames,const PaStreamCallbackTimeInfo*,
                        PaStreamCallbackFlags flags,void* self) noexcept {
        auto& s=*static_cast<PaStreamOwner*>(self);
        const bool capture=s.descriptor().actual.direction==Direction::Capture;
        if (capture && (flags&paInputOverflow)) s.fault(Status::Overflow);
        if (capture && (flags&paInputUnderflow)) s.fault(Status::ContinuityLost);
        if (!capture && (flags&paOutputUnderflow)) s.fault(Status::Underflow);
        if (!capture && (flags&paOutputOverflow)) s.fault(Status::ContinuityLost);
        if (capture) s.capture(in,frames); else s.playback(out,frames);
        return s.cancelled()?paAbort:paContinue;
    }
    std::shared_ptr<Context> context_;
    PaStream* stream_{};
};
class PortAudio final : public Provider {
public:
    std::string_view id() const noexcept override { return "portaudio"; }
    std::vector<DeviceInfo> devices() override {
        Context context;
        auto count=Pa_GetDeviceCount(); if (count<0) Context::check(count);
        std::vector<DeviceInfo> result;
        for (PaDeviceIndex i=0;i<count;++i) if (const auto* d=Pa_GetDeviceInfo(i)) {
            const auto* host=Pa_GetHostApiInfo(d->hostApi);
            result.push_back({"portaudio",std::to_string(i),std::string(host?host->name:"")+": "+d->name,
                "OS enumeration index; unstable between runs",d->maxInputChannels?Support::Supported:Support::Unsupported,
                d->maxOutputChannels?Support::Supported:Support::Unsupported});
        }
        return result;
    }
    std::unique_ptr<Stream> prepare(const Request& r) override {
        if (r.backend!=id()) throw std::invalid_argument("PortAudio backend mismatch");
        detail::validate_audio(r);
        if (!r.options.empty()) throw std::invalid_argument("PortAudio v1 has no backend options");
        auto context=std::make_shared<Context>();
        const auto index=select_device(r);
        return std::make_unique<PaStreamOwner>(r,std::move(context),index);
    }
};
}
std::unique_ptr<Provider> portaudio_provider() { return std::make_unique<PortAudio>(); }
} // namespace s7::audio
