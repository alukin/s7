#ifndef S7TRANSPORT_INCLUDE_S7_STREAM_PROTOCOL_HPP
#define S7TRANSPORT_INCLUDE_S7_STREAM_PROTOCOL_HPP

// Lossless stream delivery (S7_Network_Wire_Protocol_v1.md, section 12.1,
// item 2), independent of any socket: the transport feeds these classes
// messages and time, and sends what they return.
//
// Producer: packs elements into packets (batching), numbers them, keeps them
// in a bounded send log until every attached consumer acknowledged them, and
// replays from a consumer's RESUME position. A full log is backpressure.
// Consumer: delivers each sequence exactly once and in order, discards
// duplicates, faults on a gap, and acknowledges what its graph consumed.

#include <chrono>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "s7/wire_format.hpp"

namespace s7::stream {

using Clock = std::chrono::steady_clock;

enum class FaultCode : std::uint8_t {
    None,
    SequenceGap,       // packets are missing (e.g. producer restart)
    SendLogOverflow,   // backpressure lasted longer than max_block_ms
    StreamStateLost,   // a consumer cannot be resumed from the send log
    MalformedPacket,
    TypeMismatch,      // packet type differs from the stream's type
    Underrun,          // the consumer waited longer than max_wait_ms for data
};

[[nodiscard]] std::string to_string(FaultCode code);

struct StreamConfig {
    wire::StreamId stream_id{};
    SignalType type{SignalType::F64};
    std::uint32_t max_elements_per_packet{1024};
    std::chrono::milliseconds max_batch_latency{10};
    std::uint32_t send_log_max_packets{4096};
    std::chrono::milliseconds max_block{2000};
    std::chrono::milliseconds max_wait{2000}; // consumer side: how long a step may wait for data
};

// Durable sequence-range reservations (wire draft section 4): a producer never
// reuses a sequence number, even after a crash.
class SequenceStore {
public:
    virtual ~SequenceStore() = default;
    // End of the last reservation for the stream (0 if none).
    [[nodiscard]] virtual std::uint64_t reserved_end(const wire::StreamId& stream) = 0;
    // Durably records a new reservation end; returns only once it is safe.
    virtual void reserve(const wire::StreamId& stream, std::uint64_t end) = 0;
};

class MemorySequenceStore final : public SequenceStore {
public:
    std::uint64_t reserved_end(const wire::StreamId& stream) override;
    void reserve(const wire::StreamId& stream, std::uint64_t end) override;

private:
    std::unordered_map<std::string, std::uint64_t> ends_;
};

// <directory>/<stream_id>.seq, written to a temporary file, fsync'ed and renamed.
class FileSequenceStore final : public SequenceStore {
public:
    explicit FileSequenceStore(std::filesystem::path directory);
    std::uint64_t reserved_end(const wire::StreamId& stream) override;
    void reserve(const wire::StreamId& stream, std::uint64_t end) override;

private:
    std::filesystem::path directory_;
};

inline constexpr std::uint64_t kSequenceReservation = 65536;
inline constexpr std::uint32_t kAckEveryPackets = 64;
inline constexpr std::chrono::milliseconds kAckInterval{50};

// A consumer's stable identity, chosen by the transport (the peer certificate
// over mTLS; one anonymous key over plain WebSocket). A disconnected consumer
// keeps its place: packets it has not acknowledged stay in the send log until
// it resumes.
using ConsumerKey = std::string;

struct ProducerStats {
    std::uint64_t packets{};
    std::uint64_t elements{};
    std::uint64_t replayed{};
    std::uint64_t lost_resumes{}; // RESUMEs from a position no longer in the log
    std::size_t send_log_depth{};
    std::chrono::milliseconds blocked_total{};
};

class StreamProducer {
public:
    StreamProducer(StreamConfig config, SequenceStore& sequences);

    [[nodiscard]] const StreamConfig& config() const { return config_; }

    // Appends elements (host storage of config().type). Sealed packets go to
    // the send log. Returns how many elements were taken: fewer than `count`
    // when the send log is full (backpressure); the caller waits for acks and
    // retries, and calls fault(SendLogOverflow) after max_block.
    std::size_t append(const void* elements, std::size_t count, Clock::time_point now);
    // Seals the pending partial packet if its oldest element waited
    // max_batch_latency (or always with force). Returns whether it sealed one.
    bool flush(Clock::time_point now, bool force = false);
    [[nodiscard]] std::optional<Clock::time_point> flush_deadline() const;

    // RESUME from a consumer: start transmitting at next_expected. A position
    // older than the log starts at the oldest logged packet (the consumer
    // faults on the gap); a position beyond next_sequence() faults the stream
    // (StreamStateLost).
    void resume(const ConsumerKey& consumer, std::uint32_t alias, std::uint64_t next_expected);
    void detach(const ConsumerKey& consumer);
    void acknowledge(const ConsumerKey& consumer, std::uint64_t next_expected);

    // The next encoded packet for the consumer, or nothing if it is up to date.
    [[nodiscard]] std::optional<std::vector<std::uint8_t>> next_message(const ConsumerKey& consumer);

    void fault(FaultCode code);
    // RECOVER_STREAM: clears the fault; sending resumes.
    void recover() { fault_ = FaultCode::None; }
    [[nodiscard]] FaultCode fault_code() const { return fault_; }
    [[nodiscard]] bool log_full() const;
    [[nodiscard]] std::uint64_t next_sequence() const { return next_sequence_; }
    [[nodiscard]] ProducerStats stats() const;
    void add_blocked_time(std::chrono::milliseconds blocked) { blocked_total_ += blocked; }

private:
    struct LoggedPacket {
        std::uint64_t sequence{};
        std::vector<std::uint8_t> payload;
    };
    struct Consumer {
        std::uint32_t alias{};
        std::uint64_t next_to_send{};
        std::uint64_t acknowledged{};
        bool attached{};
    };

    void seal();
    void trim();

    StreamConfig config_;
    SequenceStore& sequences_;
    std::uint64_t next_sequence_{};
    std::uint64_t reserved_end_{};
    std::deque<LoggedPacket> log_; // contiguous sequences
    std::unordered_map<ConsumerKey, Consumer> consumers_;
    std::vector<std::uint8_t> pending_; // host storage of the open packet's elements
    std::size_t pending_elements_{};
    std::optional<Clock::time_point> pending_since_;
    FaultCode fault_{FaultCode::None};
    std::uint64_t packets_{};
    std::uint64_t elements_{};
    std::uint64_t replayed_{};
    std::uint64_t lost_resumes_{};
    std::chrono::milliseconds blocked_total_{};
};

struct ConsumerStats {
    std::uint64_t packets{};
    std::uint64_t duplicates_discarded{};
    std::uint64_t malformed{};
    std::uint64_t next_expected{};
    std::size_t buffered_elements{};
};

class StreamConsumer {
public:
    StreamConsumer(StreamConfig config, std::uint32_t alias);

    [[nodiscard]] const StreamConfig& config() const { return config_; }
    [[nodiscard]] std::uint32_t alias() const { return alias_; }

    // Sent after every (re)connect.
    [[nodiscard]] wire::ControlMessage resume_message() const;

    // A packet for this stream: delivered in order, duplicate discarded, or a
    // gap / type mismatch, which faults the stream.
    void on_packet(const wire::DataPacket& packet, Clock::time_point now);
    void on_malformed();

    // Elements available to the graph, and taking them (host storage).
    [[nodiscard]] std::size_t available() const { return buffered_elements_; }
    std::size_t take(void* out, std::size_t max_count);

    // An ACK when 64 packets were consumed or 50 ms passed since the last one
    // while something new was consumed.
    [[nodiscard]] std::optional<wire::ControlMessage> ack_due(Clock::time_point now);

    void fault(FaultCode code);
    // RECOVER_STREAM: clears the fault and accepts the next packet received as
    // the new start (the data lost in the fault stays lost and reported).
    void recover();
    [[nodiscard]] FaultCode fault_code() const { return fault_; }
    [[nodiscard]] ConsumerStats stats() const;

private:
    struct BufferedPacket {
        std::uint64_t sequence{};
        std::vector<std::uint8_t> values; // host storage of the decoded elements
        std::size_t elements{};
        std::size_t consumed{};
    };

    StreamConfig config_;
    std::uint32_t alias_;
    std::uint64_t next_expected_{};  // next sequence to accept
    std::uint64_t consumed_up_to_{}; // every packet before this was fully taken by the graph
    std::uint64_t acked_up_to_{};
    std::deque<BufferedPacket> buffer_;
    std::size_t buffered_elements_{};
    std::uint32_t consumed_since_ack_{};
    Clock::time_point last_ack_{};
    FaultCode fault_{FaultCode::None};
    bool restart_at_next_{false};
    std::uint64_t packets_{};
    std::uint64_t duplicates_{};
    std::uint64_t malformed_{};
};

} // namespace s7::stream

#endif // S7TRANSPORT_INCLUDE_S7_STREAM_PROTOCOL_HPP
