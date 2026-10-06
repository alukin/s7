#include "s7/stream_protocol.hpp"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <system_error>

#include <fcntl.h>
#include <unistd.h>

namespace s7::stream {

std::string to_string(const FaultCode code)
{
    switch (code) {
        case FaultCode::None: return "NONE";
        case FaultCode::SequenceGap: return "SEQUENCE_GAP";
        case FaultCode::SendLogOverflow: return "SEND_LOG_OVERFLOW";
        case FaultCode::StreamStateLost: return "STREAM_STATE_LOST";
        case FaultCode::MalformedPacket: return "MALFORMED_PACKET";
        case FaultCode::TypeMismatch: return "TYPE_MISMATCH";
        case FaultCode::Underrun: return "UNDERRUN";
        case FaultCode::TimestampProfileMismatch: return "TIMESTAMP_PROFILE_MISMATCH";
        case FaultCode::UnauthorizedProducer: return "UNAUTHORIZED_PRODUCER";
        case FaultCode::UnauthorizedConsumer: return "UNAUTHORIZED_CONSUMER";
    }
    return "UNKNOWN";
}

std::int64_t element_time_ns(const SampleRate& rate, const std::uint64_t index)
{
    const auto ns = static_cast<unsigned __int128>(index) * 1'000'000'000u * rate.denominator / rate.numerator;
    return ns > static_cast<unsigned __int128>(std::numeric_limits<std::int64_t>::max()) ? std::numeric_limits<std::int64_t>::max()
                                                                                         : static_cast<std::int64_t>(ns);
}

namespace {

bool roles_match(const std::span<const wire::TimestampRecord> records, const std::vector<wire::TimestampRole>& roles)
{
    return std::ranges::equal(records, roles, {}, &wire::TimestampRecord::role);
}

} // namespace

// ---- sequence stores -------------------------------------------------------

std::uint64_t MemorySequenceStore::reserved_end(const wire::StreamId& stream)
{
    const auto it = ends_.find(wire::to_string(stream));
    return it == ends_.end() ? 0 : it->second;
}

void MemorySequenceStore::reserve(const wire::StreamId& stream, const std::uint64_t end)
{
    ends_[wire::to_string(stream)] = end;
}

FileSequenceStore::FileSequenceStore(std::filesystem::path directory)
    : directory_(std::move(directory))
{
    std::filesystem::create_directories(directory_);
}

std::uint64_t FileSequenceStore::reserved_end(const wire::StreamId& stream)
{
    std::ifstream in(directory_ / (wire::to_string(stream) + ".seq"));
    std::uint64_t end = 0;
    if (in && !(in >> end)) {
        throw std::runtime_error("Corrupt sequence reservation for stream " + wire::to_string(stream) + ".");
    }
    return end;
}

void FileSequenceStore::reserve(const wire::StreamId& stream, const std::uint64_t end)
{
    const auto path = directory_ / (wire::to_string(stream) + ".seq");
    const auto temporary = std::filesystem::path(path).concat(".tmp");
    {
        const auto text = std::to_string(end) + "\n";
        const int fd = ::open(temporary.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
        if (fd < 0) {
            throw std::system_error(errno, std::generic_category(), "open " + temporary.string());
        }
        const bool ok = ::write(fd, text.data(), text.size()) == static_cast<ssize_t>(text.size()) && ::fsync(fd) == 0;
        ::close(fd);
        if (!ok) {
            throw std::runtime_error("Could not persist sequence reservation " + temporary.string() + ".");
        }
    }
    std::filesystem::rename(temporary, path);
    const int dir = ::open(directory_.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (dir >= 0) {
        ::fsync(dir);
        ::close(dir);
    }
}

// ---- producer --------------------------------------------------------------

StreamProducer::StreamProducer(StreamConfig config, SequenceStore& sequences)
    : config_(std::move(config))
    , sequences_(sequences)
{
    if (config_.max_elements_per_packet == 0 || config_.send_log_max_packets == 0) {
        throw std::invalid_argument("Stream limits must be positive.");
    }
    if (config_.sample_rate && (config_.sample_rate->numerator == 0 || config_.sample_rate->denominator == 0)) {
        throw std::invalid_argument("A stream's sample rate must be positive.");
    }
    if (!std::ranges::is_sorted(config_.timestamp_roles) ||
        std::ranges::adjacent_find(config_.timestamp_roles) != config_.timestamp_roles.end()) {
        throw std::invalid_argument("A stream's timestamp roles must be distinct and in ascending role order.");
    }
    if (wire::payload_bytes(config_.type, config_.max_elements_per_packet) > wire::kMaxPayloadBytes) {
        throw std::invalid_argument("max_elements_per_packet exceeds the 256 KiB payload limit for " +
                                    std::string(signal_type_name(config_.type)) + ".");
    }
    // Continue beyond the previous reservation: after a restart the consumer
    // sees a gap, never a reused sequence number.
    reserved_end_ = sequences_.reserved_end(config_.stream_id);
    next_sequence_ = reserved_end_;
}

bool StreamProducer::log_full() const
{
    return log_.size() >= config_.send_log_max_packets;
}

std::size_t StreamProducer::append(const void* elements, const std::size_t count, const Clock::time_point now,
                                   const std::span<const wire::TimestampRecord> times)
{
    if (config_.explicit_timestamps()) {
        if (count > 1 || (count == 1 && !roles_match(times, config_.timestamp_roles))) {
            throw std::invalid_argument("Stream " + wire::to_string(config_.stream_id) +
                                        " has explicit timestamps: publish one element at a time with its declared roles.");
        }
    } else if (!times.empty()) {
        throw std::invalid_argument("Stream " + wire::to_string(config_.stream_id) + " takes no per-element timestamps.");
    }
    if (fault_ != FaultCode::None) {
        return count; // a faulted stream sends nothing; the graph is not held back
    }
    if (config_.explicit_timestamps()) {
        // One element per packet: the packet's records are its element's times.
        if (count == 0 || log_full()) {
            return 0;
        }
        const auto element_bytes = signal_type_info(config_.type).storage_bytes;
        const auto* bytes = static_cast<const std::uint8_t*>(elements);
        pending_.assign(bytes, bytes + element_bytes);
        pending_elements_ = 1;
        pending_since_ = now;
        pending_times_.assign(times.begin(), times.end());
        seal();
        return 1;
    }
    const auto element_bytes = signal_type_info(config_.type).storage_bytes;
    const auto* bytes = static_cast<const std::uint8_t*>(elements);
    std::size_t taken = 0;
    while (taken < count) {
        if (pending_elements_ == config_.max_elements_per_packet) {
            if (log_full()) {
                break;
            }
            seal();
        }
        if (pending_elements_ == 0 && log_full()) {
            break;
        }
        const auto chunk = std::min<std::size_t>(count - taken, config_.max_elements_per_packet - pending_elements_);
        if (pending_elements_ == 0) {
            pending_since_ = now;
        }
        pending_.insert(pending_.end(), bytes + taken * element_bytes, bytes + (taken + chunk) * element_bytes);
        pending_elements_ += chunk;
        taken += chunk;
    }
    if (pending_elements_ == config_.max_elements_per_packet && !log_full()) {
        seal();
    }
    return taken;
}

bool StreamProducer::flush(const Clock::time_point now, const bool force)
{
    if (pending_elements_ == 0 || log_full() || fault_ != FaultCode::None) {
        return false;
    }
    if (!force && now - *pending_since_ < config_.max_batch_latency) {
        return false;
    }
    seal();
    return true;
}

std::optional<Clock::time_point> StreamProducer::flush_deadline() const
{
    if (pending_elements_ == 0) {
        return std::nullopt;
    }
    return *pending_since_ + config_.max_batch_latency;
}

void StreamProducer::seal()
{
    if (next_sequence_ >= reserved_end_) {
        reserved_end_ = next_sequence_ + kSequenceReservation;
        sequences_.reserve(config_.stream_id, reserved_end_);
    }
    LoggedPacket packet;
    packet.sequence = next_sequence_++;
    packet.timestamps = std::move(pending_times_);
    pending_times_.clear();
    wire::encode_elements(config_.type, pending_.data(), pending_elements_, packet.payload);
    log_.push_back(std::move(packet));
    ++packets_;
    elements_ += pending_elements_;
    pending_.clear();
    pending_elements_ = 0;
    pending_since_.reset();
}

void StreamProducer::resume(const ConsumerKey& consumer, const std::uint32_t alias, const std::uint64_t next_expected)
{
    const auto oldest = log_.empty() ? next_sequence_ : log_.front().sequence;
    auto start = next_expected;
    if (next_expected < oldest) {
        // Packets the consumer needs are gone (producer restart, or trimmed
        // before it joined). The loss is that consumer's: it continues from
        // the oldest packet still logged, sees the gap and faults; other
        // consumers are unaffected.
        ++lost_resumes_;
        start = oldest;
    } else if (next_expected > next_sequence_) {
        // The consumer is ahead of anything this producer assigned: its
        // sequence state was lost (e.g. a wiped state directory). Sending
        // would look like duplicates to the consumer, so the stream faults.
        ++lost_resumes_;
        fault(FaultCode::StreamStateLost);
        start = next_sequence_;
    }
    auto& entry = consumers_[consumer];
    entry.alias = alias;
    entry.next_to_send = start;
    entry.acknowledged = std::max(entry.acknowledged, start);
    entry.attached = true;
    replayed_ += next_sequence_ - start;
    trim();
}

void StreamProducer::detach(const ConsumerKey& consumer)
{
    if (const auto it = consumers_.find(consumer); it != consumers_.end()) {
        it->second.attached = false; // keeps its retention position
    }
}

void StreamProducer::acknowledge(const ConsumerKey& consumer, const std::uint64_t next_expected)
{
    const auto it = consumers_.find(consumer);
    if (it == consumers_.end() || next_expected > it->second.next_to_send) {
        return; // cannot acknowledge what was not sent
    }
    it->second.acknowledged = std::max(it->second.acknowledged, next_expected);
    trim();
}

void StreamProducer::trim()
{
    if (consumers_.empty()) {
        return; // nobody has the data yet
    }
    auto lowest = std::numeric_limits<std::uint64_t>::max();
    for (const auto& [_, consumer] : consumers_) {
        lowest = std::min(lowest, consumer.acknowledged);
    }
    while (!log_.empty() && log_.front().sequence < lowest) {
        log_.pop_front();
    }
}

std::optional<std::vector<std::uint8_t>> StreamProducer::next_message(const ConsumerKey& consumer)
{
    const auto it = consumers_.find(consumer);
    if (fault_ != FaultCode::None || it == consumers_.end() || !it->second.attached ||
        it->second.next_to_send >= next_sequence_ || log_.empty()) {
        return std::nullopt;
    }
    const auto index = it->second.next_to_send - log_.front().sequence;
    if (index >= log_.size()) {
        return std::nullopt;
    }
    const auto& logged = log_[index];
    wire::DataPacket packet;
    packet.type = config_.type;
    packet.stream_alias = it->second.alias;
    packet.sequence = logged.sequence;
    packet.timestamps = logged.timestamps;
    packet.payload = logged.payload;
    ++it->second.next_to_send;
    return wire::encode_data_packet(packet);
}

void StreamProducer::fault(const FaultCode code)
{
    if (fault_ == FaultCode::None) {
        fault_ = code;
    }
}

ProducerStats StreamProducer::stats() const
{
    return ProducerStats{packets_, elements_, replayed_, lost_resumes_, log_.size(), blocked_total_};
}

// ---- consumer --------------------------------------------------------------

StreamConsumer::StreamConsumer(StreamConfig config, const std::uint32_t alias)
    : config_(std::move(config))
    , alias_(alias)
{
}

wire::ControlMessage StreamConsumer::resume_message() const
{
    return wire::ControlMessage{wire::ControlKind::Resume, alias_, next_expected_, config_.stream_id};
}

void StreamConsumer::on_packet(const wire::DataPacket& packet, const Clock::time_point now)
{
    if (fault_ != FaultCode::None) {
        return;
    }
    if (packet.type != config_.type) {
        fault(FaultCode::TypeMismatch);
        return;
    }
    // Exactly the declared roles on every packet of an explicitly timestamped
    // stream, none on any other stream.
    if (config_.explicit_timestamps() ? !roles_match(packet.timestamps, config_.timestamp_roles) : !packet.timestamps.empty()) {
        fault(FaultCode::TimestampProfileMismatch);
        return;
    }
    if (restart_at_next_) {
        restart_at_next_ = false;
        next_expected_ = packet.sequence;
        consumed_up_to_ = std::max(consumed_up_to_, packet.sequence);
        acked_up_to_ = std::max(acked_up_to_, packet.sequence);
    }
    if (packet.sequence < next_expected_) {
        ++duplicates_;
        return;
    }
    if (packet.sequence > next_expected_) {
        fault(FaultCode::SequenceGap);
        return;
    }
    BufferedPacket buffered;
    buffered.sequence = packet.sequence;
    buffered.timestamps = packet.timestamps;
    buffered.elements = wire::payload_element_count(config_.type, packet.payload.size());
    buffered.values.resize(buffered.elements * signal_type_info(config_.type).storage_bytes);
    wire::decode_elements(config_.type, packet.payload, buffered.values.data());
    buffered_elements_ += buffered.elements;
    buffer_.push_back(std::move(buffered));
    ++next_expected_;
    ++packets_;
    if (last_ack_ == Clock::time_point{}) {
        last_ack_ = now;
    }
}

void StreamConsumer::on_malformed()
{
    ++malformed_;
    fault(FaultCode::MalformedPacket);
}

std::size_t StreamConsumer::take(void* out, const std::size_t max_count, std::vector<wire::TimestampRecord>* first_times)
{
    if (first_times) {
        first_times->clear();
        if (max_count > 0 && !buffer_.empty()) {
            const auto& front = buffer_.front();
            if (config_.sample_rate) {
                if (element_count_known_) {
                    const auto ns = element_time_ns(*config_.sample_rate, elements_);
                    for (const auto role : config_.timestamp_roles) {
                        first_times->push_back(wire::TimestampRecord{role, ns});
                    }
                }
            } else if (front.consumed == 0) {
                *first_times = front.timestamps;
            }
        }
    }
    const auto element_bytes = signal_type_info(config_.type).storage_bytes;
    auto* bytes = static_cast<std::uint8_t*>(out);
    std::size_t taken = 0;
    while (taken < max_count && !buffer_.empty()) {
        auto& front = buffer_.front();
        const auto chunk = std::min(max_count - taken, front.elements - front.consumed);
        std::memcpy(bytes + taken * element_bytes, front.values.data() + front.consumed * element_bytes, chunk * element_bytes);
        front.consumed += chunk;
        taken += chunk;
        buffered_elements_ -= chunk;
        elements_ += chunk;
        if (front.consumed == front.elements) {
            consumed_up_to_ = front.sequence + 1;
            ++consumed_since_ack_;
            buffer_.pop_front();
        }
    }
    return taken;
}

std::optional<wire::ControlMessage> StreamConsumer::ack_due(const Clock::time_point now)
{
    if (consumed_up_to_ <= acked_up_to_) {
        return std::nullopt;
    }
    if (consumed_since_ack_ < kAckEveryPackets && now - last_ack_ < kAckInterval) {
        return std::nullopt;
    }
    acked_up_to_ = consumed_up_to_;
    consumed_since_ack_ = 0;
    last_ack_ = now;
    return wire::ControlMessage{wire::ControlKind::Ack, alias_, acked_up_to_, {}};
}

void StreamConsumer::fault(const FaultCode code)
{
    if (fault_ == FaultCode::None) {
        fault_ = code;
    }
}

void StreamConsumer::recover()
{
    fault_ = FaultCode::None;
    restart_at_next_ = true;
    element_count_known_ = false; // the elements lost in the fault are not counted
}

ConsumerStats StreamConsumer::stats() const
{
    return ConsumerStats{packets_, elements_, duplicates_, malformed_, next_expected_, buffered_elements_};
}

} // namespace s7::stream
