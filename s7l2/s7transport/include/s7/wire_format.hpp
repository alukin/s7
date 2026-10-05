#ifndef S7TRANSPORT_INCLUDE_S7_WIRE_FORMAT_HPP
#define S7TRANSPORT_INCLUDE_S7_WIRE_FORMAT_HPP

// S7 data stream protocol, wire format version 1
// (docs/S7_Network_Wire_Protocol_v1.md, sections 7, 8 and 12.1).
//
// Every multi-byte field and value is little-endian. Values are always
// written through explicit little-endian encoding, never by copying host
// memory, so the format is the same on big-endian hosts.

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

#include <s7/signal_types.hpp>

namespace s7::wire {

inline constexpr std::uint8_t kDataVersion = 1;
inline constexpr std::uint8_t kControlVersion = 0x80 | 1;
inline constexpr std::size_t kDataHeaderBytes = 16;
inline constexpr std::size_t kControlMessageBytes = 32;
inline constexpr std::size_t kMaxPayloadBytes = 256 * 1024;
inline constexpr std::uint8_t kFlagExtensions = 0x01;
inline constexpr std::uint16_t kTimestampsExtension = 0x8001; // critical
inline constexpr std::uint16_t kCriticalExtensionBit = 0x8000;

// A malformed or unsupported message; the receiver drops it and reports a fault.
class FormatError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

using StreamId = std::array<std::uint8_t, 16>; // RFC 4122 byte order

[[nodiscard]] StreamId parse_stream_id(const std::string& uuid);
[[nodiscard]] std::string to_string(const StreamId& id);
// A new random (version 4) UUID from OpenSSL's CSPRNG.
[[nodiscard]] StreamId random_stream_id();

enum class TimestampRole : std::uint8_t {
    ModelTime = 1,
    SourceTime = 2,
    ProcessingTime = 3,
};

struct TimestampRecord {
    TimestampRole role{TimestampRole::ModelTime};
    std::int64_t nanoseconds{};

    friend bool operator==(const TimestampRecord&, const TimestampRecord&) = default;
};

struct DataPacket {
    SignalType type{SignalType::F64};
    std::uint32_t stream_alias{};
    std::uint64_t sequence{};
    std::vector<TimestampRecord> timestamps; // empty: no TIMESTAMPS extension
    std::vector<std::uint8_t> payload;       // encoded elements (see encode_elements)
};

// Encoded size of `count` elements of `type` (12-bit types are packed).
[[nodiscard]] std::size_t payload_bytes(SignalType type, std::size_t count);
// Element count of a payload of `bytes`; throws FormatError if not a whole number.
[[nodiscard]] std::size_t payload_element_count(SignalType type, std::size_t bytes);

// Encodes `count` elements stored as signal_storage_t<type> (host memory) into
// the wire representation, appending to `out`.
void encode_elements(SignalType type, const void* elements, std::size_t count, std::vector<std::uint8_t>& out);
// Decodes a payload into host storage; `out` holds payload_element_count() elements.
void decode_elements(SignalType type, std::span<const std::uint8_t> payload, void* out);

[[nodiscard]] std::vector<std::uint8_t> encode_data_packet(const DataPacket& packet);
// Validates and decodes; throws FormatError.
[[nodiscard]] DataPacket decode_data_packet(std::span<const std::uint8_t> message);

enum class ControlKind : std::uint8_t {
    Ack = 1,           // consumer -> producer: everything before next_expected is delivered
    Resume = 2,        // consumer -> producer: bind alias to stream_id, replay from next_expected
    ResumeNoState = 3, // consumer -> producer: restarted without stream state
};

struct ControlMessage {
    ControlKind kind{ControlKind::Ack};
    std::uint32_t stream_alias{};
    std::uint64_t next_expected{};
    StreamId stream_id{}; // zero in Ack

    friend bool operator==(const ControlMessage&, const ControlMessage&) = default;
};

[[nodiscard]] std::array<std::uint8_t, kControlMessageBytes> encode_control_message(const ControlMessage& message);
[[nodiscard]] ControlMessage decode_control_message(std::span<const std::uint8_t> message);

// Data packet or stream control message (by the version byte).
[[nodiscard]] inline bool is_control_message(const std::span<const std::uint8_t> message)
{
    return !message.empty() && message[0] == kControlVersion;
}

} // namespace s7::wire

#endif // S7TRANSPORT_INCLUDE_S7_WIRE_FORMAT_HPP
