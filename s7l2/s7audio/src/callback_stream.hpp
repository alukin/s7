// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once
#include <s7/hardware.hpp>
#include <algorithm>
#include <cstring>
#include <limits>
#include <thread>

namespace s7::audio::detail {
using namespace hardware;
// One producer, one consumer. Producer publishes complete interleaved frames.
// No mutex, allocation, logging or graph calls on either callback path.
class Ring {
public:
    explicit Ring(const Request& r) : bytes_(r.queue_frames*r.frame_bytes()), capacity_(r.queue_frames), stride_(r.frame_bytes()) {}
    std::size_t push(const std::byte* data,std::size_t frames) noexcept {
        auto w=write_.load(std::memory_order_relaxed);
        const auto rd=read_.load(std::memory_order_acquire);
        const auto n=std::min<std::uint64_t>(frames,capacity_-(w-rd));
        copy_in(data,w,n); write_.store(w+n,std::memory_order_release); return n;
    }
    std::size_t pop(std::byte* data,std::size_t frames) noexcept {
        auto rd=read_.load(std::memory_order_relaxed);
        const auto w=write_.load(std::memory_order_acquire);
        const auto n=std::min<std::uint64_t>(frames,w-rd);
        const auto first=std::min<std::uint64_t>(n,capacity_-rd%capacity_);
        std::memcpy(data,bytes_.data()+(rd%capacity_)*stride_,first*stride_);
        std::memcpy(data+first*stride_,bytes_.data(),(n-first)*stride_);
        read_.store(rd+n,std::memory_order_release); return n;
    }
private:
    void copy_in(const std::byte* d,std::uint64_t w,std::size_t n) noexcept {
        const auto first=std::min<std::uint64_t>(n,capacity_-w%capacity_);
        if (d) {
            std::memcpy(bytes_.data()+(w%capacity_)*stride_,d,first*stride_);
            std::memcpy(bytes_.data(),d+first*stride_,(n-first)*stride_);
        } else {
            std::memset(bytes_.data()+(w%capacity_)*stride_,0,first*stride_);
            std::memset(bytes_.data(),0,(n-first)*stride_);
        }
    }
    std::vector<std::byte> bytes_;
    std::uint64_t capacity_,stride_;
    alignas(64) std::atomic<std::uint64_t> write_{};
    alignas(64) std::atomic<std::uint64_t> read_{};
};
static_assert(std::atomic<std::uint64_t>::is_always_lock_free);
static_assert(std::atomic<Status>::is_always_lock_free);

class CallbackStream : public Stream {
public:
    explicit CallbackStream(Descriptor d) : Stream(std::move(d)), ring_(descriptor().actual) {}
    std::uint64_t emergency_output_frames() const noexcept override { return emergency_.load(); }
    void fault(Status s,int native_error=0) noexcept {
        std::uint64_t expected=0;
        const auto event=(static_cast<std::uint64_t>(static_cast<std::uint32_t>(native_error))<<32)|static_cast<std::uint32_t>(s);
        fault_.compare_exchange_strong(expected,event,std::memory_order_release);
    }
    Status callback_status() const noexcept { return static_cast<Status>(fault_.load(std::memory_order_acquire)&0xffffffffu); }
    int callback_error() const noexcept { return static_cast<std::int32_t>(fault_.load(std::memory_order_acquire)>>32); }
    void capture(const void* p,std::size_t n) noexcept {
        if (callback_status()!=Status::Ok || cancelled()) return;
        if (!p) { fault(Status::ContinuityLost); return; }
        if (ring_.push(static_cast<const std::byte*>(p),n)!=n) fault(Status::Overflow);
    }
    void capture_silence(std::size_t n) noexcept {
        if (callback_status()!=Status::Ok || cancelled()) return;
        if (ring_.push(nullptr,n)!=n) fault(Status::Overflow);
    }
    void playback(void* p,std::size_t n) noexcept {
        if (!p) { fault(Status::BackendError); return; }
        if (n>std::numeric_limits<std::size_t>::max()/descriptor().actual.frame_bytes()) { fault(Status::BackendError); return; }
        std::size_t count=0;
        if (callback_status()==Status::Ok && !cancelled()) count=ring_.pop(static_cast<std::byte*>(p),n);
        if (count<n) {
            std::memset(static_cast<std::byte*>(p)+count*descriptor().actual.frame_bytes(),0,(n-count)*descriptor().actual.frame_bytes());
            emergency_.fetch_add(n-count,std::memory_order_relaxed);
            if (!cancelled()) fault(Status::Underflow);
        }
    }
protected:
    Transfer do_read(std::span<std::byte> b,std::size_t n,Deadline d,std::stop_token st) override {
        return transfer(b.data(),nullptr,n,d,st);
    }
    Transfer do_write(std::span<const std::byte> b,std::size_t n,Deadline d,std::stop_token st) override {
        return transfer(nullptr,b.data(),n,d,st);
    }
private:
    Transfer transfer(std::byte* dst,const std::byte* src,std::size_t n,Deadline d,std::stop_token st) {
        std::size_t done=0;
        const auto stride=descriptor().actual.frame_bytes();
        while (done<n) {
            if (cancelled() || st.stop_requested()) return {done,Status::Cancelled,0,{}};
            auto status=callback_status();
            // Capture drains its trustworthy queued prefix before terminal.
            if (src && status!=Status::Ok) return {done,status,0,{},callback_error()};
            const auto count=dst?ring_.pop(dst+done*stride,n-done):ring_.push(src+done*stride,n-done);
            done+=count;
            if (done==n) break;
            status=callback_status();
            if (status!=Status::Ok) {
                // The fault may have been published just after pop observed an
                // older write index. Acquire the terminal first, then drain its
                // preceding complete-frame prefix before returning the fault.
                if (dst) done+=ring_.pop(dst+done*stride,n-done);
                if (dst && done==n) return {done,Status::Ok,0,{}};
                return {done,status,0,{},callback_error()};
            }
            if (std::chrono::steady_clock::now()>=d) return {done,Status::Timeout,0,{}};
            std::this_thread::sleep_for(std::chrono::microseconds(200));
        }
        // A capture terminal is returned on the next read after queued prefix;
        // never hide queued frames just because the producer failed later.
        return {done,src?callback_status():Status::Ok,0,{},src?callback_error():0};
    }
    Ring ring_;
    std::atomic<std::uint64_t> fault_{}; // Status and native code published together.
    std::atomic<std::uint64_t> emergency_{};
};

inline void validate_audio(const Request& r) {
    r.validate();
    if (r.type!=SignalType::F32 && r.type!=SignalType::I16 && r.type!=SignalType::I32)
        throw std::invalid_argument("audio supports F32, I16 and I32 application PCM");
    // v1 contiguous audio positions, explicitly named, no silent channel map.
    for (std::size_t i=0;i<r.channels.size();++i) if (r.channels[i]!=std::to_string(i))
        throw std::invalid_argument("audio v1 channels must be ordered contiguous IDs starting at 0");
    if (r.rate.denominator!=1 || r.rate.numerator>768000) throw std::invalid_argument("audio requires an integer nominal rate <= 768000");
}
inline Descriptor audio_descriptor(const Request& r) {
    Descriptor d; d.actual=r;
    if (r.direction==Direction::Playback) d.startup_prefill_frames=r.queue_frames;
    d.amplitude=r.type==SignalType::F32?"normalized application PCM, nominal [-1,1]":"signed full-scale application PCM";
    if (r.backend=="pipewire") d.backend_details["target_selection"]=r.device=="default"?
        "PipeWire policy-selected; physical target identity unobserved":"explicit PipeWire node/object selector; physical identity unverified";
    // Neither API proves physical converter format, rate or identity here.
    return d;
}
} // namespace s7::audio::detail
