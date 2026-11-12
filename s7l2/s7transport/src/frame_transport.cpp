// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: LGPL-3.0-or-later
#include <s7/frame_transport.hpp>
#include <algorithm>
#include <condition_variable>
#include <cstring>
#include <limits>
#include <mutex>
#include <set>
#include <zlib.h>
#ifdef S7_FRAMES_LZ4
#include <lz4.h>
#endif
#ifdef S7_FRAMES_ZSTD
#define ZSTD_STATIC_LINKING_ONLY
#include <zstd.h>
#endif

namespace s7::frames {
std::string to_string(Status status) {
    switch (status) {
    case Status::Ok: return "OK";
    case Status::Backpressure: return "BACKPRESSURE";
    case Status::Cancelled: return "CANCELLED";
    case Status::Closed: return "CLOSED";
    case Status::SourceOverflow: return "SOURCE_OVERFLOW";
    case Status::ContinuityLost: return "CONTINUITY_LOST";
    case Status::SinkUnderflow: return "SINK_UNDERFLOW";
    case Status::Malformed: return "MALFORMED";
    case Status::SequenceGap: return "SEQUENCE_GAP";
    case Status::StateLost: return "STATE_LOST";
    }
    return "UNKNOWN";
}

namespace {
using Bytes = std::span<const std::uint8_t>;
struct Reservation {
    std::shared_ptr<BufferBudget> budget;
    std::size_t bytes{};
    void reserve(std::shared_ptr<BufferBudget> b, std::size_t n) {
        if (b) b->reserve(n);
        budget = std::move(b); bytes = n;
    }
    ~Reservation() { if (budget) budget->release(bytes); }
};
template<class T> class Ring {
    std::vector<std::optional<T>> slots_;
    std::size_t first_{}, size_{};
public:
    explicit Ring(std::size_t capacity) : slots_(capacity) {}
    bool empty() const { return size_ == 0; }
    std::size_t size() const { return size_; }
    std::size_t storage_bytes() const { return slots_.capacity() * sizeof(std::optional<T>); }
    T& operator[](std::size_t index) { return *slots_[(first_ + index) % slots_.size()]; }
    T& front() { return (*this)[0]; }
    void push_back(T value) {
        if (size_ == slots_.size()) throw std::length_error("frame ring full");
        slots_[(first_ + size_) % slots_.size()].emplace(std::move(value)); ++size_;
    }
    void pop_front() { slots_[first_].reset(); first_ = (first_ + 1) % slots_.size(); --size_; }
    void clear() { while (!empty()) pop_front(); }
};
struct History {
    std::array<FaultRecord, 16> records{};
    std::size_t next{}, count{};
    void add(FaultRecord fault) { records[next] = fault; next = (next + 1) % records.size(); count = std::min(count + 1, records.size()); }
    std::vector<FaultRecord> snapshot() const {
        std::vector<FaultRecord> result; result.reserve(count);
        for (std::size_t i = 0; i < count; ++i) result.push_back(records[(next + records.size() - count + i) % records.size()]);
        return result;
    }
};
void require(bool value, const char* error) { if (!value) throw wire::FormatError(error); }
template<class T> T get(Bytes b, std::size_t offset) {
    require(offset <= b.size() && sizeof(T) <= b.size() - offset, "truncated frame field");
    T n{};
    for (std::size_t i = 0; i < sizeof(T); ++i) n |= T(b[offset + i]) << (i * 8);
    return n;
}
template<class T> void put(std::vector<std::uint8_t>& b, T value) {
    for (std::size_t i = 0; i < sizeof(T); ++i) b.push_back(static_cast<std::uint8_t>(value >> (i * 8)));
}
template<class T> void store(std::span<std::uint8_t> b, std::size_t offset, T value) {
    for (std::size_t i = 0; i < sizeof(T); ++i) b[offset + i] = static_cast<std::uint8_t>(value >> (i * 8));
}
bool nonzero(const wire::StreamId& id) { return std::ranges::any_of(id, [](auto v) { return v != 0; }); }
std::uint32_t checksum(Bytes b) { return static_cast<std::uint32_t>(crc32(0, b.data(), static_cast<uInt>(b.size()))); }
Status fault_cause(const Event& e) {
    const auto code = get<std::uint16_t>(e.value, 0);
    require(code == 1 || code == 3, "unsupported source fault");
    require(get<std::uint16_t>(e.value, 2) <= 1 && get<std::uint32_t>(e.value, 4) == 0, "invalid fault flags/reserved");
    require(get<std::uint16_t>(e.value, 2) != 0 || get<std::uint64_t>(e.value, 8) == 0, "unknown loss count is not zero");
    return code == 1 ? Status::SourceOverflow : Status::ContinuityLost;
}
void validate_info(const Descriptor& d, const Info& info, std::size_t bytes) {
    require(nonzero(info.segment), "zero segment identity");
    require(info.configuration_revision == d.configuration_revision && info.timing_revision == d.timing_revision,
            "frame revision mismatch");
    require(info.event_count <= kMaxEvents, "too many frame events");
    require(info.frames <= d.max_frames_per_packet && info.frames <= UINT64_MAX - info.first_frame,
            "frame count/index overflow");
    require(!info.segment_start || info.first_frame == 0, "segment does not start at zero");
    require(bytes == std::size_t(info.frames) * d.frame_bytes(), "frame payload length mismatch");
    if (info.fault_only) {
        require(info.frames == 0 && info.event_count == 1, "invalid terminal frame");
    } else require(info.frames > 0, "empty data frame packet");
    for (std::size_t i = 0; i < info.event_count; ++i) {
        const auto& e = info.events[i];
        require(e.offset < info.frames || (info.fault_only && e.offset == 0), "event outside packet");
        if (i) {
            const auto& prev = info.events[i - 1];
            require(e.offset > prev.offset || (e.offset == prev.offset && e.type > prev.type), "duplicate/unsorted events");
        }
        if (info.fault_only) { require(e.type == 0x8001, "missing terminal fault"); static_cast<void>(fault_cause(e)); }
        else require(e.type == 0x8002, "unsupported data event");
    }
}
void check_pool(const Descriptor& d, const std::shared_ptr<BufferPool>& pool, bool send) {
    require(pool && pool->slot_count() >= d.window_packets &&
            pool->slot_bytes() >= (send ? kMaxMessageBytes : wire::kMaxPayloadBytes), "insufficient reserved frame pool");
}
} // namespace

void BufferBudget::reserve(std::size_t bytes) {
    auto used = used_.load();
    do { if (bytes > limit_ - used) throw std::length_error("node frame buffer budget exhausted"); }
    while (!used_.compare_exchange_weak(used, used + bytes));
}
void BufferBudget::release(std::size_t bytes) noexcept { used_.fetch_sub(bytes); }

struct BufferPool::Lease::Slot {
    explicit Slot(std::size_t size) : data(std::make_unique<std::uint8_t[]>(size)) {}
    std::unique_ptr<std::uint8_t[]> data;
};
struct BufferPool::State {
    Reservation reserved;
    std::mutex mutex;
    std::condition_variable_any changed;
    std::vector<std::shared_ptr<Lease::Slot>> slots;
    std::size_t slot_bytes{};
    bool closed{};
};
BufferPool::Lease& BufferPool::Lease::operator=(Lease other) noexcept {
    std::swap(state_, other.state_); std::swap(slot_, other.slot_);
    std::swap(offset_, other.offset_); std::swap(size_, other.size_); return *this;
}
BufferPool::Lease::~Lease() {
    // Synchronize predicate changes with waiters to avoid missed notifications.
    if (state_) { std::lock_guard lock(state_->mutex); slot_.reset(); state_->changed.notify_all(); }
}
Bytes BufferPool::Lease::bytes() const { return slot_ ? Bytes(slot_->data.get() + offset_, size_) : Bytes{}; }
std::size_t BufferPool::Lease::capacity_bytes() const { return state_ ? state_->slot_bytes : 0; }
BufferPool::Lease BufferPool::Lease::slice(std::size_t offset, std::size_t size) const {
    if (offset > size_ || size > size_ - offset) throw std::out_of_range("lease slice");
    Lease result(*this); result.offset_ += offset; result.size_ = size; return result;
}
BufferPool::BufferPool(std::size_t slots, std::size_t bytes, std::shared_ptr<BufferBudget> budget) : state_(std::make_shared<State>()) {
    if (!slots || !bytes || slots > SIZE_MAX / bytes) throw std::invalid_argument("invalid pool capacity");
    state_->slot_bytes = bytes;
    state_->reserved.reserve(std::move(budget), slots * bytes);
    state_->slots.reserve(slots);
    for (std::size_t i = 0; i < slots; ++i) state_->slots.push_back(std::make_shared<Lease::Slot>(bytes));
}
BufferPool::~BufferPool() { close(); }
BufferPool::Lease BufferPool::copy(Bytes bytes) { return wait_copy(bytes, stream::Clock::now()); }
BufferPool::Lease BufferPool::wait_copy(Bytes bytes, stream::Clock::time_point deadline, std::stop_token stop) {
    if (bytes.size() > state_->slot_bytes) throw std::length_error("block exceeds pool slot");
    std::unique_lock lock(state_->mutex);
    const auto available = [&] { return std::ranges::find_if(state_->slots, [](const auto& s) { return s.use_count() == 1; }); };
    state_->changed.wait_until(lock, stop, deadline, [&] { return state_->closed || available() != state_->slots.end(); });
    if (state_->closed || stop.stop_requested()) return {};
    const auto it = available();
    if (it == state_->slots.end()) return {};
    Lease result; result.state_ = state_; result.slot_ = *it; result.size_ = bytes.size();
    std::copy(bytes.begin(), bytes.end(), result.slot_->data.get());
    return result;
}
void BufferPool::close() { std::lock_guard lock(state_->mutex); state_->closed = true; state_->changed.notify_all(); }
std::size_t BufferPool::capacity_bytes() const { return slot_count() * slot_bytes(); }
std::size_t BufferPool::slot_count() const { return state_->slots.size(); }
std::size_t BufferPool::slot_bytes() const { return state_->slot_bytes; }
std::shared_ptr<BufferBudget> BufferPool::budget() const { return state_->reserved.budget; }
std::size_t BufferPool::leased_slots() const {
    std::lock_guard lock(state_->mutex);
    return std::ranges::count_if(state_->slots, [](const auto& s) { return s.use_count() > 1; });
}

bool encoding_available(Encoding encoding) {
    switch (encoding) {
    case Encoding::Raw: return true;
#ifdef S7_FRAMES_LZ4
    case Encoding::Lz4: return true;
#endif
#ifdef S7_FRAMES_ZSTD
    case Encoding::Zstd: return true;
#endif
    default: return false;
    }
}
std::size_t Descriptor::frame_bytes() const {
    require(signal_type_from_wire_id(static_cast<std::uint16_t>(type)).has_value() && !channels.empty() && channels.size() <= 64,
            "invalid frame type/channel count");
    return wire::payload_bytes(type, 1) * channels.size();
}
void Descriptor::validate() const {
    require(signal_type_from_wire_id(static_cast<std::uint16_t>(type)).has_value(), "unknown frame sample type");
    require(type != SignalType::I12 && type != SignalType::U12, "bit-level scalar frames are unsupported");
    require(nonzero(stream_id) && channels.size() >= 1 && channels.size() <= 64, "invalid frame identity/channel count");
    std::set<std::string> unique;
    for (const auto& c : channels) require(!c.empty() && c.size() <= 128 && unique.insert(c).second, "invalid/duplicate channel identity");
    require(frame_rate.numerator > 0 && frame_rate.denominator > 0 && !clock_domain.empty() && clock_domain.size() <= 128,
            "invalid frame rate/clock domain");
    require(!clock_origin.empty() && clock_origin.size() <= 256, "missing/oversized clock origin definition");
    require(time_role == wire::TimestampRole::ModelTime || time_role == wire::TimestampRole::SourceTime, "invalid frame time role");
    require(time_provenance == "unknown" || time_provenance == "device" || time_provenance == "synthetic", "invalid time provenance");
    require(position_provenance == "software" || position_provenance == "hardware", "invalid position provenance");
    require(max_frames_per_packet && max_frames_per_packet <= wire::kMaxPayloadBytes / frame_bytes(), "frame payload exceeds bound");
    require(window_packets > 0 && window_packets <= 65536 && window_frames >= max_frames_per_packet &&
            window_frames <= std::uint64_t(window_packets) * max_frames_per_packet, "invalid outstanding frame window");
    require(encoding_available(encoding), "frame encoding is unavailable");
}
Event clock_anchor(std::uint32_t offset, std::int64_t time, std::uint64_t uncertainty) {
    Event result; result.offset = offset; result.type = 0x8002;
    store<std::uint64_t>(result.value, 0, static_cast<std::uint64_t>(time));
    store<std::uint64_t>(result.value, 8, uncertainty); return result;
}
Event source_fault(Status cause, std::optional<std::uint64_t> lost) {
    if (cause != Status::SourceOverflow && cause != Status::ContinuityLost) throw std::invalid_argument("not a producer loss fault");
    Event result; result.type = 0x8001;
    store<std::uint16_t>(result.value, 0, cause == Status::SourceOverflow ? 1 : 3);
    store<std::uint16_t>(result.value, 2, lost ? 1 : 0);
    store<std::uint64_t>(result.value, 8, lost.value_or(0)); return result;
}

struct Codec::Impl {
    Reservation reserved;
    Descriptor descriptor;
    std::vector<std::uint8_t> encoded, compressed, decoded;
    std::vector<std::uint64_t> encoder_workspace, decoder_workspace;
#ifdef S7_FRAMES_ZSTD
    ZSTD_CCtx* encoder{};
    ZSTD_DCtx* decoder{};
#endif
    explicit Impl(Descriptor d, std::shared_ptr<BufferBudget> budget) : descriptor(std::move(d)) {
        descriptor.validate();
        std::size_t encoder_words{}, decoder_words{};
#ifdef S7_FRAMES_LZ4
        if (descriptor.encoding == Encoding::Lz4) encoder_words = (LZ4_sizeofState() + 7) / 8;
#endif
#ifdef S7_FRAMES_ZSTD
        if (descriptor.encoding == Encoding::Zstd) {
            const auto enc = ZSTD_estimateCCtxSize(1), dec = ZSTD_estimateDCtxSize();
            require(!ZSTD_isError(enc) && !ZSTD_isError(dec) && enc < 16 * 1024 * 1024 && dec < 1024 * 1024, "invalid codec workspace bound");
            encoder_words = (enc + 7) / 8; decoder_words = (dec + 7) / 8;
        }
#endif
        reserved.reserve(std::move(budget), kMaxMessageBytes + 2 * wire::kMaxPayloadBytes + 2048 + (encoder_words + decoder_words) * 8);
        encoded.reserve(kMaxMessageBytes); compressed.resize(wire::kMaxPayloadBytes + 2048); decoded.resize(wire::kMaxPayloadBytes);
        encoder_workspace.resize(encoder_words); decoder_workspace.resize(decoder_words);
#ifdef S7_FRAMES_ZSTD
        if (descriptor.encoding == Encoding::Zstd) {
            encoder = ZSTD_initStaticCCtx(encoder_workspace.data(), encoder_workspace.size() * 8);
            decoder = ZSTD_initStaticDCtx(decoder_workspace.data(), decoder_workspace.size() * 8);
            require(encoder && decoder, "cannot initialize static codec workspace");
        }
#endif
        const auto actual = encoded.capacity() + compressed.capacity() + decoded.capacity() +
                            (encoder_workspace.capacity() + decoder_workspace.capacity()) * 8;
        if (actual > reserved.bytes) {
            if (reserved.budget) reserved.budget->reserve(actual - reserved.bytes);
            reserved.bytes = actual;
        }
    }
};
Codec::Codec(Descriptor descriptor, std::shared_ptr<BufferBudget> budget) : impl_(std::make_unique<Impl>(std::move(descriptor), std::move(budget))) {}
Codec::~Codec() = default;
std::size_t Codec::workspace_bytes() const {
    const auto& c = *impl_;
    return c.encoded.capacity() + c.compressed.capacity() + c.decoded.capacity() +
           (c.encoder_workspace.capacity() + c.decoder_workspace.capacity()) * 8;
}
Bytes Codec::encode(std::uint32_t alias, std::uint64_t sequence, const Info& info, Bytes payload) {
    auto& c = *impl_; const auto& d = c.descriptor;
    validate_info(d, info, payload.size());
    require(alias != 0 && sequence != UINT64_MAX, "invalid frame alias/sequence");
    auto chosen = Encoding::Raw;
    Bytes body = payload;
    if (!info.fault_only && d.encoding != Encoding::Raw) {
        std::size_t size{};
#ifdef S7_FRAMES_LZ4
        if (d.encoding == Encoding::Lz4) {
            const auto n = LZ4_compress_fast_extState(c.encoder_workspace.data(), reinterpret_cast<const char*>(payload.data()),
                reinterpret_cast<char*>(c.compressed.data()), static_cast<int>(payload.size()), static_cast<int>(c.compressed.size()), 1);
            require(n > 0, "LZ4 compression failed"); size = static_cast<std::size_t>(n);
        }
#endif
#ifdef S7_FRAMES_ZSTD
        if (d.encoding == Encoding::Zstd) {
            size = ZSTD_compressCCtx(c.encoder, c.compressed.data(), c.compressed.size(), payload.data(), payload.size(), 1);
            require(!ZSTD_isError(size), "Zstandard compression failed");
        }
#endif
        if (size && size < payload.size()) { chosen = d.encoding; body = Bytes(c.compressed.data(), size); }
    }
    auto& out = c.encoded; out.clear();
    out.push_back(wire::kDataVersion); out.push_back(wire::kFlagExtensions);
    put<std::uint16_t>(out, static_cast<std::uint16_t>(d.type)); put(out, alias); put(out, sequence);
    const bool encoding = !info.fault_only && d.encoding != Encoding::Raw;
    put<std::uint16_t>(out, static_cast<std::uint16_t>(4 + 56 + 24 * info.event_count + (encoding ? 16 : 0)));
    put(out, kFrameExtension); put<std::uint16_t>(out, static_cast<std::uint16_t>(56 + 24 * info.event_count));
    put<std::uint16_t>(out, 1); put<std::uint16_t>(out, (info.segment_start ? 1 : 0) | (info.fault_only ? 2 : 0));
    put(out, info.frames); out.insert(out.end(), info.segment.begin(), info.segment.end());
    put(out, info.first_frame); put(out, info.configuration_revision); put(out, info.timing_revision);
    put(out, info.event_count); put<std::uint32_t>(out, 24 * info.event_count);
    for (std::size_t i = 0; i < info.event_count; ++i) {
        const auto& e = info.events[i]; put(out, e.offset); put(out, e.type); put<std::uint16_t>(out, 16);
        out.insert(out.end(), e.value.begin(), e.value.end());
    }
    if (encoding) {
        put(out, kEncodingExtension); put<std::uint16_t>(out, 12); put<std::uint16_t>(out, 1);
        put<std::uint16_t>(out, static_cast<std::uint16_t>(chosen));
        put<std::uint32_t>(out, static_cast<std::uint32_t>(payload.size())); put(out, checksum(payload));
    }
    while ((out.size() - 16) % 8) out.push_back(0);
    out.insert(out.end(), body.begin(), body.end()); return out;
}
Packet Codec::decode(Bytes message, BufferPool& pool, std::uint64_t available_frames) {
    auto& c = *impl_; const auto& d = c.descriptor;
    require(message.size() >= 18 && message.size() <= kMaxMessageBytes, "invalid frame message length");
    require(message[0] == 1 && message[1] == 1 && get<std::uint16_t>(message, 2) == static_cast<std::uint16_t>(d.type), "frame header/type mismatch");
    Packet packet; packet.alias = get<std::uint32_t>(message, 4); packet.sequence = get<std::uint64_t>(message, 8);
    require(packet.alias != 0 && packet.sequence != UINT64_MAX, "invalid frame alias/sequence");
    const std::size_t tlv = get<std::uint16_t>(message, 16), end = 18 + tlv, payload_start = 16 + (2 + tlv + 7) / 8 * 8;
    require(tlv <= 56 + kMaxMetadataBytes + 64 && payload_start <= message.size(), "frame extension area exceeds bound");
    for (auto i = end; i < payload_start; ++i) require(message[i] == 0, "nonzero extension padding");
    bool have_frames{}, have_encoding{}; auto codec = Encoding::Raw;
    std::size_t decoded_size{}; std::uint32_t crc{};
    std::uint16_t previous{};
    for (std::size_t offset = 18; offset < end;) {
        require(end - offset >= 4, "truncated frame extension");
        const auto type = get<std::uint16_t>(message, offset); const std::size_t length = get<std::uint16_t>(message, offset + 2);
        require(type > previous && length <= end - offset - 4, "duplicate/unsorted/truncated extension"); previous = type;
        const auto value = message.subspan(offset + 4, length); offset += 4 + length;
        if (type == kFrameExtension) {
            require(length >= 56 && get<std::uint16_t>(value, 0) == 1, "invalid frame extension version/length");
            auto& info = packet.info; const auto flags = get<std::uint16_t>(value, 2);
            require(flags <= 3, "unknown frame flags"); info.segment_start = flags & 1; info.fault_only = flags & 2;
            info.frames = get<std::uint32_t>(value, 4); std::copy_n(value.begin() + 8, 16, info.segment.begin());
            info.first_frame = get<std::uint64_t>(value, 24); info.configuration_revision = get<std::uint64_t>(value, 32);
            info.timing_revision = get<std::uint64_t>(value, 40); info.event_count = get<std::uint32_t>(value, 48);
            require(info.event_count <= kMaxEvents && get<std::uint32_t>(value, 52) == info.event_count * 24 &&
                    length == 56 + info.event_count * 24, "invalid frame events length/count");
            for (std::size_t i = 0; i < info.event_count; ++i) {
                const auto base = 56 + i * 24; auto& e = info.events[i];
                e.offset = get<std::uint32_t>(value, base); e.type = get<std::uint16_t>(value, base + 4);
                require(get<std::uint16_t>(value, base + 6) == 16, "invalid event value size");
                std::copy_n(value.begin() + base + 8, 16, e.value.begin());
            }
            decoded_size = std::size_t(info.frames) * d.frame_bytes(); have_frames = true;
        } else if (type == kEncodingExtension) {
            require(length == 12 && get<std::uint16_t>(value, 0) == 1, "invalid encoding extension");
            codec = static_cast<Encoding>(get<std::uint16_t>(value, 2));
            require(have_frames && get<std::uint32_t>(value, 4) == decoded_size, "decoded length mismatch");
            crc = get<std::uint32_t>(value, 8); have_encoding = true;
        } else require((type & 0x8000) == 0 && type != wire::kTimestampsExtension, "unsupported critical frame extension");
    }
    require(have_frames && decoded_size <= wire::kMaxPayloadBytes, "missing/oversized frames");
    validate_info(d, packet.info, decoded_size);
    require(packet.info.frames <= available_frames, "receive frame window exceeded");
    require(have_encoding == (!packet.info.fault_only && d.encoding != Encoding::Raw), "encoding profile mismatch");
    require(codec == Encoding::Raw || codec == d.encoding, "unnegotiated codec");
    const auto body = message.subspan(payload_start);
    require(body.size() <= wire::kMaxPayloadBytes, "encoded payload exceeds bound");
    Bytes raw = body;
    if (codec != Encoding::Raw) {
        require(!body.empty() && body.size() < decoded_size, "noncanonical compressed block");
        std::size_t size{};
#ifdef S7_FRAMES_LZ4
        if (codec == Encoding::Lz4) {
            const auto n = LZ4_decompress_safe(reinterpret_cast<const char*>(body.data()), reinterpret_cast<char*>(c.decoded.data()),
                static_cast<int>(body.size()), static_cast<int>(decoded_size));
            require(n >= 0, "invalid LZ4 block"); size = static_cast<std::size_t>(n);
        }
#endif
#ifdef S7_FRAMES_ZSTD
        if (codec == Encoding::Zstd) {
            require(ZSTD_getFrameContentSize(body.data(), body.size()) == decoded_size &&
                    ZSTD_getDictID_fromFrame(body.data(), body.size()) == 0 &&
                    ZSTD_findFrameCompressedSize(body.data(), body.size()) == body.size(), "invalid Zstandard frame content/dictionary/trailer");
            size = ZSTD_decompressDCtx(c.decoder, c.decoded.data(), decoded_size, body.data(), body.size());
            require(!ZSTD_isError(size), "invalid Zstandard frame");
        }
#endif
        require(size == decoded_size, "decoded frame size mismatch"); raw = Bytes(c.decoded.data(), size);
    }
    require(raw.size() == decoded_size && (!have_encoding || checksum(raw) == crc), "frame payload/checksum mismatch");
    if (!packet.info.fault_only) {
        packet.payload = pool.copy(raw);
        if (!packet.payload) throw std::length_error("receive frame pool exhausted");
    }
    return packet;
}

struct FrameSink::Impl {
    struct Logged { std::uint64_t sequence; std::uint32_t frames; BufferPool::Lease message, input; };
    struct Peer { std::uint64_t sent{}, acknowledged{}; bool attached{}; };
    Descriptor d; stream::SequenceStore& sequences; std::shared_ptr<BufferPool> pool;
    BufferPool terminal; Codec codec; std::uint32_t alias;
    Reservation native_reservation;
    mutable std::mutex mutex;
    Ring<Logged> log;
    std::unordered_map<stream::ConsumerKey, Peer> peers;
    std::vector<std::uint8_t> native;
    wire::StreamId segment{wire::random_stream_id()};
    std::uint64_t next{}, reservation{}, frame{}, outstanding{};
    SourceState state{SourceState::Active};
    Status cause{Status::Ok};
    History history;
    explicit Impl(Descriptor descriptor, stream::SequenceStore& seq, std::shared_ptr<BufferPool> buffers, std::uint32_t a)
        : d(std::move(descriptor)), sequences(seq), pool(std::move(buffers)), terminal(1, 256, pool ? pool->budget() : nullptr),
          codec(d, pool ? pool->budget() : nullptr), alias(a), log(std::size_t(d.window_packets) + 1) {
        check_pool(d, pool, true); require(alias != 0, "zero frame alias");
        next = reservation = seq.reserved_end(d.stream_id); native.reserve(wire::kMaxPayloadBytes);
        native_reservation.reserve(pool->budget(), native.capacity() + log.storage_bytes() + sizeof(History));
    }
    void reserve() {
        require(next != UINT64_MAX, "packet sequence exhausted");
        if (next == reservation) {
            require(next <= UINT64_MAX - stream::kSequenceReservation, "sequence reservation overflow");
            const auto end = next + stream::kSequenceReservation; sequences.reserve(d.stream_id, end); reservation = end;
        }
    }
    Info info(std::uint32_t count, std::span<const Event> events) const {
        require(events.size() <= kMaxEvents, "too many frame events");
        Info result; result.frames = count; result.segment = segment; result.first_frame = frame;
        result.segment_start = frame == 0; result.configuration_revision = d.configuration_revision;
        result.timing_revision = d.timing_revision; result.event_count = static_cast<std::uint32_t>(events.size());
        std::copy(events.begin(), events.end(), result.events.begin()); return result;
    }
    PublishResult publish(Bytes bytes, std::uint32_t count, std::span<const Event> events, BufferPool::Lease input = {}) {
        if (state != SourceState::Active) return {0, state == SourceState::Closed ? Status::Closed : cause};
        const auto metadata = info(count, events); validate_info(d, metadata, bytes.size());
        if (log.size() >= d.window_packets || count > d.window_frames - outstanding) return {0, Status::Backpressure};
        reserve();
        auto sealed = pool->copy(codec.encode(alias, next, metadata, bytes));
        if (!sealed) return {0, Status::Backpressure};
        log.push_back({next, count, std::move(sealed), std::move(input)}); ++next; frame += count; outstanding += count;
        return {count, Status::Ok};
    }
    void trim() {
        if (peers.empty()) return;
        auto minimum = UINT64_MAX;
        for (const auto& [_, p] : peers) minimum = std::min(minimum, p.acknowledged);
        while (!log.empty() && log.front().sequence < minimum) { outstanding -= log.front().frames; log.pop_front(); }
        if (state == SourceState::DrainingFault && log.empty()) state = SourceState::Faulted;
    }
};
FrameSink::FrameSink(Descriptor d, stream::SequenceStore& seq, std::shared_ptr<BufferPool> pool, std::uint32_t alias)
    : impl_(std::make_unique<Impl>(std::move(d), seq, std::move(pool), alias)) {}
FrameSink::~FrameSink() = default;
PublishResult FrameSink::publish_shared(const BufferPool::Lease& payload, std::uint32_t frames, std::span<const Event> events, std::stop_token stop) {
    auto& p = *impl_; std::lock_guard lock(p.mutex);
    if (stop.stop_requested()) return {0, Status::Cancelled};
    require(payload.capacity_bytes() <= wire::kMaxPayloadBytes, "shared input allocation exceeds frame quota");
    return p.publish(payload.bytes(), frames, events, payload);
}
PublishResult FrameSink::publish_copy(const void* values, std::size_t frames, std::span<const Event> events, std::stop_token stop) {
    auto& p = *impl_; std::lock_guard lock(p.mutex);
    if (!frames) return {};
    if (!values || frames > SIZE_MAX / p.d.channels.size() / signal_type_info(p.d.type).storage_bytes)
        throw std::invalid_argument("invalid native frame input");
    require(events.size() <= kMaxEvents, "too many input events");
    for (std::size_t i = 0; i < events.size(); ++i) {
        const auto& e = events[i]; require(e.offset < frames && e.type == 0x8002, "invalid input event");
        if (i) require(e.offset > events[i - 1].offset, "duplicate/unsorted input events");
    }
    std::size_t done{};
    while (done < frames) {
        if (stop.stop_requested()) return {done, Status::Cancelled};
        const auto count = static_cast<std::uint32_t>(std::min<std::size_t>(frames - done, p.d.max_frames_per_packet));
        std::array<Event, kMaxEvents> local{}; std::size_t n{};
        for (auto e : events) if (e.offset >= done && e.offset - done < count) { e.offset -= static_cast<std::uint32_t>(done); local[n++] = e; }
        p.native.clear();
        wire::encode_elements(p.d.type, static_cast<const std::uint8_t*>(values) + done * p.d.channels.size() * signal_type_info(p.d.type).storage_bytes,
                              std::size_t(count) * p.d.channels.size(), p.native);
        const auto result = p.publish(p.native, count, std::span(local.data(), n)); done += result.frames;
        if (result.status != Status::Ok) return {done, result.status};
    }
    return {done, Status::Ok};
}
void FrameSink::resume(const stream::ConsumerKey& peer, std::uint64_t next) {
    auto& p = *impl_; std::lock_guard lock(p.mutex);
    const auto oldest = p.log.empty() ? p.next : p.log.front().sequence;
    if (next > p.next) throw std::invalid_argument("resume ahead of producer");
    require(!peer.empty() && peer.size() <= 128 && (p.peers.contains(peer) || p.peers.size() < 64), "invalid/too many consumers");
    auto [it, inserted] = p.peers.try_emplace(peer);
    auto& entry = it->second;
    if (inserted) entry.acknowledged = oldest;
    // RESUME means receipt position, not consumption. Keep the consumption
    // ledger across disconnects until a separate ACK explicitly advances it.
    entry.sent = std::max(next, oldest); entry.attached = true;
    p.trim();
}
void FrameSink::detach(const stream::ConsumerKey& peer) { auto& p = *impl_; std::lock_guard lock(p.mutex); if (auto it = p.peers.find(peer); it != p.peers.end()) it->second.attached = false; }
void FrameSink::acknowledge(const stream::ConsumerKey& peer, std::uint64_t next) {
    auto& p = *impl_; std::lock_guard lock(p.mutex);
    if (auto it = p.peers.find(peer); it != p.peers.end() && next <= it->second.sent) { it->second.acknowledged = std::max(it->second.acknowledged, next); p.trim(); }
}
BufferPool::Lease FrameSink::next_message(const stream::ConsumerKey& peer) {
    auto& p = *impl_; std::lock_guard lock(p.mutex);
    const auto it = p.peers.find(peer);
    if (p.state == SourceState::Closed || it == p.peers.end() || !it->second.attached || p.log.empty() || it->second.sent >= p.next) return {};
    const auto index = it->second.sent - p.log.front().sequence;
    if (index >= p.log.size()) return {};
    auto message = p.log[index].message; ++it->second.sent; return message;
}
void FrameSink::fault(Status cause, std::optional<std::uint64_t> lost) {
    const auto event = source_fault(cause, lost);
    auto& p = *impl_; std::lock_guard lock(p.mutex);
    if (p.state != SourceState::Active) return;
    auto info = p.info(0, std::span(&event, 1)); info.fault_only = true;
    p.reserve(); auto marker = p.terminal.copy(p.codec.encode(p.alias, p.next, info, {}));
    require(bool(marker), "reserved terminal slot still retained");
    p.log.push_back({p.next, 0, std::move(marker), {}}); ++p.next; p.cause = cause; p.state = SourceState::DrainingFault;
    p.history.add({cause, p.next - 1, p.frame, p.segment, lost});
}
void FrameSink::recover(wire::StreamId segment) {
    auto& p = *impl_; std::lock_guard lock(p.mutex);
    require(p.state == SourceState::Faulted && p.log.empty() && p.terminal.leased_slots() == 0 && nonzero(segment) && segment != p.segment,
            "recovery requires consumed terminal and a new segment");
    p.segment = segment; p.frame = 0; p.cause = Status::Ok; p.state = SourceState::Active;
}
void FrameSink::close() { auto& p = *impl_; std::lock_guard lock(p.mutex); p.state = SourceState::Closed; }
SourceState FrameSink::state() const { std::lock_guard lock(impl_->mutex); return impl_->state; }
std::size_t FrameSink::log_depth() const { std::lock_guard lock(impl_->mutex); return impl_->log.size(); }
std::uint64_t FrameSink::next_sequence() const { std::lock_guard lock(impl_->mutex); return impl_->next; }
std::vector<FaultRecord> FrameSink::recent_faults() const { std::lock_guard lock(impl_->mutex); return impl_->history.snapshot(); }

struct FrameSource::Impl {
    Descriptor d; std::shared_ptr<BufferPool> pool; Codec codec; std::uint32_t alias;
    Reservation metadata_reservation;
    mutable std::mutex mutex; Ring<Packet> queue;
    std::uint64_t next{}, consumed{}, frame{}, buffered{};
    std::size_t offset{};
    std::optional<wire::StreamId> segment, required_segment;
    Status fault{Status::Ok}; std::optional<std::uint64_t> lost; bool terminal{}, closed{};
    History history;
    Impl(Descriptor descriptor, std::shared_ptr<BufferPool> buffers, std::uint32_t a)
        : d(std::move(descriptor)), pool(std::move(buffers)), codec(d, pool ? pool->budget() : nullptr), alias(a), queue(std::size_t(d.window_packets) + 1) {
        check_pool(d, pool, false); require(alias != 0, "zero frame alias");
        metadata_reservation.reserve(pool->budget(), queue.storage_bytes() + sizeof(History));
    }
    ReceiveResult consume(std::size_t count) {
        if (closed) return {0, Status::Closed, {}, {}};
        if (fault != Status::Ok) return {0, fault, {}, lost};
        if (queue.empty()) return {0, Status::Backpressure, {}, {}};
        ReceiveResult result;
        if (!queue.front().info.fault_only) {
            auto& packet = queue.front();
            const auto old = offset; const auto taken = std::min<std::size_t>(count, packet.info.frames - offset);
            result.frames = taken; result.info = packet.info; result.info.first_frame += old;
            result.info.frames = static_cast<std::uint32_t>(taken); result.info.segment_start &= old == 0; result.info.event_count = 0;
            for (std::size_t i = 0; i < packet.info.event_count; ++i) {
                auto e = packet.info.events[i];
                if (e.offset >= old && e.offset - old < taken) { e.offset -= static_cast<std::uint32_t>(old); result.info.events[result.info.event_count++] = e; }
            }
            offset += taken; buffered -= taken;
            if (offset == packet.info.frames) { consumed = packet.sequence + 1; queue.pop_front(); offset = 0; }
        }
        if (!queue.empty() && queue.front().info.fault_only) {
            fault = fault_cause(queue.front().info.events[0]);
            const auto& event = queue.front().info.events[0];
            if (get<std::uint16_t>(event.value, 2)) lost = get<std::uint64_t>(event.value, 8);
            result.lost_frames = lost;
            history.add({fault, queue.front().sequence, queue.front().info.first_frame, queue.front().info.segment, lost});
            if (!result.frames) result.info = queue.front().info;
            consumed = queue.front().sequence + 1; queue.pop_front(); result.status = fault;
        }
        return result;
    }
};
FrameSource::FrameSource(Descriptor d, std::shared_ptr<BufferPool> pool, std::uint32_t alias)
    : impl_(std::make_unique<Impl>(std::move(d), std::move(pool), alias)) {}
FrameSource::~FrameSource() = default;
bool FrameSource::on_message(Bytes bytes) {
    auto& p = *impl_; std::lock_guard lock(p.mutex);
    if (p.closed || p.fault != Status::Ok) return false;
    try {
        // Dedupe before decompression/pool admission; aliases are checked first.
        require(bytes.size() >= 18 && bytes.size() <= kMaxMessageBytes && bytes[0] == wire::kDataVersion &&
                bytes[1] == wire::kFlagExtensions && get<std::uint16_t>(bytes, 2) == static_cast<std::uint16_t>(p.d.type) &&
                get<std::uint32_t>(bytes, 4) == p.alias, "frame header/type/alias mismatch");
        const auto sequence = get<std::uint64_t>(bytes, 8);
        if (sequence < p.next) return false;
        if (sequence > p.next) {
            p.fault = Status::SequenceGap; p.history.add({p.fault, p.next, p.frame, p.segment.value_or(wire::StreamId{}), {}}); return false;
        }
        require(!p.terminal, "data follows terminal fault");
        const auto budget = p.queue.size() >= p.d.window_packets ? 0 : p.d.window_frames - p.buffered;
        auto packet = p.codec.decode(bytes, *p.pool, budget);
        const auto info = packet.info;
        require(p.queue.size() < std::size_t(p.d.window_packets) + 1 && info.frames <= p.d.window_frames - p.buffered, "receive frame window exceeded");
        if (!p.segment) require(info.segment_start && info.first_frame == 0 && (!p.required_segment || *p.required_segment == info.segment), "missing/wrong initial segment");
        else require(info.segment == *p.segment && !info.segment_start, "unapproved segment change");
        require(info.first_frame == p.frame, "noncontiguous frame position");
        p.queue.push_back(std::move(packet)); p.segment = info.segment; p.frame += info.frames;
        p.buffered += info.frames; p.terminal = info.fault_only; ++p.next; return true;
    } catch (const wire::FormatError&) { p.fault = Status::Malformed; }
      catch (const std::length_error&) { p.fault = Status::Malformed; }
    p.history.add({p.fault, p.next, p.frame, p.segment.value_or(wire::StreamId{}), {}}); return false;
}
Packet FrameSource::front_packet() const {
    auto& p = *impl_; std::lock_guard lock(p.mutex);
    if (p.closed || p.fault != Status::Ok || p.queue.empty() || p.queue.front().info.fault_only) return {};
    const auto& packet = p.queue.front(); const auto bytes = p.d.frame_bytes();
    Packet result = packet;
    result.payload = packet.payload.slice(p.offset * bytes, (packet.info.frames - p.offset) * bytes);
    result.info.first_frame += p.offset; result.info.frames -= static_cast<std::uint32_t>(p.offset);
    result.info.segment_start &= p.offset == 0; result.info.event_count = 0;
    for (std::size_t i = 0; i < packet.info.event_count; ++i) {
        auto e = packet.info.events[i];
        if (e.offset >= p.offset) { e.offset -= static_cast<std::uint32_t>(p.offset); result.info.events[result.info.event_count++] = e; }
    }
    return result;
}
BufferPool::Lease FrameSource::front() const { return front_packet().payload; }
ReceiveResult FrameSource::consume(std::size_t count) { std::lock_guard lock(impl_->mutex); return impl_->consume(count); }
ReceiveResult FrameSource::receive_copy(void* values, std::size_t count) {
    auto& p = *impl_; std::lock_guard lock(p.mutex);
    if (!p.closed && p.fault == Status::Ok && !p.queue.empty() && !p.queue.front().info.fault_only) {
        const auto take = std::min<std::size_t>(count, p.queue.front().info.frames - p.offset);
        if (take && !values) throw std::invalid_argument("null frame output");
        if (take) wire::decode_elements(p.d.type, p.queue.front().payload.bytes().subspan(p.offset * p.d.frame_bytes(), take * p.d.frame_bytes()), values);
    }
    return p.consume(count);
}
std::uint64_t FrameSource::next_expected() const { std::lock_guard lock(impl_->mutex); return impl_->next; }
std::uint64_t FrameSource::consumed_up_to() const { std::lock_guard lock(impl_->mutex); return impl_->consumed; }
Status FrameSource::status() const { std::lock_guard lock(impl_->mutex); return impl_->closed ? Status::Closed : impl_->fault; }
std::vector<FaultRecord> FrameSource::recent_faults() const { std::lock_guard lock(impl_->mutex); return impl_->history.snapshot(); }
void FrameSource::sink_underflow(std::optional<std::uint64_t> lost) {
    std::lock_guard lock(impl_->mutex);
    if (impl_->fault == Status::Ok) {
        impl_->fault = Status::SinkUnderflow; impl_->lost = lost;
        impl_->history.add({impl_->fault, impl_->next, impl_->frame, impl_->segment.value_or(wire::StreamId{}), lost});
    }
}
void FrameSource::recover(std::uint64_t next, wire::StreamId segment) {
    auto& p = *impl_; std::lock_guard lock(p.mutex);
    require(!p.closed && p.fault != Status::Ok && next >= p.next && nonzero(segment) && (!p.segment || segment != *p.segment), "invalid frame recovery boundary");
    p.queue.clear(); p.next = p.consumed = next; p.frame = p.buffered = p.offset = 0;
    p.segment.reset(); p.required_segment = segment; p.fault = Status::Ok; p.lost.reset(); p.terminal = false;
}
void FrameSource::close() { std::lock_guard lock(impl_->mutex); impl_->closed = true; }
} // namespace s7::frames
