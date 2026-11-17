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
Boundary& validate_graph(const ExecutableGraph& g) {
    Boundary* boundary=nullptr;
    for (const auto& [id, e] : g.elements) {
        if (auto* b = dynamic_cast<Boundary*>(e.get())) {
            if (boundary) throw std::invalid_argument("hardware v1 requires one boundary: independent/duplex clocks are not yet qualified");
            boundary=b;
        }
    }
    if (!boundary) throw std::invalid_argument("hardware run has no boundary");
    if (g.domains.size()!=1 || g.domains[0].rate != boundary->request().rate)
        throw std::invalid_argument("hardware v1 requires one matching rate domain");
    return *boundary;
}
Run::Run(ExecutableGraph g, Resolve resolve) : block_(0) {
    boundary_=&validate_graph(g);
    const auto& r=boundary_->request();
    runtime_=std::make_unique<NodeRuntime>(std::move(g));
    runtime_->set_model_time_rate(r.rate);
    block_=Buffer(r.block_frames*r.frame_bytes());
    provider_=resolve(r.backend);
    if (!provider_ || provider_->id()!=r.backend) throw std::invalid_argument("requested hardware provider is unavailable: "+r.backend);
    { BackendCallGuard guard;stream_=provider_->prepare(r); }
    if (!stream_) throw std::runtime_error("hardware provider returned no stream");
    const auto& a=stream_->descriptor().actual;
    if (a.backend!=r.backend || a.device!=r.device || a.direction!=r.direction || a.type!=r.type ||
        a.rate!=r.rate || a.channels!=r.channels || a.block_frames!=r.block_frames || a.queue_frames!=r.queue_frames) {
        stream_->stop(); throw std::invalid_argument("hardware negotiated descriptor differs from graph request");
    }
}
Run::~Run() { if (stream_) stream_->stop(); }
RunStats Run::execute(std::uint64_t frames, std::chrono::milliseconds timeout, std::stop_token st) {
    if (executed_ || !frames || timeout.count()<=0) throw std::invalid_argument("hardware run is single-use and needs positive frames/timeout");
    executed_=true;
    RunStats stats;
    const auto& r=boundary_->request();
    bool started=false;
    std::size_t prefilled=0;
    try {
        if (r.direction==Direction::Capture && !st.stop_requested()) { stream_->start(); started=true; }
        while (stats.graph_frames<frames && !st.stop_requested() && !stream_->cancelled()) {
            auto count=static_cast<std::size_t>(std::min<std::uint64_t>(r.block_frames, frames-stats.graph_frames));
            if (r.direction==Direction::Playback && !started)
                count=std::min(count,stream_->descriptor().startup_prefill_frames-prefilled);
            Transfer input;
            if (r.direction==Direction::Capture) {
                input=stream_->read(block_.bytes(),count,std::chrono::steady_clock::now()+timeout,st);
                count=input.frames; stats.transferred_frames+=count;
            }
            if (count) {
                boundary_->stage(block_.bytes(),count);
                for (std::size_t i=0;i<count;++i) { runtime_->step(); ++stats.graph_frames; }
                boundary_->unstage();
            }
            if (r.direction==Direction::Playback) {
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
            } else if (input.status!=Status::Ok || !count) {
                stats.status=input.status==Status::Ok?Status::BackendError:input.status;stats.native_error=input.native_error; break;
            }
        }
        if (st.stop_requested() || stream_->cancelled()) stats.status=Status::Cancelled;
    } catch (...) {
        boundary_->on_step_failure(); boundary_->unstage(); stream_->stop(); throw;
    }
    boundary_->on_step_failure(); boundary_->unstage(); stream_->stop();
    stats.emergency_output_frames=stream_->emergency_output_frames();
    return stats;
}
} // namespace s7::hardware
