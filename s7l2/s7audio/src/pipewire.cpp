// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: LGPL-3.0-or-later
#include "callback_stream.hpp"
#include <pipewire/pipewire.h>
#include <spa/param/audio/format-utils.h>
#include <spa/buffer/meta.h>
#include <mutex>

namespace s7::audio {
using namespace hardware;
void initialize_pipewire() { static std::once_flag once; std::call_once(once,[]{ pw_init(nullptr,nullptr); }); }
namespace {
class PwStreamOwner final : public detail::CallbackStream {
public:
    explicit PwStreamOwner(const Request& r) : CallbackStream(detail::audio_descriptor(r)) {
        detail::validate_audio(r);
        for (const auto& [key,value]:r.options) {
            if (key=="underrun") continue; // Validated by CallbackStream.
            if (key!="capture_sink" || (value!="true" && value!="false") || r.direction!=Direction::Capture)
                throw std::invalid_argument("PipeWire options are boolean capture_sink on capture and underrun on playback");
        }
        initialize_pipewire();
        try {
            loop_=pw_thread_loop_new("s7-audio",nullptr);
            if (!loop_) throw std::runtime_error("PipeWire thread loop allocation failed");
            const auto rate=std::to_string(r.rate.numerator);
            const auto latency=std::to_string(r.block_frames)+"/"+rate;
            auto* props=pw_properties_new(PW_KEY_MEDIA_TYPE,"Audio",PW_KEY_MEDIA_CATEGORY,
                r.direction==Direction::Capture?"Capture":"Playback",PW_KEY_MEDIA_ROLE,"Production",
                PW_KEY_NODE_LATENCY,latency.c_str(),PW_KEY_STREAM_DONT_REMIX,"true",nullptr);
            if (!props) throw std::runtime_error("PipeWire properties allocation failed");
            pw_properties_set(props,PW_KEY_NODE_DONT_RECONNECT,"true");
            pw_properties_set(props,"node.dont-move","true");
            if (r.device!="default") {
                pw_properties_set(props,PW_KEY_TARGET_OBJECT,r.device.c_str());
                pw_properties_set(props,"node.dont-fallback","true");
            }
            if (r.options.contains("capture_sink")) pw_properties_set(props,PW_KEY_STREAM_CAPTURE_SINK,r.options.at("capture_sink").c_str());
            stream_=pw_stream_new_simple(pw_thread_loop_get_loop(loop_),"S7 audio",props,&events(),this);
            if (!stream_) throw std::runtime_error("PipeWire stream creation failed");
            std::uint8_t pod_buffer[2048];
            auto builder=SPA_POD_BUILDER_INIT(pod_buffer,sizeof(pod_buffer));
            spa_audio_info_raw raw{}; raw.format=format(); raw.rate=static_cast<std::uint32_t>(r.rate.numerator);
            raw.channels=static_cast<std::uint32_t>(r.channels.size()); raw.flags=SPA_AUDIO_FLAG_UNPOSITIONED;
            const spa_pod* params[]={spa_format_audio_raw_build(&builder,SPA_PARAM_EnumFormat,&raw)};
            const auto flags=static_cast<pw_stream_flags>(PW_STREAM_FLAG_AUTOCONNECT|PW_STREAM_FLAG_MAP_BUFFERS|
                PW_STREAM_FLAG_RT_PROCESS|PW_STREAM_FLAG_INACTIVE);
            if (pw_stream_connect(stream_,r.direction==Direction::Capture?PW_DIRECTION_INPUT:PW_DIRECTION_OUTPUT,
                                  PW_ID_ANY,flags,params,1)<0) throw std::runtime_error("PipeWire stream connection failed");
            if (pw_thread_loop_start(loop_)<0) throw std::runtime_error("PipeWire loop start failed");
            loop_started_=true;
            pw_thread_loop_lock(loop_);
            // Bounded preparation, including a missing server/target. No device
            // samples are captured/played while the stream is inactive.
            timespec deadline{}; pw_thread_loop_get_time(loop_,&deadline,3'000'000'000LL);
            while (state_==PW_STREAM_STATE_UNCONNECTED || state_==PW_STREAM_STATE_CONNECTING) {
                if (pw_thread_loop_timed_wait_full(loop_,&deadline)<0) break;
            }
            const bool ready=state_==PW_STREAM_STATE_PAUSED && callback_status()==Status::Ok;
            pw_thread_loop_unlock(loop_);
            if (!ready) throw std::runtime_error("PipeWire preparation failed or timed out (server/target unavailable)");
        } catch (...) { cleanup(); throw; }
    }
    ~PwStreamOwner() override { stop(); cleanup(); }
protected:
    void do_start() override {
        pw_thread_loop_lock(loop_);
        const int result=pw_stream_set_active(stream_,true);
        pw_thread_loop_unlock(loop_);
        if (result<0) throw std::runtime_error("PipeWire activation failed");
    }
    void do_stop() noexcept override {
        // Streams are single-use. Destroy/quiesce the callback owner here so
        // stop returns with stable counters and no remaining buffer callbacks.
        cleanup();
    }
private:
    spa_audio_format format() const noexcept {
        const auto t=descriptor().actual.type;
        return t==SignalType::F32?SPA_AUDIO_FORMAT_F32:t==SignalType::I16?SPA_AUDIO_FORMAT_S16:SPA_AUDIO_FORMAT_S32;
    }
    void cleanup() noexcept {
        if (loop_started_) pw_thread_loop_lock(loop_);
        if (stream_) { pw_stream_destroy(stream_); stream_=nullptr; }
        if (loop_started_) { pw_thread_loop_unlock(loop_); pw_thread_loop_stop(loop_); loop_started_=false; }
        if (loop_) { pw_thread_loop_destroy(loop_); loop_=nullptr; }
    }
    static const pw_stream_events& events() {
        static const auto e=[] {
            pw_stream_events v{}; v.version=PW_VERSION_STREAM_EVENTS;
            v.state_changed=[](void* p,pw_stream_state old,pw_stream_state next,const char*) {
                auto& s=*static_cast<PwStreamOwner*>(p); s.state_=next;
                if (next==PW_STREAM_STATE_ERROR || (next==PW_STREAM_STATE_UNCONNECTED && old!=PW_STREAM_STATE_UNCONNECTED))
                    s.fault(Status::Disconnected);
                pw_thread_loop_signal(s.loop_,false);
            };
            v.param_changed=[](void* p,std::uint32_t id,const spa_pod* param) {
                if (id!=SPA_PARAM_Format || !param) return;
                auto& s=*static_cast<PwStreamOwner*>(p); spa_audio_info_raw raw{};
                if (spa_format_audio_raw_parse(param,&raw)<0 || raw.format!=s.format() ||
                    raw.rate!=s.descriptor().actual.rate.numerator || raw.channels!=s.descriptor().actual.channels.size())
                    s.fault(Status::InvalidConfiguration);
            };
            v.process=[](void* p) { static_cast<PwStreamOwner*>(p)->process(); };
            return v;
        }();
        return e;
    }
    void process() noexcept {
        auto* buffer=pw_stream_dequeue_buffer(stream_);
        if (!buffer) { fault(Status::ContinuityLost); return; }
        const bool capture=descriptor().actual.direction==Direction::Capture;
        const auto stride=descriptor().actual.frame_bytes();
        auto* b=buffer->buffer;
        if (!b || b->n_datas!=1 || !b->datas[0].chunk) {
            fault(Status::BackendError); pw_stream_queue_buffer(stream_,buffer); return;
        }
        auto& data=b->datas[0]; auto& chunk=*data.chunk;
        if (capture) {
            const auto* header=static_cast<const spa_meta_header*>(spa_buffer_find_meta_data(b,SPA_META_Header,sizeof(spa_meta_header)));
            // An initial discontinuity starts this stream's new segment; it
            // cannot describe a gap in samples already delivered by this run.
            if ((header && ((header->flags&SPA_META_HEADER_FLAG_CORRUPTED) ||
                            (capture_started_ && (header->flags&SPA_META_HEADER_FLAG_DISCONT)))) ||
                (chunk.flags&SPA_CHUNK_FLAG_CORRUPTED)) fault(Status::ContinuityLost);
            const bool silence=(chunk.flags&SPA_CHUNK_FLAG_EMPTY) || (header && (header->flags&SPA_META_HEADER_FLAG_GAP));
            // Reject wrapping/strided chunks rather than trusting byte offsets.
            if (chunk.offset>data.maxsize || chunk.size>data.maxsize-chunk.offset || chunk.size%stride ||
                (chunk.stride && chunk.stride!=static_cast<std::int32_t>(stride))) fault(Status::BackendError);
            else {
                // GAP/EMPTY mean media-neutral data in SPA, not loss. Their
                // backing memory need not contain initialized PCM samples.
                if (silence) capture_silence(chunk.size/stride);
                else if (!data.data) fault(Status::BackendError);
                else this->capture(static_cast<const std::byte*>(data.data)+chunk.offset,chunk.size/stride);
                if (chunk.size) capture_started_=true;
            }
        } else {
            if (!data.data) { fault(Status::BackendError);pw_stream_queue_buffer(stream_,buffer);return; }
            std::size_t frames=data.maxsize/stride;
            if (buffer->requested) frames=std::min<std::uint64_t>(frames,buffer->requested);
            playback(data.data,frames);
            chunk.offset=0; chunk.stride=static_cast<std::int32_t>(stride); chunk.size=static_cast<std::uint32_t>(frames*stride);
            chunk.flags=0; buffer->size=frames;
        }
        if (const auto result=pw_stream_queue_buffer(stream_,buffer);result<0) fault(Status::BackendError,result);
    }
    pw_thread_loop* loop_{};
    pw_stream* stream_{};
    pw_stream_state state_{PW_STREAM_STATE_UNCONNECTED}; // Protected by thread-loop lock.
    bool loop_started_{};
    bool capture_started_{}; // RT process owner only.
};
class PipeWire final : public Provider {
public:
    std::string_view id() const noexcept override { return "pipewire"; }
    std::vector<DeviceInfo> devices() override {
        // Explicit default target discovery, no fabricated physical identity.
        // Named node/object.serial selectors can be obtained from `pw-dump`.
        return {{"pipewire","default","PipeWire policy-selected audio endpoint","OS policy, physical identity unknown",Support::Unknown,Support::Unknown}};
    }
    std::unique_ptr<Stream> prepare(const Request& r) override {
        if (r.backend!=id()) throw std::invalid_argument("PipeWire backend mismatch");
        return std::make_unique<PwStreamOwner>(r);
    }
};
}
std::unique_ptr<Provider> pipewire_provider() { return std::make_unique<PipeWire>(); }
} // namespace s7::audio
