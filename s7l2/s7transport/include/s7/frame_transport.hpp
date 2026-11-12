// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: LGPL-3.0-or-later
#ifndef S7TRANSPORT_INCLUDE_S7_FRAME_TRANSPORT_HPP
#define S7TRANSPORT_INCLUDE_S7_FRAME_TRANSPORT_HPP

#include <s7/stream_protocol.hpp>
#include <array>
#include <atomic>
#include <memory>
#include <stop_token>

// Opt-in, socket-independent frame service. Not yet advertised by RuntimeService.
// Every frame contains one value per channel; payload leases hold portable wire
// bytes, never host objects. All codec units are independently decodable.
namespace s7::frames {
inline constexpr std::uint16_t kFrameExtension = 0x8002;
inline constexpr std::uint16_t kEncodingExtension = 0x8003;
inline constexpr std::size_t kMaxEvents = 32;
inline constexpr std::size_t kMaxMetadataBytes = 2048;
inline constexpr std::size_t kMaxMessageBytes = wire::kMaxPayloadBytes + 4096;
enum class Encoding : std::uint16_t { Raw = 0, Lz4 = 1, Zstd = 2 };
[[nodiscard]] bool encoding_available(Encoding encoding);

enum class Status { Ok, Backpressure, Cancelled, Closed, SourceOverflow,
                    ContinuityLost, SinkUnderflow, Malformed, SequenceGap, StateLost };
[[nodiscard]] std::string to_string(Status status);
enum class SourceState { Active, DrainingFault, Faulted, Closed };

// Shared node-wide admission budget for preallocated buffer/workspace capacity.
// It is not process RSS or an allocator-overhead measurement.
class BufferBudget {
public:
    explicit BufferBudget(std::size_t bytes) : limit_(bytes) {}
    void reserve(std::size_t bytes);
    void release(std::size_t bytes) noexcept;
    [[nodiscard]] std::size_t reserved_bytes() const { return used_.load(); }
private:
    std::size_t limit_;
    std::atomic<std::size_t> used_{};
};

// Preallocated fixed-size slots. Shared leases are immutable; retained slices
// keep the entire allocation charged. Destruction/close wakes blocked allocators.
class BufferPool {
    struct State;
public:
    class Lease {
    public:
        Lease() = default;
        Lease(const Lease&) = default;
        Lease(Lease&&) noexcept = default;
        Lease& operator=(Lease other) noexcept;
        ~Lease();
        [[nodiscard]] std::span<const std::uint8_t> bytes() const;
        [[nodiscard]] std::size_t capacity_bytes() const;
        [[nodiscard]] Lease slice(std::size_t offset, std::size_t size) const;
        [[nodiscard]] explicit operator bool() const { return bool(slot_); }
    private:
        friend class BufferPool;
        struct Slot;
        std::shared_ptr<State> state_;
        std::shared_ptr<Slot> slot_;
        std::size_t offset_{}, size_{};
    };
    BufferPool(std::size_t slots, std::size_t slot_bytes, std::shared_ptr<BufferBudget> budget = {});
    ~BufferPool();
    BufferPool(const BufferPool&) = delete;
    BufferPool& operator=(const BufferPool&) = delete;
    [[nodiscard]] Lease copy(std::span<const std::uint8_t> bytes);
    [[nodiscard]] Lease wait_copy(std::span<const std::uint8_t> bytes,
                                 stream::Clock::time_point deadline, std::stop_token stop = {});
    void close();
    [[nodiscard]] std::size_t capacity_bytes() const;
    [[nodiscard]] std::size_t slot_count() const;
    [[nodiscard]] std::size_t slot_bytes() const;
    [[nodiscard]] std::size_t leased_slots() const;
    [[nodiscard]] std::shared_ptr<BufferBudget> budget() const;
private:
    std::shared_ptr<State> state_;
};

struct Descriptor {
    wire::StreamId stream_id{wire::random_stream_id()};
    SignalType type{SignalType::F32};
    std::vector<std::string> channels{"mono"};
    stream::SampleRate frame_rate{48000, 1};
    std::string clock_domain{"synthetic"};
    std::string clock_origin{"model run origin; nanoseconds"};
    wire::TimestampRole time_role{wire::TimestampRole::ModelTime};
    std::string time_provenance{"synthetic"};
    std::string position_provenance{"software"};
    std::uint32_t max_frames_per_packet{480};
    std::uint32_t window_packets{32};
    std::uint64_t window_frames{480 * 32};
    std::uint64_t configuration_revision{}, timing_revision{};
    Encoding encoding{Encoding::Raw};
    friend bool operator==(const Descriptor&, const Descriptor&) = default;
    void validate() const;
    [[nodiscard]] std::size_t frame_bytes() const;
};

struct Event {
    std::uint32_t offset{};
    std::uint16_t type{};
    std::array<std::uint8_t, 16> value{};
    friend bool operator==(const Event&, const Event&) = default;
};
[[nodiscard]] Event clock_anchor(std::uint32_t offset, std::int64_t time_ns,
                                 std::uint64_t uncertainty_ns = UINT64_MAX);
[[nodiscard]] Event source_fault(Status cause, std::optional<std::uint64_t> lost = {});

struct Info {
    bool segment_start{}, fault_only{};
    std::uint32_t frames{};
    wire::StreamId segment{};
    std::uint64_t first_frame{}, configuration_revision{}, timing_revision{};
    std::array<Event, kMaxEvents> events{};
    std::uint32_t event_count{};
};
struct Packet {
    std::uint32_t alias{};
    std::uint64_t sequence{};
    Info info;
    BufferPool::Lease payload;
};

// Per-instance codec workspace is fixed at construction. Methods are serialized
// by their owning source/sink; do not call one codec concurrently.
class Codec {
public:
    explicit Codec(Descriptor descriptor, std::shared_ptr<BufferBudget> budget = {});
    ~Codec();
    Codec(const Codec&) = delete;
    Codec& operator=(const Codec&) = delete;
    [[nodiscard]] std::span<const std::uint8_t> encode(std::uint32_t alias, std::uint64_t sequence,
        const Info& info, std::span<const std::uint8_t> payload);
    [[nodiscard]] Packet decode(std::span<const std::uint8_t> message, BufferPool& pool,
                                std::uint64_t available_frames = UINT64_MAX);
    [[nodiscard]] std::size_t workspace_bytes() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

struct PublishResult { std::size_t frames{}; Status status{Status::Ok}; };
struct ReceiveResult {
    std::size_t frames{}; Status status{Status::Ok}; Info info;
    std::optional<std::uint64_t> lost_frames;
};
struct FaultRecord {
    Status status{Status::Ok};
    std::uint64_t sequence{}, first_frame{};
    wire::StreamId segment{};
    std::optional<std::uint64_t> lost_frames;
};

// Caller supplies dedicated pools, each reserved for at least window_packets
// slots. Transport alias is fixed for this service instance, including fan-out.
// SequenceStore access shared across producers must be serialized by the owner.
class FrameSink {
public:
    FrameSink(Descriptor descriptor, stream::SequenceStore& sequences,
              std::shared_ptr<BufferPool> send_pool, std::uint32_t alias = 1);
    ~FrameSink();
    [[nodiscard]] PublishResult publish_copy(const void* interleaved_values, std::size_t frames,
                                             std::span<const Event> events = {}, std::stop_token stop = {});
    // Exactly one already encoded, immutable numeric block (not codec bytes).
    [[nodiscard]] PublishResult publish_shared(const BufferPool::Lease& payload, std::uint32_t frames,
                                               std::span<const Event> events = {}, std::stop_token stop = {});
    void resume(const stream::ConsumerKey& peer, std::uint64_t next_expected);
    void detach(const stream::ConsumerKey& peer);
    void acknowledge(const stream::ConsumerKey& peer, std::uint64_t consumed_up_to);
    [[nodiscard]] BufferPool::Lease next_message(const stream::ConsumerKey& peer);
    void fault(Status cause, std::optional<std::uint64_t> lost = {});
    // Called by an already authorized owner, never by an unauthenticated packet.
    // Recovery requires the terminal marker to have been consumed by all peers.
    void recover(wire::StreamId new_segment);
    void close();
    [[nodiscard]] SourceState state() const;
    [[nodiscard]] std::size_t log_depth() const;
    [[nodiscard]] std::uint64_t next_sequence() const;
    [[nodiscard]] std::vector<FaultRecord> recent_faults() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

class FrameSource {
public:
    FrameSource(Descriptor descriptor, std::shared_ptr<BufferPool> receive_pool, std::uint32_t alias = 1);
    ~FrameSource();
    // Peer authorization belongs to the connection owner before calling this.
    bool on_message(std::span<const std::uint8_t> message);
    [[nodiscard]] Packet front_packet() const;
    [[nodiscard]] BufferPool::Lease front() const;
    // Explicit consumption, including for retained leases. Never ACK on receipt.
    [[nodiscard]] ReceiveResult consume(std::size_t frames);
    [[nodiscard]] ReceiveResult receive_copy(void* interleaved_values, std::size_t max_frames);
    [[nodiscard]] std::uint64_t next_expected() const;
    [[nodiscard]] std::uint64_t consumed_up_to() const;
    [[nodiscard]] Status status() const;
    [[nodiscard]] std::vector<FaultRecord> recent_faults() const;
    void sink_underflow(std::optional<std::uint64_t> lost = {});
    // A reviewed recovery boundary supplied by the authorized owner.
    void recover(std::uint64_t next_sequence, wire::StreamId new_segment);
    void close();
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace s7::frames
#endif
