// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: LGPL-3.0-or-later
#include <s7/hardware.hpp>
#include <s7/hardware_backend.hpp>
#include <algorithm>
#include <cstring>
#include <limits>
#include <set>
#include <boost/uuid/random_generator.hpp>
#include <boost/uuid/uuid_io.hpp>

namespace s7::hardware {
const char* to_string(Status s) noexcept {
    switch (s) {
    case Status::Ok: return "ok";
    case Status::Timeout: return "timeout";
    case Status::Cancelled: return "cancelled";
    case Status::Overflow: return "source overflow";
    case Status::Underflow: return "sink underflow";
    case Status::ContinuityLost: return "continuity lost";
    case Status::Disconnected: return "disconnected";
    case Status::Busy: return "busy";
    case Status::Unsupported: return "unsupported";
    case Status::InvalidConfiguration: return "invalid configuration";
    case Status::BackendError: return "backend error";
    }
    return "unknown";
}
bool terminal(Status s) noexcept { return s != Status::Ok && s != Status::Timeout; }
void Request::validate() const {
    if (backend.empty() || backend.size()>128 || backend.find('\0')!=std::string::npos ||
        device.empty() || device.size()>4096 || device.find('\0')!=std::string::npos)
        throw std::invalid_argument("hardware backend/device must be explicit and bounded");
    if (rate != Rate::reduced(rate.numerator, rate.denominator))
        throw std::invalid_argument("hardware rate must be reduced");
    if (channels.empty() || channels.size() > 64) throw std::invalid_argument("hardware requires 1..64 channels");
    std::set<std::string> unique;
    for (const auto& c : channels) if (c.empty() || c.size()>128 || c.find('\0')!=std::string::npos || !unique.insert(c).second)
        throw std::invalid_argument("hardware channels must be unique and nonempty");
    if (options.size()>32) throw std::invalid_argument("hardware option count exceeds 32");
    for (const auto& [key,value]:options) if (key.empty() || key.size()>128 || value.size()>4096 ||
        key.find('\0')!=std::string::npos || value.find('\0')!=std::string::npos)
        throw std::invalid_argument("hardware option metadata exceeds bounds");
    if (!block_frames || block_frames > 1048576 || queue_frames < block_frames || queue_frames > 16777216)
        throw std::invalid_argument("hardware block/queue bounds invalid");
    if (frame_bytes() > (256ULL << 20) / queue_frames)
        throw std::invalid_argument("hardware queue exceeds 256 MiB limit");
}
std::size_t Request::frame_bytes() const {
    const auto stride = signal_type_info(type).storage_bytes;
    if (!stride || channels.size() > std::numeric_limits<std::size_t>::max() / stride)
        throw std::invalid_argument("hardware frame size overflow");
    return stride * channels.size();
}
Stream::Stream(Descriptor d) : descriptor_([&] {
    d.actual.validate();
    if (d.actual.direction==Direction::Playback) {
        if (!d.startup_prefill_frames) d.startup_prefill_frames=d.actual.block_frames;
        if (d.startup_prefill_frames>d.actual.queue_frames) throw std::invalid_argument("hardware prefill exceeds queue capacity");
    }
    if (d.session_id.empty()) d.session_id=boost::uuids::to_string(boost::uuids::random_generator{}());
    return std::move(d);
}()) {}
void Stream::start() {
    if (state_ != State::Prepared || cancelled()) throw std::logic_error("hardware stream cannot restart; prepare a new segment");
    try { BackendCallGuard guard; do_start(); state_ = State::Running; }
    catch (...) { state_ = State::Faulted; fault_ = Status::BackendError; stop(); throw; }
}
void Stream::stop() noexcept {
    cancel();
    { BackendCallGuard guard;do_stop(); }
    if (state_ != State::Faulted) state_ = State::Stopped;
}
void Stream::check_buffer(std::size_t bytes, std::size_t frames) const {
    if (!frames || frames > descriptor_.actual.block_frames || frames > bytes / descriptor_.actual.frame_bytes())
        throw std::invalid_argument("hardware transfer exceeds caller/block bounds");
}
Transfer Stream::finish(Transfer r, std::size_t requested) {
    if (r.frames > requested || r.frames > std::numeric_limits<std::uint64_t>::max() - position_) {
        r = {}; r.status = Status::BackendError;
    }
    r.first_frame = position_; position_ += r.frames;
    if (terminal(r.status)) { state_ = State::Faulted; fault_ = r.status; fault_native_error_=r.native_error; }
    return r;
}
Transfer Stream::read(std::span<std::byte> b, std::size_t n, Deadline d, std::stop_token st) {
    check_buffer(b.size(), n);
    if (descriptor_.actual.direction != Direction::Capture) throw std::logic_error("read on playback stream");
    if (state_ == State::Faulted) return {0, fault_, position_, {},fault_native_error_};
    if (cancelled() || st.stop_requested()) return finish({0, Status::Cancelled, 0, {}}, n);
    if (state_ != State::Running) throw std::logic_error("capture stream is not running");
    try { BackendCallGuard guard;return finish(do_read(b, n, d, st), n); }
    catch (...) { state_=State::Faulted; fault_=Status::BackendError; stop(); throw; }
}
Transfer Stream::write(std::span<const std::byte> b, std::size_t n, Deadline d, std::stop_token st) {
    check_buffer(b.size(), n);
    if (descriptor_.actual.direction != Direction::Playback) throw std::logic_error("write on capture stream");
    if (state_ == State::Faulted) return {0, fault_, position_, {},fault_native_error_};
    if (cancelled() || st.stop_requested()) return finish({0, Status::Cancelled, 0, {}}, n);
    if (state_ != State::Running && state_ != State::Prepared) throw std::logic_error("playback stream is stopped");
    try { BackendCallGuard guard;return finish(do_write(b, n, d, st), n); }
    catch (...) { state_=State::Faulted; fault_=Status::BackendError; stop(); throw; }
}
Transfer Stream::do_read(std::span<std::byte>, std::size_t, Deadline, std::stop_token) { return {0, Status::Unsupported, 0, {}}; }
Transfer Stream::do_write(std::span<const std::byte>, std::size_t, Deadline, std::stop_token) { return {0, Status::Unsupported, 0, {}}; }
Buffer::Buffer(std::size_t n) : data_(n / sizeof(std::max_align_t) + (n % sizeof(std::max_align_t) != 0)), size_(n) {
    if (n) std::memset(data_.data(),0,n);
}
Boundary::Boundary(std::string_view name, Request r) : Element(name, true), request_([&]{r.validate();return std::move(r);}()), value_(request_.frame_bytes()) {
    auto b = value_.bytes();
    const auto stride = signal_type_info(request_.type).storage_bytes;
    for (std::size_t i=0; i<request_.channels.size(); ++i) {
        if (request_.direction == Direction::Capture) register_output(i, request_.channels[i], request_.type, b.data()+i*stride);
        else register_input(i, request_.channels[i], request_.type, nullptr);
    }
}
Source::Source(Request r, std::string_view name) : Boundary(name, [&] { r.direction=Direction::Capture; return std::move(r); }()) {}
Sink::Sink(Request r, std::string_view name) : Boundary(name, [&] { r.direction=Direction::Playback; return std::move(r); }()) {}
void Boundary::stage(std::span<std::byte> b, std::size_t n) {
    if (faulted_ || cursor_ != frames_) throw std::logic_error("hardware boundary fault or incomplete previous block");
    if (!n || n > request_.block_frames || n > b.size()/request_.frame_bytes()) throw std::invalid_argument("hardware staging bounds");
    block_ = b; frames_=n; cursor_=0;
}
void Boundary::unstage() noexcept { block_={}; frames_=cursor_=0; }
void Boundary::step() {
    if (faulted_ || cursor_ >= frames_) throw std::runtime_error("hardware boundary requires explicit Run activation; faulted runs require a new graph");
    auto* frame = block_.data() + cursor_*request_.frame_bytes();
    const auto stride = signal_type_info(request_.type).storage_bytes;
    if (request_.direction == Direction::Capture) std::memcpy(value_.bytes().data(), frame, request_.frame_bytes());
    else for (std::size_t i=0; i<input_count(); ++i) std::memcpy(frame+i*stride,input_binding(i),stride);
    ++cursor_;
}
namespace {
struct Profile {
    Boundary* primary{};
    Boundary* audio{};
    CaptureBlockSource* block_source{};
    std::uint64_t audio_divisor{};

    GraphProfile graph_profile() const {
        if (block_source) return {block_source->request(), *block_source->declared_rate(),
                                  block_source->request().block_frames, block_source->type_name()};
        return {primary->request(), primary->request().rate, 1, primary->type_name()};
    }
};

Profile profile(const ExecutableGraph& g) {
    Profile result;
    Boundary* playback=nullptr;
    for (const auto& [id, e] : g.elements) {
        if (auto* b = dynamic_cast<Boundary*>(e.get())) {
            auto*& slot = b->request().direction == Direction::Capture ? result.primary : playback;
            if (slot) throw std::invalid_argument("hardware run supports at most one capture and one playback boundary");
            slot=b;
        } else if (auto* source = dynamic_cast<CaptureBlockSource*>(e.get())) {
            if (result.block_source) throw std::invalid_argument("hardware run supports only one block source");
            result.block_source=source;
        }
    }
    if (result.block_source) {
        if (result.primary || playback || g.domains.size()!=1 ||
            g.domains.front().rate!=*result.block_source->declared_rate())
            throw std::invalid_argument("Block capture requires one hardware boundary and one block-rate domain");
        return result;
    }
    if (!result.primary) {
        if (!playback) throw std::invalid_argument("hardware run has no boundary");
        if (g.domains.size()!=1 || g.domains.front().rate!=playback->request().rate)
            throw std::invalid_argument("playback-only hardware run requires one matching rate domain");
        result.primary=playback;
        return result;
    }

    const auto rate=result.primary->request().rate;
    // Every other domain must be downstream of capture and no faster than it.
    std::vector<bool> downstream(g.domains.size(), false);
    for (std::size_t i=0;i<g.domains.size();++i) downstream[i]=g.domains[i].rate==rate;
    for (bool changed=true;changed;) {
        changed=false;
        for (const auto& [input, output]:g.domain_links) {
            if (downstream[input] && !downstream[output]) downstream[output]=changed=true;
        }
    }
    for (std::size_t i=0;i<g.domains.size();++i) {
        const auto other=g.domains[i].rate;
        if (!downstream[i] || static_cast<uint128_t>(other.numerator)*rate.denominator >
            static_cast<uint128_t>(rate.numerator)*other.denominator)
            throw std::invalid_argument("capture hardware run requires matching or slower downstream rate domains");
    }
    if (playback) {
        const auto audio_rate=playback->request().rate;
        if (playback->type_name()!="AudioSink" || rate.denominator!=1 || audio_rate.denominator!=1 ||
            rate.numerator % audio_rate.numerator || rate==audio_rate)
            throw std::invalid_argument("capture can feed one AudioSink at an integer divisor of the capture rate");
        result.audio=playback;
        result.audio_divisor=rate.numerator/audio_rate.numerator;
    }
    return result;
}

std::unique_ptr<Stream> prepare_stream(Provider* provider, const Request& r) {
    if (!provider || provider->id()!=r.backend) throw std::invalid_argument("requested hardware provider is unavailable: "+r.backend);
    BackendCallGuard guard;
    auto stream=provider->prepare(r);
    if (!stream) throw std::runtime_error("hardware provider returned no stream");
    const auto& a=stream->descriptor().actual;
    if (a.backend!=r.backend || a.device!=r.device || a.direction!=r.direction || a.type!=r.type ||
        a.rate!=r.rate || a.channels!=r.channels || a.block_frames!=r.block_frames || a.queue_frames!=r.queue_frames) {
        stream->stop(); throw std::invalid_argument("hardware negotiated descriptor differs from graph request");
    }
    return stream;
}
}

bool has_hardware_boundary(const ExecutableGraph& g) {
    return std::ranges::any_of(g.elements, [](const auto& entry) {
        return dynamic_cast<const Boundary*>(entry.second.get()) ||
               dynamic_cast<const CaptureBlockSource*>(entry.second.get());
    });
}

GraphProfile graph_profile(const ExecutableGraph& g) { return profile(g).graph_profile(); }

Boundary& validate_graph(const ExecutableGraph& g) {
    const auto config=profile(g);
    if (!config.primary) throw std::invalid_argument("graph does not have a scalar hardware boundary");
    return *config.primary;
}

const Request& Run::request() const noexcept {
    if (auto* source=dynamic_cast<const CaptureBlockSource*>(boundary_)) return source->request();
    return static_cast<const Boundary&>(*boundary_).request();
}

Run::Run(ExecutableGraph g) : block_(0) {
    const auto config=profile(g);
    boundary_=config.block_source ? static_cast<Element*>(config.block_source) : config.primary;
    audio_sink_=config.audio;
    audio_divisor_=config.audio_divisor;
    const auto& r=request();
    const auto graph_rate=config.graph_profile().graph_rate;
    runtime_=std::make_unique<NodeRuntime>(std::move(g));
    runtime_->set_model_time_rate(graph_rate);
    if (!config.block_source || r.channels.size()>1) block_=Buffer(r.block_frames*r.frame_bytes());
    if (audio_sink_) {
        const auto& audio=audio_sink_->request();
        audio_block_=Buffer(audio.block_frames*audio.frame_bytes());
    }
}

Run::Run(ExecutableGraph g, Resolve resolve) : Run(std::move(g)) { prepare(std::move(resolve)); }

void Run::prepare(Resolve resolve) {
    if (stream_) throw std::logic_error("hardware run already prepared");
    std::unique_ptr<Provider> audio_provider;
    std::unique_ptr<Stream> audio_stream;
    if (audio_sink_) {
        const auto& audio=audio_sink_->request();
        audio_provider=resolve(audio.backend);
        audio_stream=prepare_stream(audio_provider.get(), audio);
    }
    const auto& r=request();
    auto provider=resolve(r.backend);
    stream_=prepare_stream(provider.get(), r);
    provider_=std::move(provider);
    audio_provider_=std::move(audio_provider);
    audio_stream_=std::move(audio_stream);
}

const Descriptor& Run::descriptor() const {
    if (!stream_) throw std::logic_error("hardware run is not prepared");
    return stream_->descriptor();
}

Run::~Run() {
    if (stream_) stream_->stop();
    if (audio_stream_) audio_stream_->stop();
}

void Run::step_to(RunStats& stats, std::uint64_t end, const StepObserver& observe) {
    while (stats.graph_frames<end) {
        runtime_->step();
        ++stats.graph_frames;
        if (observe) observe(stats.graph_frames);
    }
}

RunStats Run::execute_capture(std::uint64_t frames, std::chrono::milliseconds timeout, std::stop_token st,
                              const StepObserver& observe) {
    RunStats stats;
    auto& boundary=static_cast<Boundary&>(*boundary_);
    std::stop_callback cancel_capture(st, [this] { cancel(); });
    const auto interrupted=[&] { return stream_->cancelled(); };
    const auto finish=[&] {
        boundary.on_step_failure();
        boundary.unstage();
        stream_->stop();
        if (audio_sink_) {
            audio_sink_->on_step_failure();
            audio_sink_->unstage();
            audio_stream_->stop();
            stats.emergency_output_frames=audio_stream_->emergency_output_frames();
        }
    };
    const auto audio_frames_through=[&](std::uint64_t ticks) {
        // Tick zero produces the first audio frame. Count subsequent audio
        // ticks on the same model-time grid used by NodeRuntime.
        return ticks ? (ticks-1)/audio_divisor_+1 : 0;
    };
    const auto prefill=audio_stream_ ? audio_stream_->descriptor().startup_prefill_frames : 0;
    const auto audio_stride=audio_sink_ ? audio_sink_->request().frame_bytes() : 0;
    auto block_limit=boundary.request().block_frames;
    if (audio_sink_ && (block_limit-1)/audio_divisor_+1 > audio_sink_->request().block_frames)
        block_limit=audio_sink_->request().block_frames*audio_divisor_;

    try {
        if (!interrupted()) stream_->start();
        while (stats.graph_frames<frames && !interrupted()) {
            const auto limit=static_cast<std::size_t>(std::min<std::uint64_t>(block_limit, frames-stats.graph_frames));
            const auto input=stream_->read(block_.bytes(),limit,std::chrono::steady_clock::now()+timeout,st);
            stats.transferred_frames+=input.frames;
            if (input.frames) {
                const auto end=stats.graph_frames+input.frames;
                const auto count=audio_sink_ ? audio_frames_through(end)-audio_frames_through(stats.graph_frames) : 0;
                if (count) audio_sink_->stage(audio_block_.bytes(),count);
                boundary.stage(block_.bytes(),input.frames);
                step_to(stats,end,observe);
                boundary.unstage();
                if (count) {
                    audio_sink_->unstage();
                    const auto deadline=std::chrono::steady_clock::now()+timeout;
                    for (std::size_t sent=0;sent<count && !interrupted();) {
                        auto pending=count-sent;
                        if (stats.playback_frames<prefill) pending=std::min(pending,prefill-stats.playback_frames);
                        const auto out=audio_stream_->write(audio_block_.bytes().subspan(sent*audio_stride),pending,deadline,st);
                        sent+=out.frames;
                        stats.playback_frames+=out.frames;
                        if (out.status!=Status::Ok || !out.frames) {
                            stats.status=out.status==Status::Ok ? Status::BackendError : out.status;
                            stats.native_error=out.native_error;
                            break;
                        }
                        if (audio_stream_->state()==State::Prepared && stats.playback_frames==prefill && !interrupted())
                            audio_stream_->start();
                    }
                }
            }
            if (input.status!=Status::Ok || !input.frames) {
                stats.status=input.status==Status::Ok ? Status::BackendError : input.status;
                stats.native_error=input.native_error;
            }
            if (stats.status!=Status::Ok) break;
        }
        if (interrupted()) stats.status=Status::Cancelled;
        else if (stats.status==Status::Ok && audio_stream_ && audio_stream_->state()==State::Prepared)
            audio_stream_->start();
    } catch (...) {
        finish();
        throw;
    }
    finish();
    return stats;
}

RunStats Run::execute(std::uint64_t frames, std::chrono::milliseconds timeout, std::stop_token st,
                      const StepObserver& observe) {
    if (!stream_) throw std::logic_error("hardware run is not prepared");
    if (executed_ || !frames || timeout.count()<=0) throw std::invalid_argument("hardware run is single-use and needs positive frames/timeout");
    executed_=true;
    if (dynamic_cast<CaptureBlockSource*>(boundary_)) return execute_block_capture(frames,timeout,st,observe);
    auto& boundary=static_cast<Boundary&>(*boundary_);
    RunStats stats;
    const auto& r=request();
    if (r.direction==Direction::Capture) return execute_capture(frames,timeout,st,observe);
    bool started=false;
    std::size_t prefilled=0;
    try {
        while (stats.graph_frames<frames && !st.stop_requested() && !stream_->cancelled()) {
            auto count=static_cast<std::size_t>(std::min<std::uint64_t>(r.block_frames, frames-stats.graph_frames));
            if (!started)
                count=std::min(count,stream_->descriptor().startup_prefill_frames-prefilled);
            if (count) {
                boundary.stage(block_.bytes(),count);
                step_to(stats,stats.graph_frames+count,observe);
                boundary.unstage();
            }
            std::size_t sent=0;
            const auto deadline=std::chrono::steady_clock::now()+timeout;
            while (sent<count) {
                auto out=stream_->write(block_.bytes().subspan(sent*r.frame_bytes()),count-sent,deadline,st);
                stats.transferred_frames+=out.frames; sent+=out.frames;
                if (out.status!=Status::Ok || !out.frames) { stats.status=out.status==Status::Ok?Status::BackendError:out.status;stats.native_error=out.native_error; break; }
            }
            if (stats.status!=Status::Ok) break;
            if (!started) prefilled+=sent;
            if (!started && (prefilled==stream_->descriptor().startup_prefill_frames || stats.graph_frames==frames)) {
                if (st.stop_requested() || stream_->cancelled()) { stats.status=Status::Cancelled; break; }
                stream_->start(); started=true;
            }
        }
        if (st.stop_requested() || stream_->cancelled()) stats.status=Status::Cancelled;
    } catch (...) {
        boundary.on_step_failure(); boundary.unstage(); stream_->stop(); throw;
    }
    boundary.on_step_failure(); boundary.unstage(); stream_->stop();
    stats.emergency_output_frames=stream_->emergency_output_frames();
    return stats;
}
} // namespace s7::hardware
