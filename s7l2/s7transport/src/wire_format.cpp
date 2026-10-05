#include "s7/wire_format.hpp"

#include <algorithm>
#include <bit>
#include <cstring>

#include <boost/uuid/string_generator.hpp>
#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_io.hpp>

namespace s7::wire {
namespace {

// ---- little-endian primitives (independent of host byte order) -------------

template <typename U>
void put_le(std::vector<std::uint8_t>& out, U value)
{
    for (std::size_t index = 0; index < sizeof(U); ++index) {
        out.push_back(static_cast<std::uint8_t>(value & 0xFF));
        value = static_cast<U>(value >> 8);
    }
}

template <typename U>
[[nodiscard]] U get_le(const std::span<const std::uint8_t> bytes, const std::size_t offset)
{
    U value{};
    for (std::size_t index = sizeof(U); index-- > 0;) {
        value = static_cast<U>((value << 8) | bytes[offset + index]);
    }
    return value;
}

template <std::size_t Bytes>
struct UnsignedOfSize;
template <> struct UnsignedOfSize<1> { using type = std::uint8_t; };
template <> struct UnsignedOfSize<2> { using type = std::uint16_t; };
template <> struct UnsignedOfSize<4> { using type = std::uint32_t; };
template <> struct UnsignedOfSize<8> { using type = std::uint64_t; };
template <> struct UnsignedOfSize<16> { using type = uint128_t; };

// The bit pattern of one real component as an unsigned integer of its size
// (two's complement for signed integers, IEEE interchange bits for floats).
template <typename C>
[[nodiscard]] auto component_bits(const C value)
{
    typename UnsignedOfSize<sizeof(C)>::type bits{};
    std::memcpy(&bits, &value, sizeof(C));
    return bits;
}

template <typename C>
[[nodiscard]] C component_from_bits(const typename UnsignedOfSize<sizeof(C)>::type bits)
{
    C value{};
    std::memcpy(&value, &bits, sizeof(C));
    return value;
}

template <SignalType Type>
constexpr bool is_packed12 = SignalTraits<Type>::component_bits == 12;

template <SignalType Type>
using component_t = signal_storage_t<component_signal_type(Type)>;

// Components of one element, in wire order (re, im for complex types).
template <SignalType Type>
void element_components(const signal_storage_t<Type>& value, component_t<Type>* out)
{
    constexpr auto kind = signal_type_info(Type).kind;
    if constexpr (kind == SignalKind::ComplexFloat) {
        out[0] = value.real();
        out[1] = value.imag();
    } else if constexpr (kind == SignalKind::ComplexInteger || kind == SignalKind::ComplexUnsigned) {
        out[0] = value.re;
        out[1] = value.im;
    } else {
        out[0] = value;
    }
}

template <SignalType Type>
[[nodiscard]] signal_storage_t<Type> element_from_components(const component_t<Type>* parts)
{
    if constexpr (signal_type_info(Type).is_complex()) {
        return signal_storage_t<Type>{parts[0], parts[1]};
    } else {
        return parts[0];
    }
}

template <SignalType Type>
constexpr std::size_t components_per_element = signal_type_info(Type).is_complex() ? 2 : 1;

[[nodiscard]] std::size_t packed12_bytes(const std::size_t values)
{
    return 3 * (values / 2) + 2 * (values % 2);
}

} // namespace

StreamId parse_stream_id(const std::string& uuid)
{
    try {
        const auto parsed = boost::uuids::string_generator{}(uuid);
        StreamId id{};
        std::copy(parsed.begin(), parsed.end(), id.begin());
        return id;
    } catch (const std::exception&) {
        throw std::invalid_argument("Invalid stream id (expected a UUID): '" + uuid + "'.");
    }
}

std::string to_string(const StreamId& id)
{
    boost::uuids::uuid uuid{};
    std::copy(id.begin(), id.end(), uuid.begin());
    return boost::uuids::to_string(uuid);
}

std::size_t payload_bytes(const SignalType type, const std::size_t count)
{
    return visit_signal_type(type, [count]<SignalType T>() -> std::size_t {
        const auto values = count * components_per_element<T>;
        if constexpr (is_packed12<T>) {
            return packed12_bytes(values);
        } else {
            return values * sizeof(component_t<T>);
        }
    });
}

std::size_t payload_element_count(const SignalType type, const std::size_t bytes)
{
    return visit_signal_type(type, [type, bytes]<SignalType T>() -> std::size_t {
        constexpr auto per_element = components_per_element<T>;
        std::size_t values = 0;
        if constexpr (is_packed12<T>) {
            if (bytes % 3 == 1) {
                throw FormatError("payload length is not a whole number of 12-bit values");
            }
            values = (bytes / 3) * 2 + (bytes % 3 == 2 ? 1 : 0);
        } else {
            if (bytes % sizeof(component_t<T>) != 0) {
                throw FormatError("payload length is not a whole number of " + std::string(signal_type_name(type)) + " values");
            }
            values = bytes / sizeof(component_t<T>);
        }
        if (values % per_element != 0) {
            throw FormatError("payload holds an incomplete complex value");
        }
        return values / per_element;
    });
}

void encode_elements(const SignalType type, const void* elements, const std::size_t count, std::vector<std::uint8_t>& out)
{
    visit_signal_type(type, [&]<SignalType T>() {
        using C = component_t<T>;
        constexpr auto per_element = components_per_element<T>;
        const auto* values = static_cast<const signal_storage_t<T>*>(elements);
        out.reserve(out.size() + payload_bytes(T, count));
        if constexpr (is_packed12<T>) {
            // Pairs of 12-bit values in a little-endian 24-bit word: first in
            // bits 0-11, second in 12-23; an odd last value in 16 bits.
            std::vector<std::uint16_t> codes;
            codes.reserve(count * per_element);
            for (std::size_t index = 0; index < count; ++index) {
                C parts[per_element];
                element_components<T>(values[index], parts);
                for (const auto part : parts) {
                    codes.push_back(static_cast<std::uint16_t>(static_cast<std::uint16_t>(part) & 0x0FFF));
                }
            }
            std::size_t index = 0;
            for (; index + 1 < codes.size(); index += 2) {
                const std::uint32_t word = codes[index] | (static_cast<std::uint32_t>(codes[index + 1]) << 12);
                out.push_back(static_cast<std::uint8_t>(word & 0xFF));
                out.push_back(static_cast<std::uint8_t>((word >> 8) & 0xFF));
                out.push_back(static_cast<std::uint8_t>((word >> 16) & 0xFF));
            }
            if (index < codes.size()) {
                put_le<std::uint16_t>(out, codes[index]);
            }
        } else {
            for (std::size_t index = 0; index < count; ++index) {
                C parts[per_element];
                element_components<T>(values[index], parts);
                for (const auto part : parts) {
                    put_le(out, component_bits(part));
                }
            }
        }
    });
}

void decode_elements(const SignalType type, const std::span<const std::uint8_t> payload, void* out)
{
    const auto count = payload_element_count(type, payload.size());
    visit_signal_type(type, [&]<SignalType T>() {
        using C = component_t<T>;
        constexpr auto per_element = components_per_element<T>;
        auto* values = static_cast<signal_storage_t<T>*>(out);
        if constexpr (is_packed12<T>) {
            std::vector<C> parts;
            parts.reserve(count * per_element);
            auto from_code = [](const std::uint16_t code) -> C {
                if constexpr (IntegerRange<component_signal_type(T)>::is_signed) {
                    return static_cast<C>((code & 0x0800) != 0 ? static_cast<int>(code) - 0x1000 : code);
                } else {
                    return static_cast<C>(code);
                }
            };
            std::size_t offset = 0;
            while (parts.size() + 2 <= count * per_element) {
                const std::uint32_t word = payload[offset] | (payload[offset + 1] << 8) | (static_cast<std::uint32_t>(payload[offset + 2]) << 16);
                parts.push_back(from_code(static_cast<std::uint16_t>(word & 0x0FFF)));
                parts.push_back(from_code(static_cast<std::uint16_t>((word >> 12) & 0x0FFF)));
                offset += 3;
            }
            if (parts.size() < count * per_element) {
                const auto code = get_le<std::uint16_t>(payload, offset);
                if ((code & 0xF000) != 0) {
                    throw FormatError("unused bits of a 12-bit value are not zero");
                }
                parts.push_back(from_code(code));
            }
            for (std::size_t index = 0; index < count; ++index) {
                values[index] = element_from_components<T>(&parts[index * per_element]);
            }
        } else {
            using U = typename UnsignedOfSize<sizeof(C)>::type;
            std::size_t offset = 0;
            for (std::size_t index = 0; index < count; ++index) {
                C parts[per_element];
                for (auto& part : parts) {
                    part = component_from_bits<C>(get_le<U>(payload, offset));
                    offset += sizeof(C);
                }
                values[index] = element_from_components<T>(parts);
            }
        }
    });
}

std::vector<std::uint8_t> encode_data_packet(const DataPacket& packet)
{
    if (packet.payload.size() > kMaxPayloadBytes) {
        throw FormatError("payload exceeds 256 KiB");
    }
    static_cast<void>(payload_element_count(packet.type, packet.payload.size()));

    std::vector<std::uint8_t> out;
    out.reserve(kDataHeaderBytes + 32 + packet.payload.size());
    out.push_back(kDataVersion);
    out.push_back(packet.timestamps.empty() ? 0 : kFlagExtensions);
    put_le<std::uint16_t>(out, static_cast<std::uint16_t>(packet.type));
    put_le<std::uint32_t>(out, packet.stream_alias);
    put_le<std::uint64_t>(out, packet.sequence);

    if (!packet.timestamps.empty()) {
        auto records = packet.timestamps;
        std::ranges::sort(records, {}, &TimestampRecord::role);
        for (std::size_t index = 1; index < records.size(); ++index) {
            if (records[index].role == records[index - 1].role) {
                throw FormatError("a timestamp role appears twice");
            }
        }
        // u16 length of the TLV records, the records, zero padding to a multiple of 8.
        const auto value_length = 1 + 9 * records.size();
        const auto tlv_length = 4 + value_length;
        put_le<std::uint16_t>(out, static_cast<std::uint16_t>(tlv_length));
        put_le<std::uint16_t>(out, kTimestampsExtension);
        put_le<std::uint16_t>(out, static_cast<std::uint16_t>(value_length));
        out.push_back(static_cast<std::uint8_t>(records.size()));
        for (const auto& record : records) {
            out.push_back(static_cast<std::uint8_t>(record.role));
            put_le<std::uint64_t>(out, static_cast<std::uint64_t>(record.nanoseconds));
        }
        while ((out.size() - kDataHeaderBytes) % 8 != 0) {
            out.push_back(0);
        }
    }
    out.insert(out.end(), packet.payload.begin(), packet.payload.end());
    return out;
}

DataPacket decode_data_packet(const std::span<const std::uint8_t> message)
{
    if (message.size() < kDataHeaderBytes) {
        throw FormatError("message shorter than the 16-byte header");
    }
    if (message[0] != kDataVersion) {
        throw FormatError("unsupported data packet version " + std::to_string(message[0]));
    }
    const auto flags = message[1];
    if ((flags & ~kFlagExtensions) != 0) {
        throw FormatError("reserved flag bits are set");
    }
    DataPacket packet;
    const auto type = signal_type_from_wire_id(get_le<std::uint16_t>(message, 2));
    if (!type) {
        throw FormatError("unknown data type " + std::to_string(get_le<std::uint16_t>(message, 2)));
    }
    packet.type = *type;
    packet.stream_alias = get_le<std::uint32_t>(message, 4);
    packet.sequence = get_le<std::uint64_t>(message, 8);

    std::size_t payload_offset = kDataHeaderBytes;
    if ((flags & kFlagExtensions) != 0) {
        if (message.size() < kDataHeaderBytes + 2) {
            throw FormatError("extension area is truncated");
        }
        const std::size_t tlv_length = get_le<std::uint16_t>(message, kDataHeaderBytes);
        const auto area = (2 + tlv_length + 7) / 8 * 8;
        if (kDataHeaderBytes + area > message.size()) {
            throw FormatError("extension area exceeds the message");
        }
        for (std::size_t pad = kDataHeaderBytes + 2 + tlv_length; pad < kDataHeaderBytes + area; ++pad) {
            if (message[pad] != 0) {
                throw FormatError("extension padding is not zero");
            }
        }
        std::size_t offset = kDataHeaderBytes + 2;
        const auto end = offset + tlv_length;
        bool seen_timestamps = false;
        while (offset < end) {
            if (offset + 4 > end) {
                throw FormatError("truncated extension record");
            }
            const auto ext_type = get_le<std::uint16_t>(message, offset);
            const std::size_t ext_length = get_le<std::uint16_t>(message, offset + 2);
            offset += 4;
            if (offset + ext_length > end) {
                throw FormatError("extension record exceeds the extension area");
            }
            if (ext_type == kTimestampsExtension) {
                if (seen_timestamps) {
                    throw FormatError("TIMESTAMPS extension appears twice");
                }
                seen_timestamps = true;
                if (ext_length < 1) {
                    throw FormatError("TIMESTAMPS extension is empty");
                }
                const std::size_t count = message[offset];
                if (ext_length != 1 + 9 * count) {
                    throw FormatError("TIMESTAMPS length does not match its record count");
                }
                for (std::size_t index = 0; index < count; ++index) {
                    const auto base = offset + 1 + 9 * index;
                    const auto role = message[base];
                    if (role < 1 || role > 3) {
                        throw FormatError("unknown timestamp role " + std::to_string(role));
                    }
                    if (!packet.timestamps.empty() && role <= static_cast<std::uint8_t>(packet.timestamps.back().role)) {
                        throw FormatError("timestamp roles are not in ascending order");
                    }
                    packet.timestamps.push_back(
                        {static_cast<TimestampRole>(role), static_cast<std::int64_t>(get_le<std::uint64_t>(message, base + 1))});
                }
            } else if ((ext_type & kCriticalExtensionBit) != 0) {
                throw FormatError("unknown critical extension " + std::to_string(ext_type));
            }
            offset += ext_length;
        }
        payload_offset = kDataHeaderBytes + area;
    }

    const auto payload = message.subspan(payload_offset);
    if (payload.size() > kMaxPayloadBytes) {
        throw FormatError("payload exceeds 256 KiB");
    }
    static_cast<void>(payload_element_count(packet.type, payload.size()));
    if (payload.empty()) {
        throw FormatError("payload is empty");
    }
    // An odd final 12-bit value occupies 16 bits whose top 4 bits must be zero.
    if (signal_type_info(component_signal_type(packet.type)).component_bits == 12 && payload.size() % 3 == 2 &&
        (payload.back() & 0xF0) != 0) {
        throw FormatError("unused bits of a 12-bit value are not zero");
    }
    packet.payload.assign(payload.begin(), payload.end());
    return packet;
}

std::array<std::uint8_t, kControlMessageBytes> encode_control_message(const ControlMessage& message)
{
    std::vector<std::uint8_t> out;
    out.reserve(kControlMessageBytes);
    out.push_back(kControlVersion);
    out.push_back(static_cast<std::uint8_t>(message.kind));
    put_le<std::uint16_t>(out, 0);
    put_le<std::uint32_t>(out, message.stream_alias);
    put_le<std::uint64_t>(out, message.next_expected);
    out.insert(out.end(), message.stream_id.begin(), message.stream_id.end());
    std::array<std::uint8_t, kControlMessageBytes> bytes{};
    std::copy(out.begin(), out.end(), bytes.begin());
    return bytes;
}

ControlMessage decode_control_message(const std::span<const std::uint8_t> message)
{
    if (message.size() != kControlMessageBytes) {
        throw FormatError("stream control message must be 32 bytes");
    }
    if (message[0] != kControlVersion) {
        throw FormatError("not a stream control message");
    }
    const auto kind = message[1];
    if (kind < 1 || kind > 3) {
        throw FormatError("unknown stream control kind " + std::to_string(kind));
    }
    if (get_le<std::uint16_t>(message, 2) != 0) {
        throw FormatError("reserved field is not zero");
    }
    ControlMessage decoded;
    decoded.kind = static_cast<ControlKind>(kind);
    decoded.stream_alias = get_le<std::uint32_t>(message, 4);
    decoded.next_expected = get_le<std::uint64_t>(message, 8);
    std::copy(message.begin() + 16, message.end(), decoded.stream_id.begin());
    return decoded;
}

} // namespace s7::wire
