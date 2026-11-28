// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include <s7/array_port.hpp>
#include <s7/element.hpp>
#include <s7/element_registry.hpp>
#include <s7/runtime.hpp>
#include <atomic>
#include <chrono>
#include <functional>
#include <map>
#include <memory>
#include <span>
#include <stop_token>

namespace s7::hardware {
using Deadline = std::chrono::steady_clock::time_point;
enum class Direction { Capture, Playback };
enum class Support { Unknown, Unsupported, Supported };
enum class State { Prepared, Running, Stopped, Faulted };
enum class Status { Ok, Timeout, Cancelled, Overflow, Underflow, ContinuityLost,
                    Disconnected, Busy, Unsupported, InvalidConfiguration, BackendError };
const char* to_string(Status) noexcept;
bool terminal(Status) noexcept;

struct Request {
    std::string backend; // Explicit; there is no silent fallback.
    std::string device{"default"}; // Backend-scoped, not a cross-provider identity.
    Direction direction{Direction::Capture};
    SignalType type{SignalType::F32};
    std::vector<std::string> channels{"0"};
    Rate rate{48000, 1};
    std::size_t block_frames{256};
    std::size_t queue_frames{4096};
    std::map<std::string, std::string> options;
    void validate() const;
    std::size_t frame_bytes() const;
};
struct DeviceInfo {
    std::string backend, selector, label, identity_provenance;
    Support capture{Support::Unknown}, playback{Support::Unknown};
};
struct Descriptor {
    Request actual;
    std::string session_id;
    std::string clock_id; // Empty means no qualified clock identity.
    std::string amplitude;
    std::string native_format; // Empty means unknown; application type is actual.type.
    std::map<std::string,std::string> backend_details; // Readbacks/provenance, never inferred physical identity.
    Support conversion{Support::Unknown};
    Support resampling{Support::Unknown};
    Support timestamps{Support::Unsupported};
    std::size_t startup_prefill_frames{}; // Playback acceptance threshold before activation.
    std::uint64_t segment{1}, configuration_revision{1}, timing_revision{1};
};
struct Timestamp {
    std::int64_t nanoseconds{};
    std::string domain; // E.g. backend device time; never inferred UTC.
    std::string origin;
};
struct Transfer {
    std::size_t frames{}; // Valid simultaneous-frame prefix, including on error.
    Status status{Status::Ok};
    std::uint64_t first_frame{}; // Software-delivered/accepted position, not ADC/DAC position.
    std::optional<Timestamp> time; // Refers to first frame, when backend supplies evidence.
    int native_error{}; // Backend-scoped code when available; zero means unknown/not applicable.
};

// All lifecycle/transfer calls have one owner. cancel() is the only concurrent
// operation. Copy access only in v1; no API lends a native/callback buffer.
class Stream {
public:
    explicit Stream(Descriptor);
    virtual ~Stream() = default;
    Stream(const Stream&) = delete;
    Stream& operator=(const Stream&) = delete;
    const Descriptor& descriptor() const noexcept { return descriptor_; }
    State state() const noexcept { return state_; }
    void start();
    void stop() noexcept; // Abort: queued playback may be discarded; no drain promise.
    virtual void cancel() noexcept { cancelled_.store(true, std::memory_order_release); }
    bool cancelled() const noexcept { return cancelled_.load(std::memory_order_acquire); }
    Transfer read(std::span<std::byte>, std::size_t frames, Deadline, std::stop_token = {});
    Transfer write(std::span<const std::byte>, std::size_t frames, Deadline, std::stop_token = {});
    virtual std::uint64_t emergency_output_frames() const noexcept { return 0; }
protected:
    virtual void do_start() = 0;
    virtual void do_stop() noexcept = 0;
    virtual Transfer do_read(std::span<std::byte>, std::size_t, Deadline, std::stop_token);
    virtual Transfer do_write(std::span<const std::byte>, std::size_t, Deadline, std::stop_token);
private:
    void check_buffer(std::size_t bytes, std::size_t frames) const;
    Transfer finish(Transfer, std::size_t requested);
    const Descriptor descriptor_;
    State state_{State::Prepared};
    std::atomic<bool> cancelled_{};
    std::uint64_t position_{};
    Status fault_{Status::Ok};
    int fault_native_error_{};
};
class Provider {
public:
    virtual ~Provider() = default;
    virtual std::string_view id() const noexcept = 0;
    virtual std::vector<DeviceInfo> devices() = 0; // Explicit discovery may contact drivers.
    virtual std::unique_ptr<Stream> prepare(const Request&) = 0; // No activation/TX.
};

// Aligned caller-owned storage. Allocation happens before transfers, never in
// an audio callback. Byte views do not imply a serialized/wire representation.
class Buffer {
public:
    explicit Buffer(std::size_t bytes);
    std::span<std::byte> bytes() { return {reinterpret_cast<std::byte*>(data_.data()), size_}; }
private:
    std::vector<std::max_align_t> data_;
    std::size_t size_{};
};

class Boundary : public Element {
public:
    Boundary(const Boundary&)=delete;
    Boundary& operator=(const Boundary&)=delete;
    const Request& request() const noexcept { return request_; }
    std::optional<Rate> declared_rate() const override { return request_.rate; }
    void on_step_failure() noexcept override { faulted_ = true; }
    void step() override;
    // A Run owns the surrounding device I/O; the element only moves one frame.
    void stage(std::span<std::byte> block, std::size_t frames);
    void unstage() noexcept;
protected:
    // name has static storage duration, as required by Element::type_name().
    Boundary(std::string_view name, Request);
private:
    Request request_;
    Buffer value_;
    std::span<std::byte> block_;
    std::size_t frames_{}, cursor_{};
    bool faulted_{};
};
class Source : public Boundary {
public:
    explicit Source(Request, std::string_view name = "HardwareSource");
};
class Sink : public Boundary {
public:
    explicit Sink(Request, std::string_view name = "HardwareSink");
};

// A single-channel C32 capture device, with one complete IQ block per graph step.
class CaptureBlockSource : public ArrayElement {
public:
    CaptureBlockSource(std::string_view name, Request, std::uint64_t source_id);
    const Request& request() const noexcept { return request_; }
    std::optional<Rate> declared_rate() const override { return graph_rate_; }
    void step() override;

private:
    friend class Run;
    const Request request_;
    const std::uint64_t source_id_;
    const Rate graph_rate_;
    const ArrayDescriptor shape_;
    ArrayPool pool_;
    ArrayOutputSlot& output_;
    ArrayValue pending_;
};

struct GraphProfile {
    const Request& request;
    Rate graph_rate;
    std::size_t frames_per_step;
    std::string_view boundary_type;
};
bool has_hardware_boundary(const ExecutableGraph&);

struct RunStats {
    std::uint64_t graph_frames{}, transferred_frames{}, playback_frames{}, emergency_output_frames{};
    Status status{Status::Ok};
    int native_error{};
};
// Offline profile validation; does not resolve providers or open devices.
GraphProfile graph_profile(const ExecutableGraph&);
// The scalar hardware boundary of a scalar hardware graph.
Boundary& validate_graph(const ExecutableGraph&);
// A capture-driven run may also feed one lower-rate AudioSink. The audio
// provider buffers complete frames and reports underruns/other device faults.
// The graph uses nominal rates; it does not synchronize physical clocks.
class Run {
public:
    using Resolve = std::function<std::unique_ptr<Provider>(std::string_view)>;
    using StepObserver = std::function<void(std::uint64_t)>;
    // Offline construction. The execution owner prepares and releases streams.
    explicit Run(ExecutableGraph);
    Run(ExecutableGraph, Resolve);
    ~Run();
    void prepare(Resolve);
    // Steps count committed graph evaluations. Transfer counts use hardware frames.
    RunStats execute(std::uint64_t steps, std::chrono::milliseconds transfer_timeout,
                     std::stop_token = {}, const StepObserver& = {});
    const Descriptor& descriptor() const;
    NodeRuntime& runtime() noexcept { return *runtime_; }
    void cancel() noexcept {
        if (stream_) stream_->cancel();
        if (audio_stream_) audio_stream_->cancel();
    }
private:
    RunStats execute_capture(std::uint64_t frames, std::chrono::milliseconds timeout, std::stop_token,
                             const StepObserver&);
    RunStats execute_block_capture(std::uint64_t steps, std::chrono::milliseconds timeout, std::stop_token,
                                   const StepObserver&);
    const Request& request() const noexcept;
    void step_to(RunStats&, std::uint64_t end, const StepObserver&);
    std::unique_ptr<Provider> provider_, audio_provider_;
    std::unique_ptr<Stream> stream_, audio_stream_;
    Element* boundary_{};
    Boundary* audio_sink_{};
    std::unique_ptr<NodeRuntime> runtime_;
    Buffer block_, audio_block_{0};
    std::uint64_t audio_divisor_{};
    bool executed_{};
};

// Shared metadata/factory support for public and external provider libraries.
// Source/sink names must have static storage duration.
void register_elements(ElementRegistry&, std::string_view library, std::string_view source_name,
                       std::string_view sink_name, std::vector<SignalType> types,
                       std::string_view default_backend);
void register_elements(ElementRegistry&, std::string_view library, std::string_view source_name,
                       std::string_view sink_name, std::vector<SignalType> types,
                       std::string_view default_backend, std::string_view category);
Request request_from_context(const ElementCreationContext&, Direction);
Request request_from_context(const ElementCreationContext&, Direction, SignalType);
} // namespace s7::hardware
