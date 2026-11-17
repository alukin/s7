// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: LGPL-3.0-or-later
#include <s7/sdr.hpp>
#include <s7/hardware_backend.hpp>
#include <SoapySDR/Device.hpp>
#include <SoapySDR/Formats.hpp>
#include <SoapySDR/Constants.h>
#include <SoapySDR/Errors.h>
#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstring>

namespace s7::sdr {
using namespace hardware;
namespace {
Status status(int code) {
    switch (code) {
    case SOAPY_SDR_TIMEOUT: return Status::Timeout;
    case SOAPY_SDR_OVERFLOW: return Status::Overflow;
    case SOAPY_SDR_UNDERFLOW: return Status::Underflow;
    case SOAPY_SDR_CORRUPTION: case SOAPY_SDR_TIME_ERROR: return Status::ContinuityLost;
    case SOAPY_SDR_NOT_SUPPORTED: return Status::Unsupported;
    default: return Status::BackendError;
    }
}
void check(int code) { if (code<0) throw std::runtime_error(std::string("SoapySDR: ")+SoapySDR_errToStr(code)); }
double number(const std::string& text) {
    double value=0; const auto p=std::from_chars(text.data(),text.data()+text.size(),value);
    if (p.ec!=std::errc{} || p.ptr!=text.data()+text.size() || !std::isfinite(value)) throw std::invalid_argument("invalid SDR numeric option");
    return value;
}
class SoapyStream final : public Stream {
public:
    SoapyStream(Descriptor d,SoapySDR::Device* device,SoapySDR::Stream* stream)
        : Stream(std::move(d)),device_(device),stream_(stream),
          planar_(descriptor().actual.block_frames*descriptor().actual.frame_bytes()),
          pending_(descriptor().actual.block_frames*descriptor().actual.frame_bytes()) {
        const auto& r=descriptor().actual;
        const auto plane=r.block_frames*signal_type_info(r.type).storage_bytes;
        // plane offsets must preserve alignment for driver-typed accesses.
        for (std::size_t c=0;c<r.channels.size();++c) pointers_.push_back(planar_.bytes().data()+c*plane);
        for (auto* p:pointers_) const_pointers_.push_back(p);
    }
    ~SoapyStream() override {
        BackendCallGuard guard;
        stop();
        if (stream_) { try { device_->closeStream(stream_); } catch (...) {} }
        if (device_) { try { SoapySDR::Device::unmake(device_); } catch (...) {} }
    }
protected:
    void do_start() override {
        check(device_->activateStream(stream_)); active_=true;
        if (pending_frames_) {
            std::size_t sent=0;
            const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(1);
            while (sent<pending_frames_) {
                auto result=transfer(nullptr,pending_.bytes().data()+sent*descriptor().actual.frame_bytes(),pending_frames_-sent,deadline,{});
                sent+=result.frames;
                if (result.status!=Status::Ok || !result.frames) throw std::runtime_error(std::string("SoapySDR prefill: ")+to_string(result.status));
            }
            pending_frames_=0;
        }
    }
    void do_stop() noexcept override {
        if (active_) { try { device_->deactivateStream(stream_); } catch (...) {} active_=false; }
    }
    Transfer do_read(std::span<std::byte> b,std::size_t n,Deadline d,std::stop_token st) override { return transfer(b.data(),nullptr,n,d,st); }
    Transfer do_write(std::span<const std::byte> b,std::size_t n,Deadline d,std::stop_token st) override {
        if (!active_) {
            if (pending_frames_) return {0,Status::Busy,0,{}};
            std::memcpy(pending_.bytes().data(),b.data(),n*descriptor().actual.frame_bytes()); pending_frames_=n;
            return {n,Status::Ok,0,{}}; // Accepted into this stream's bounded prefill.
        }
        return transfer(nullptr,b.data(),n,d,st);
    }
private:
    Transfer transfer(std::byte* dst,const std::byte* src,std::size_t n,Deadline deadline,std::stop_token st) {
        const auto& r=descriptor().actual;
        const auto stride=signal_type_info(r.type).storage_bytes;
        auto check_ci12=[&](const std::byte* data) {
            if (r.type!=SignalType::CI12) return;
            std::int16_t iq[2]; std::memcpy(iq,data,sizeof(iq));
            if (iq[0]<-2048 || iq[0]>2047 || iq[1]<-2048 || iq[1]>2047)
                throw std::invalid_argument("SoapySDR CI12 value outside significant-bit range");
        };
        if (src && r.type==SignalType::CI12) for (std::size_t i=0;i<n*r.channels.size();++i) check_ci12(src+i*stride);
        if (src) for (std::size_t c=0;c<r.channels.size();++c) for (std::size_t i=0;i<n;++i)
            std::memcpy(static_cast<std::byte*>(pointers_[c])+i*stride,src+(i*r.channels.size()+c)*stride,stride);
        while (true) {
            if (cancelled() || st.stop_requested()) return {0,Status::Cancelled,0,{}};
            const auto now=std::chrono::steady_clock::now();
            if (now>=deadline) return {0,Status::Timeout,0,{}};
            auto us=std::clamp<std::int64_t>(std::chrono::duration_cast<std::chrono::microseconds>(deadline-now).count(),1,10000);
            int flags=0; long long time=0;
            int count;
            if (dst) count=device_->readStream(stream_,pointers_.data(),n,flags,time,static_cast<long>(us));
            else count=device_->writeStream(stream_,const_pointers_.data(),n,flags,0,static_cast<long>(us));
            if (count==SOAPY_SDR_TIMEOUT) continue; // Does not discard/restart the native stream.
            if (count<0) return {0,status(count),0,{},count};
            if (static_cast<std::size_t>(count)>n || count==0) return {0,Status::BackendError,0,{}};
            if (dst) for (std::size_t c=0;c<r.channels.size();++c) for (int i=0;i<count;++i) {
                auto* sample=static_cast<std::byte*>(pointers_[c])+static_cast<std::size_t>(i)*stride;
                check_ci12(sample);
                std::memcpy(dst+(static_cast<std::size_t>(i)*r.channels.size()+c)*stride,sample,stride);
            }
            Transfer result{static_cast<std::size_t>(count),Status::Ok,0,{}};
            if (dst && (flags&SOAPY_SDR_HAS_TIME)) result.time=Timestamp{time,"soapy-device-time","driver-reported, epoch unspecified"};
            if (flags&SOAPY_SDR_END_ABRUPT) result.status=Status::ContinuityLost;
            if (flags&SOAPY_SDR_END_BURST) result.status=Status::ContinuityLost; // Continuous profile only.
            if (src) {
                std::size_t mask=0; int sf=0; long long t=0;
                const auto s=device_->readStreamStatus(stream_,mask,sf,t,0);
                if (s<0 && s!=SOAPY_SDR_TIMEOUT && s!=SOAPY_SDR_NOT_SUPPORTED) { result.status=status(s);result.native_error=s; }
            }
            return result;
        }
    }
    SoapySDR::Device* device_{};
    SoapySDR::Stream* stream_{};
    Buffer planar_,pending_;
    std::vector<void*> pointers_;
    std::vector<const void*> const_pointers_;
    std::size_t pending_frames_{};
    bool active_{};
};
class Soapy final : public Provider {
public:
    std::string_view id() const noexcept override { return "soapy"; }
    std::vector<DeviceInfo> devices() override {
        BackendCallGuard guard;
        std::vector<DeviceInfo> result;
        for (const auto& d:SoapySDR::Device::enumerate()) {
            const auto label=d.find("label");
            result.push_back({"soapy",SoapySDR::KwargsToString(d),label==d.end()?"SoapySDR device":label->second,
                "driver-reported selector; cross-provider association unknown",Support::Unknown,Support::Unknown});
        }
        return result;
    }
    std::unique_ptr<Stream> prepare(const Request& r) override {
        BackendCallGuard guard;
        r.validate();
        if (r.backend!=id() || r.device=="default" || r.device.empty()) throw std::invalid_argument("SoapySDR requires an explicit device selector (driver=...,serial=...)");
        if (r.type!=SignalType::C32 && r.type!=SignalType::CI12 && r.type!=SignalType::CI16) throw std::invalid_argument("SoapySDR supports C32, qualified native CI12/CI16");
        for (const auto& [key,value]:r.options) {
            if (key!="frequency_hz" && key!="bandwidth_hz" && key!="gain_db" && key!="antenna") throw std::invalid_argument("unknown SoapySDR option: "+key);
            if (key!="antenna") {
                const auto v=number(value);
                if (key!="gain_db" && v<=0) throw std::invalid_argument("RF frequency/bandwidth must be positive");
            }
        }
        SoapySDR::Device* device=nullptr; SoapySDR::Stream* stream=nullptr;
        try {
            device=SoapySDR::Device::make(r.device);
            if (!device) throw std::runtime_error("SoapySDR device unavailable");
            const int direction=r.direction==Direction::Capture?SOAPY_SDR_RX:SOAPY_SDR_TX;
            std::vector<std::size_t> channels;
            Descriptor d; d.actual=r; d.amplitude=r.type==SignalType::C32?"explicit CF32 application I/Q conversion":"native signed I/Q, no normalization";
            d.timestamps=Support::Unknown;
            const std::string format=r.type==SignalType::C32?SOAPY_SDR_CF32:SOAPY_SDR_CS16;
            for (const auto& text:r.channels) {
                std::size_t channel=0; const auto p=std::from_chars(text.data(),text.data()+text.size(),channel);
                if (p.ec!=std::errc{} || p.ptr!=text.data()+text.size() || std::to_string(channel)!=text || channel>=device->getNumChannels(direction))
                    throw std::invalid_argument("invalid SoapySDR channel ID");
                channels.push_back(channel);
                double fullscale=0; auto native=device->getNativeStreamFormat(direction,channel,fullscale);
                if (r.type!=SignalType::C32 && (native!=SOAPY_SDR_CS16 || fullscale!=(r.type==SignalType::CI12?2048.0:32768.0)))
                    throw std::invalid_argument("integer I/Q requires matching native CS16 full-scale evidence (2048 for CI12; 32768 for CI16); select C32 conversion explicitly otherwise");
                d.backend_details["channel."+text+".native_format"]=native+" fullscale="+std::to_string(fullscale);
                const auto formats=device->getStreamFormats(direction,channel);
                if (std::find(formats.begin(),formats.end(),format)==formats.end()) throw std::invalid_argument("SoapySDR application format unsupported");
                device->setSampleRate(direction,channel,r.rate.to_hz());
                if (device->getSampleRate(direction,channel)!=r.rate.to_hz()) throw std::invalid_argument("SoapySDR nominal rate differs from graph");
                for (const auto& [key,value]:r.options) {
                    if (key=="antenna") { device->setAntenna(direction,channel,value); if (device->getAntenna(direction,channel)!=value) throw std::invalid_argument("antenna readback mismatch"); }
                    else {
                        const auto v=number(value); double actual=0;
                        if (key=="frequency_hz") { device->setFrequency(direction,channel,v); actual=device->getFrequency(direction,channel); }
                        if (key=="bandwidth_hz") { device->setBandwidth(direction,channel,v); actual=device->getBandwidth(direction,channel); }
                        if (key=="gain_db") {
                            if (device->hasGainMode(direction,channel)) device->setGainMode(direction,channel,false);
                            device->setGain(direction,channel,v); actual=device->getGain(direction,channel);
                        }
                        if (actual!=v) throw std::invalid_argument("SoapySDR control readback mismatch: "+key);
                    }
                }
                d.backend_details["channel."+text+".frequency_hz"]=std::to_string(device->getFrequency(direction,channel));
            }
            stream=device->setupStream(direction,format,channels);
            if (!stream) throw std::runtime_error("SoapySDR setupStream returned null");
            auto result=std::make_unique<SoapyStream>(std::move(d),device,stream);
            device=nullptr; stream=nullptr; return result;
        } catch (...) {
            if (stream) { try { device->closeStream(stream); } catch (...) {} }
            if (device) { try { SoapySDR::Device::unmake(device); } catch (...) {} }
            throw;
        }
    }
};
}
std::unique_ptr<Provider> soapy_provider() { return std::make_unique<Soapy>(); }
} // namespace s7::sdr
