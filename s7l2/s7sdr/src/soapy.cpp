// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: LGPL-3.0-or-later
#include <s7/sdr.hpp>
#include <s7/hardware_backend.hpp>
#include <SoapySDR/Device.hpp>
#include <SoapySDR/Formats.hpp>
#include <SoapySDR/Constants.h>
#include <SoapySDR/Errors.h>
#include <SoapySDR/Logger.hpp>
#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstring>
#include <format>
#include <optional>
#ifdef S7_HAVE_IIO
#include <iio.h>
#include <atomic>
#include <cerrno>
#include <system_error>
#endif

namespace s7::sdr {
using namespace hardware;
namespace {
#ifdef S7_HAVE_IIO
bool is_pluto(const SoapySDR::Kwargs& selector) {
    const auto driver = selector.find("driver");
    return driver != selector.end() && driver->second == "plutosdr";
}
#endif
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
// Exact decimal text without exponents: whole numbers (Hz, S/s) in fixed
// notation, other values in the shortest round-trip form.
std::string decimal(double value) {
    if (std::trunc(value)==value && std::abs(value)<1e17) return std::format("{:.0f}",value);
    return std::format("{}",value);
}
double number(const std::string& text) {
    double value=0; const auto p=std::from_chars(text.data(),text.data()+text.size(),value);
    if (p.ec!=std::errc{} || p.ptr!=text.data()+text.size() || !std::isfinite(value)) throw std::invalid_argument("invalid SDR numeric option");
    return value;
}

#ifdef S7_HAVE_IIO
// SoapyPlutoSDR shares its IIO context across device instances. Serialize
// ownership so one run cannot idle the radio while another is using it.
class Ad936xSession {
public:
    Ad936xSession(std::string uri, Direction direction, bool keep_sdr_active)
        : uri_(std::move(uri)), stream_lo_(direction == Direction::Capture ? "altvoltage0" : "altvoltage1"),
          keep_sdr_active_(keep_sdr_active) {
        if (in_use_.test_and_set()) throw std::runtime_error("An S7 PlutoSDR session is already open");
    }
    ~Ad936xSession() {
        if (used_) {
            try {
                const auto context = open();
                const auto* phy = iio_context_find_device(context.get(), "ad9361-phy");
                if (!phy) throw std::runtime_error("AD936x PHY is unavailable");
                if (started_ && keep_sdr_active_) {
                    // SoapyPlutoSDR powers down the stream's LO when closing it.
                    if (!set_lo_powerdown(phy, stream_lo_, false))
                        throw std::runtime_error("could not restore the stream's local oscillator");
                } else {
                    idle(context.get(), phy);
                }
            } catch (const std::exception& error) {
                SoapySDR::logf(SOAPY_SDR_ERROR, "Cannot set post-run AD936x state at %s: %s", uri_.c_str(), error.what());
            }
        }
        in_use_.clear();
    }
    Ad936xSession(const Ad936xSession&) = delete;
    Ad936xSession& operator=(const Ad936xSession&) = delete;

    void wake() {
        const auto context = open();
        const auto* phy = iio_context_find_device(context.get(), "ad9361-phy");
        if (!phy) throw std::runtime_error("AD936x PHY is unavailable");
        char mode[64]{};
        const auto count = iio_device_attr_read(phy, "ensm_mode", mode, sizeof(mode));
        if (count < 0) throw std::system_error(static_cast<int>(-count), std::generic_category(), "Read AD936x ENSM mode");
        const std::string_view readback(mode);
        const auto state = readback.substr(0, readback.find_first_of("\r\n"));
        if (state == "alert" || state == "sleep" || state == "wait") {
            used_ = true;
            const auto result = iio_device_attr_write(phy, "ensm_mode", "fdd");
            if (result < 0) throw std::system_error(static_cast<int>(-result), std::generic_category(), "Wake AD936x for streaming");
        }
    }
    void mark_used() noexcept { used_ = true; }
    void mark_started() noexcept { started_ = true; }

private:
    using Context = std::unique_ptr<iio_context, decltype(&iio_context_destroy)>;
    Context open() const {
        Context context(iio_create_context_from_uri(uri_.c_str()), iio_context_destroy);
        if (!context) throw std::system_error(errno, std::generic_category(), "Open AD936x at " + uri_);
        const int result = iio_context_set_timeout(context.get(), 1000);
        if (result < 0 && result != -ENOSYS)
            throw std::system_error(-result, std::generic_category(), "Set AD936x IIO timeout");
        return context;
    }
    static void idle(const iio_context* context, const iio_device* phy) {
        bool ok = set_lo_powerdown(phy, "altvoltage1", true);
        ok &= set_lo_powerdown(phy, "altvoltage0", true);
        if (const auto* dds = iio_context_find_device(context, "cf-ad9361-dds-core-lpc")) {
            for (unsigned i = 0; i < iio_device_get_channels_count(dds); ++i) {
                const auto* channel = iio_device_get_channel(dds, i);
                if (iio_channel_is_output(channel) &&
                    std::string_view(iio_channel_get_id(channel)).starts_with("altvoltage") &&
                    iio_channel_find_attr(channel, "raw"))
                    ok &= iio_channel_attr_write_bool(channel, "raw", false) >= 0;
            }
        }
        ok &= iio_device_attr_write(phy, "ensm_mode", "alert") >= 0;
        if (!ok) throw std::runtime_error("one or more RF idle controls failed");
    }
    static bool set_lo_powerdown(const iio_device* phy, const char* name, bool powerdown) {
        const auto* channel = iio_device_find_channel(phy, name, true);
        return channel && iio_channel_attr_write_bool(channel, "powerdown", powerdown) >= 0;
    }
    inline static std::atomic_flag in_use_{};
    std::string uri_;
    const char* stream_lo_;
    bool keep_sdr_active_{};
    bool used_{};
    bool started_{};
};
#endif

class SoapyStream final : public Stream {
public:
    SoapyStream(Descriptor d,SoapySDR::Device* device,SoapySDR::Stream* stream)
        : Stream(std::move(d)),device_(device),stream_(stream),
          planar_(descriptor().actual.block_frames*descriptor().actual.frame_bytes()),
          pending_(descriptor().actual.direction==Direction::Playback
              ? descriptor().actual.block_frames*descriptor().actual.frame_bytes() : 0) {
        const auto& r=descriptor().actual;
        const auto plane=r.block_frames*signal_type_info(r.type).storage_bytes;
        // plane offsets must preserve alignment for driver-typed accesses.
        pointers_.resize(r.channels.size());
        for (std::size_t c=0;c<r.channels.size();++c) pointers_[c]=planar_.bytes().data()+c*plane;
        for (auto* p:pointers_) const_pointers_.push_back(p);
    }
    ~SoapyStream() override {
        stop();
    }
#ifdef S7_HAVE_IIO
    void take_session(std::unique_ptr<Ad936xSession> session) noexcept { ad936x_ = std::move(session); }
#endif
protected:
    void do_start() override {
        active_=true; // A failed activation may still leave native resources running.
        check(device_->activateStream(stream_));
        native_limit_=device_->getStreamMTU(stream_);
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
#ifdef S7_HAVE_IIO
        if (ad936x_) ad936x_->mark_started();
#endif
    }
    void do_stop() noexcept override {
        if (active_) {
            try { check(device_->deactivateStream(stream_)); }
            catch (const std::exception& error) { SoapySDR::log(SOAPY_SDR_ERROR, error.what()); }
            catch (...) { SoapySDR::log(SOAPY_SDR_ERROR, "SoapySDR stream deactivation failed"); }
            active_=false;
        }
        if (stream_) {
            try { device_->closeStream(stream_); }
            catch (...) { SoapySDR::log(SOAPY_SDR_ERROR, "SoapySDR stream close failed"); }
            stream_=nullptr;
        }
        if (device_) {
            try { SoapySDR::Device::unmake(device_); }
            catch (...) { SoapySDR::log(SOAPY_SDR_ERROR, "SoapySDR device release failed"); }
            device_=nullptr;
        }
#ifdef S7_HAVE_IIO
        // Release the driver's USB interface before reopening it to idle the PHY.
        ad936x_.reset();
#endif
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
        if (native_limit_) n=std::min(n,native_limit_);
        const auto stride=signal_type_info(r.type).storage_bytes;
        auto check_ci12=[&](const std::byte* data) {
            if (r.type!=SignalType::CI12) return;
            std::int16_t iq[2]; std::memcpy(iq,data,sizeof(iq));
            if (iq[0]<-2048 || iq[0]>2047 || iq[1]<-2048 || iq[1]>2047)
                throw std::invalid_argument("SoapySDR CI12 value outside significant-bit range");
        };
        if (src && r.type==SignalType::CI12) for (std::size_t i=0;i<n*r.channels.size();++i) check_ci12(src+i*stride);
        const auto* data=dst ? dst : src;
        const auto alignment=r.type==SignalType::C32 ? alignof(float) : alignof(std::int16_t);
        const bool direct=r.channels.size()==1 && reinterpret_cast<std::uintptr_t>(data)%alignment==0;
        if (r.channels.size()==1) {
            pointers_[0]=direct ? dst : planar_.bytes().data();
            const_pointers_[0]=direct ? src : planar_.bytes().data();
        }
        if (src && !direct) for (std::size_t c=0;c<r.channels.size();++c) for (std::size_t i=0;i<n;++i)
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
            if (dst && direct && r.type==SignalType::CI12)
                for (int i=0;i<count;++i) check_ci12(dst+static_cast<std::size_t>(i)*stride);
            if (dst && !direct) for (std::size_t c=0;c<r.channels.size();++c) for (int i=0;i<count;++i) {
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
    std::size_t native_limit_{};
    Buffer planar_,pending_;
    std::vector<void*> pointers_;
    std::vector<const void*> const_pointers_;
    std::size_t pending_frames_{};
    bool active_{};
#ifdef S7_HAVE_IIO
    std::unique_ptr<Ad936xSession> ad936x_;
#endif
};
class Soapy final : public Provider {
public:
    explicit Soapy(bool keep_sdr_active) : keep_sdr_active_(keep_sdr_active) {}
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
        // Continuous settings are realized at the closest value the device can
        // produce: S7 accepts and reports it (requested and actual, ADR-028).
        // An explicit rate_tolerance_ppm is an opt-in strict rate check.
        std::optional<double> rate_tolerance_ppm;
        for (const auto& [key,value]:r.options) {
            if (key=="rate_tolerance_ppm") {
                rate_tolerance_ppm=number(value);
                if (r.direction!=Direction::Capture || *rate_tolerance_ppm<0)
                    throw std::invalid_argument("SoapySDR rate_tolerance_ppm must be nonnegative and is RX-only");
                continue;
            }
            if (key=="gain_mode") {
                if (r.direction!=Direction::Capture || (value!="auto" && value!="manual") ||
                    (value=="auto" && r.options.contains("gain_db")))
                    throw std::invalid_argument("SoapySDR gain_mode must be auto or manual on RX; auto is incompatible with gain_db");
                continue;
            }
            if (key!="frequency_hz" && key!="bandwidth_hz" && key!="gain_db" && key!="antenna") throw std::invalid_argument("unknown SoapySDR option: "+key);
            if (key!="antenna") {
                const auto v=number(value);
                if (key!="gain_db" && v<=0) throw std::invalid_argument("RF frequency/bandwidth must be positive");
            }
        }
        SoapySDR::Device* device=nullptr; SoapySDR::Stream* stream=nullptr;
#ifdef S7_HAVE_IIO
        std::unique_ptr<Ad936xSession> ad936x;
#endif
        try {
            auto selector = SoapySDR::KwargsFromString(r.device);
#ifdef S7_HAVE_IIO
            const bool pluto = is_pluto(selector);
            if (keep_sdr_active_ && !pluto)
                throw std::invalid_argument("Keeping RF active requires PlutoSDR");
            if (pluto) {
                if (!selector.contains("uri")) {
                    if (const auto host = selector.find("hostname"); host != selector.end() && !host->second.empty())
                        selector["uri"] = "ip:" + host->second;
                }
                const auto uri = selector.find("uri");
                if (uri == selector.end() || uri->second.empty() || uri->second == "ip:" || uri->second == "usb:")
                    throw std::invalid_argument("PlutoSDR requires an explicit IIO URI or hostname");
                ad936x = std::make_unique<Ad936xSession>(uri->second, r.direction, keep_sdr_active_);
                ad936x->wake();
            }
#else
            if (keep_sdr_active_)
                throw std::invalid_argument("Keeping PlutoSDR active requires libiio support");
#endif
            device=SoapySDR::Device::make(selector);
            if (!device) throw std::runtime_error("SoapySDR device unavailable");
#ifdef S7_HAVE_IIO
            if (ad936x) ad936x->mark_used();
#endif
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
                const auto prefix="channel."+text+".";
                device->setSampleRate(direction,channel,r.rate.to_hz());
                const auto actual_rate=device->getSampleRate(direction,channel);
                if (!std::isfinite(actual_rate) || actual_rate<=0)
                    throw std::invalid_argument("SoapySDR channel "+text+" reported an invalid sample rate");
                const auto deviation_ppm=(actual_rate-r.rate.to_hz())/r.rate.to_hz()*1e6;
                if (rate_tolerance_ppm && std::abs(deviation_ppm)>*rate_tolerance_ppm)
                    throw std::invalid_argument("SoapySDR channel "+text+
                    " nominal rate differs from graph beyond rate_tolerance_ppm: requested "+r.rate.to_string()+
                    ", got "+decimal(actual_rate)+" Hz");
                // The graph keeps the nominal rate as model time; the device's
                // realized rate is reported, not substituted.
                d.backend_details[prefix+"requested_sample_rate_hz"]=decimal(r.rate.to_hz());
                d.backend_details[prefix+"sample_rate_hz"]=decimal(actual_rate);
                d.backend_details[prefix+"sample_rate_deviation_ppm"]=decimal(deviation_ppm);
                // Tune before setting gain, since tuning may change the device's gain range.
                for (const auto* key:{"antenna","frequency_hz","bandwidth_hz","gain_mode","gain_db"}) {
                    const auto option=r.options.find(key);
                    if (option==r.options.end()) continue;
                    const auto& [name,value]=*option;
                    if (name=="antenna") { device->setAntenna(direction,channel,value); if (device->getAntenna(direction,channel)!=value) throw std::invalid_argument("antenna readback mismatch"); }
                    else if (name=="gain_mode") {
                        const bool automatic=value=="auto";
                        if (!device->hasGainMode(direction,channel)) throw std::invalid_argument("SoapySDR selectable gain mode unsupported");
                        device->setGainMode(direction,channel,automatic);
                        if (device->getGainMode(direction,channel)!=automatic) throw std::invalid_argument("SoapySDR gain mode readback mismatch");
                    }
                    else {
                        // Continuous: the device realizes the closest value it can.
                        const auto v=number(value); double actual=0;
                        if (name=="frequency_hz") { device->setFrequency(direction,channel,v); actual=device->getFrequency(direction,channel); }
                        if (name=="bandwidth_hz") { device->setBandwidth(direction,channel,v); actual=device->getBandwidth(direction,channel); }
                        if (name=="gain_db") {
                            if (device->hasGainMode(direction,channel)) device->setGainMode(direction,channel,false);
                            device->setGain(direction,channel,v); actual=device->getGain(direction,channel);
                        }
                        if (!std::isfinite(actual)) throw std::invalid_argument("SoapySDR channel "+text+" reported an invalid "+name);
                        d.backend_details[prefix+"requested_"+name]=value;
                        d.backend_details[prefix+name]=decimal(actual);
                    }
                }
                if (!d.backend_details.contains(prefix+"frequency_hz"))
                    d.backend_details[prefix+"frequency_hz"]=decimal(device->getFrequency(direction,channel));
            }
            stream=device->setupStream(direction,format,channels);
            if (!stream) throw std::runtime_error("SoapySDR setupStream returned null");
            auto result=std::make_unique<SoapyStream>(std::move(d),device,stream);
#ifdef S7_HAVE_IIO
            result->take_session(std::move(ad936x));
#endif
            device=nullptr; stream=nullptr; return result;
        } catch (...) {
            if (stream) { try { device->closeStream(stream); } catch (...) {} }
            if (device) { try { SoapySDR::Device::unmake(device); } catch (...) {} }
            throw;
        }
    }
private:
    bool keep_sdr_active_{};
};
}
#ifdef S7_HAVE_IIO
bool can_keep_sdr_active(std::string_view device) {
    return is_pluto(SoapySDR::KwargsFromString(std::string(device)));
}
#endif
std::unique_ptr<Provider> soapy_provider(bool keep_sdr_active) {
    return std::make_unique<Soapy>(keep_sdr_active);
}
} // namespace s7::sdr
