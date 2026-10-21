// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: LGPL-3.0-or-later

// Checks the wire format codec against the byte-exact fixtures written by
// tools/gen_wire_fixtures.py (an independent reference implementation).
#include <array>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include <boost/json.hpp>

#include <s7/signal_values.hpp>
#include <s7/wire_format.hpp>

#ifndef S7_WIRE_FIXTURES
#define S7_WIRE_FIXTURES "s7transport/tests/fixtures/wire_v1.json"
#endif

namespace {

namespace json = boost::json;
namespace wire = s7::wire;
using s7::SignalType;

void expect(const bool condition, const std::string& what)
{
    if (!condition) {
        throw std::runtime_error("Check failed: " + what);
    }
}

std::vector<std::uint8_t> from_hex(const std::string_view hex)
{
    std::vector<std::uint8_t> bytes;
    for (std::size_t index = 0; index + 1 < hex.size(); index += 2) {
        bytes.push_back(static_cast<std::uint8_t>(std::stoi(std::string(hex.substr(index, 2)), nullptr, 16)));
    }
    return bytes;
}

std::string to_hex(const std::span<const std::uint8_t> bytes)
{
    static constexpr char digits[] = "0123456789abcdef";
    std::string hex;
    for (const auto byte : bytes) {
        hex.push_back(digits[byte >> 4]);
        hex.push_back(digits[byte & 0x0F]);
    }
    return hex;
}

// Values from the fixture, encoded as payload bytes through host storage.
std::vector<std::uint8_t> payload_from_values(const SignalType type, const json::array& values)
{
    std::vector<std::uint8_t> payload;
    s7::visit_signal_type(type, [&]<SignalType T>() {
        std::vector<s7::signal_storage_t<T>> storage;
        for (const auto& value : values) {
            storage.push_back(s7::signal_value_from_json<T>(value, "fixture value"));
        }
        wire::encode_elements(T, storage.data(), storage.size(), payload);
    });
    return payload;
}

wire::TimestampRole role_from_name(const std::string_view name)
{
    if (name == "MODEL_TIME") return wire::TimestampRole::ModelTime;
    if (name == "SOURCE_TIME") return wire::TimestampRole::SourceTime;
    return wire::TimestampRole::ProcessingTime;
}

// Decodes a payload into storage and encodes it again: must reproduce the bytes.
void expect_payload_round_trip(const SignalType type, const std::vector<std::uint8_t>& payload, const std::string& name)
{
    const auto count = wire::payload_element_count(type, payload.size());
    s7::visit_signal_type(type, [&]<SignalType T>() {
        std::vector<s7::signal_storage_t<T>> storage(count);
        wire::decode_elements(T, payload, storage.data());
        std::vector<std::uint8_t> again;
        wire::encode_elements(T, storage.data(), storage.size(), again);
        expect(again == payload, name + ": payload decodes and re-encodes to the same bytes");
    });
}

void check_fixtures(const json::object& fixtures)
{
    for (const auto& entry : fixtures.at("data").as_array()) {
        const auto& c = entry.as_object();
        const std::string name = c.at("name").as_string().c_str();
        const auto type = s7::parse_signal_type(c.at("type").as_string());

        wire::DataPacket packet;
        packet.type = type;
        packet.stream_alias = c.at("stream_alias").to_number<std::uint32_t>();
        packet.sequence = c.at("sequence").to_number<std::uint64_t>();
        for (const auto& ts : c.at("timestamps").as_array()) {
            packet.timestamps.push_back({role_from_name(ts.at("role").as_string()), ts.at("ns").to_number<std::int64_t>()});
        }
        packet.payload = payload_from_values(type, c.at("values").as_array());
        if (const auto* sent = c.if_contains("send_time_ns"); sent && !sent->is_null()) {
            packet.send_time_ns = sent->to_number<std::int64_t>();
        }

        const auto encoded = wire::encode_data_packet(packet);
        expect(to_hex(encoded) == c.at("hex").as_string(), name + ": encoding is byte-exact (got " + to_hex(encoded) + ")");

        const auto decoded = wire::decode_data_packet(from_hex(c.at("hex").as_string()));
        expect(decoded.type == type && decoded.stream_alias == packet.stream_alias && decoded.sequence == packet.sequence,
               name + ": header fields decode");
        auto sorted = packet.timestamps;
        std::ranges::sort(sorted, {}, &wire::TimestampRecord::role);
        expect(decoded.timestamps == sorted, name + ": timestamps decode");
        expect(decoded.send_time_ns == packet.send_time_ns, name + ": send time decodes");
        expect(decoded.payload == packet.payload, name + ": payload decodes");
        expect_payload_round_trip(type, decoded.payload, name);
    }

    for (const auto& entry : fixtures.at("data_ignoring_extensions").as_array()) {
        const auto decoded = wire::decode_data_packet(from_hex(entry.at("hex").as_string()));
        expect(decoded.type == SignalType::F64 && decoded.payload.size() == 8, "unknown noncritical extension is ignored");
    }

    for (const auto& entry : fixtures.at("control").as_array()) {
        const auto& c = entry.as_object();
        const std::string name = c.at("name").as_string().c_str();
        wire::ControlMessage message;
        message.kind = static_cast<wire::ControlKind>(c.at("kind").to_number<int>());
        message.stream_alias = c.at("stream_alias").to_number<std::uint32_t>();
        message.next_expected = c.at("next_expected").to_number<std::uint64_t>();
        if (c.at("stream_id").is_string()) {
            message.stream_id = wire::parse_stream_id(c.at("stream_id").as_string().c_str());
        }
        const auto encoded = wire::encode_control_message(message);
        expect(to_hex(encoded) == c.at("hex").as_string(), name + ": control encoding is byte-exact");
        const auto bytes = from_hex(c.at("hex").as_string());
        expect(wire::is_control_message(bytes), name + ": recognized as control message");
        expect(wire::decode_control_message(bytes) == message, name + ": control message decodes");
    }

    for (const auto& entry : fixtures.at("invalid").as_array()) {
        const std::string name = entry.at("name").as_string().c_str();
        const auto bytes = from_hex(entry.at("hex").as_string());
        bool rejected = false;
        try {
            if (wire::is_control_message(bytes)) {
                static_cast<void>(wire::decode_control_message(bytes));
            } else {
                static_cast<void>(wire::decode_data_packet(bytes));
            }
        } catch (const wire::FormatError&) {
            rejected = true;
        }
        expect(rejected, name + " is rejected (" + std::string(entry.at("reason").as_string()) + ")");
    }
}

// Every type round-trips edge values through the codec.
void check_all_types_round_trip()
{
    for (const auto type : s7::kAllSignalTypes) {
        s7::visit_signal_type(type, [&]<SignalType T>() {
            using V = s7::signal_storage_t<T>;
            constexpr auto kind = s7::signal_type_info(T).kind;
            std::vector<V> values(5);
            if constexpr (kind == s7::SignalKind::SignedInteger || kind == s7::SignalKind::UnsignedInteger) {
                values = {s7::IntegerRange<T>::min, s7::IntegerRange<T>::max, V(0), V(1), V(s7::IntegerRange<T>::max / 3)};
            } else if constexpr (kind == s7::SignalKind::ComplexInteger || kind == s7::SignalKind::ComplexUnsigned) {
                using R = s7::IntegerRange<T>;
                values = {V{R::min, R::max}, V{R::max, R::min}, V{}, V{1, 0}, V{0, 1}};
            } else if constexpr (kind == s7::SignalKind::Float) {
                values = {V(0), V(-0.0), V(1.5), V(-1e4), std::numeric_limits<V>::infinity()};
            } else {
                using F = typename V::value_type;
                values = {V{F(1), F(-2)}, V{}, V{F(0.25), F(3)}, V{F(-0.0), F(8)}, V{std::numeric_limits<F>::max(), F(1)}};
            }
            wire::DataPacket packet;
            packet.type = T;
            packet.sequence = 9;
            wire::encode_elements(T, values.data(), values.size(), packet.payload);
            expect(packet.payload.size() == wire::payload_bytes(T, values.size()), "payload size of " + std::string(s7::signal_type_name(T)));
            const auto decoded = wire::decode_data_packet(wire::encode_data_packet(packet));
            std::vector<V> back(values.size());
            wire::decode_elements(T, decoded.payload, back.data());
            expect(std::memcmp(back.data(), values.data(), sizeof(V) * values.size()) == 0,
                   std::string(s7::signal_type_name(T)) + " values round-trip bit-exactly");
        });
    }
}

} // namespace

int main()
try {
    std::ifstream in(S7_WIRE_FIXTURES);
    expect(static_cast<bool>(in), "fixture file is readable");
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    boost::json::parse_options options;
    options.numbers = boost::json::number_precision::precise;
    check_fixtures(boost::json::parse(text, {}, options).as_object());
    check_all_types_round_trip();
    return 0;
} catch (const std::exception& ex) {
    std::cerr << "s7transport wire format smoke: " << ex.what() << '\n';
    return 1;
}
