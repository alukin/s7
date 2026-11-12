// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: LGPL-3.0-or-later
#include <s7/frame_transport.hpp>
#include <boost/json.hpp>
#include <algorithm>
#include <iostream>

namespace f = s7::frames;
namespace json = boost::json;
namespace {
std::string string(const json::object& c, const char* name) { return std::string(c.at(name).as_string()); }
std::uint64_t number(const json::object& c, const char* name) {
    const auto& value = c.at(name);
    if (value.is_string()) return std::stoull(std::string(value.as_string()));
    return value.is_uint64() ? value.as_uint64() : static_cast<std::uint64_t>(value.as_int64());
}
std::vector<std::uint8_t> unhex(const std::string& s) {
    if (s.size() % 2) throw s7::wire::FormatError("odd hex length");
    std::vector<std::uint8_t> result;
    for (std::size_t i = 0; i < s.size(); i += 2) result.push_back(static_cast<std::uint8_t>(std::stoul(s.substr(i, 2), nullptr, 16)));
    return result;
}
std::string hex(std::span<const std::uint8_t> bytes) {
    constexpr char digits[] = "0123456789abcdef"; std::string result;
    for (auto b : bytes) { result += digits[b >> 4]; result += digits[b & 15]; } return result;
}
json::object run(const json::object& request) {
    const auto& c = request.at("case").as_object();
    f::Descriptor d; const auto type = s7::signal_type_from_name(string(c, "type"));
    if (!type) throw s7::wire::FormatError("unknown fixture type");
    d.type = *type; d.encoding = static_cast<f::Encoding>(number(c, "encoding"));
    d.channels.clear(); for (const auto& channel : c.at("channels").as_array()) d.channels.push_back(std::string(channel.as_string()));
    d.max_frames_per_packet = number(c, "max_frames"); d.window_packets = number(c, "window_packets"); d.window_frames = number(c, "window_frames");
    d.configuration_revision = number(c, "configuration_revision"); d.timing_revision = number(c, "timing_revision");
    if (!f::encoding_available(d.encoding)) return {{"status", "unavailable"}};
    f::Codec codec(d); f::Info expected;
    const auto& i = c.at("info").as_object(); expected.frames = number(i, "frames"); expected.segment = s7::wire::parse_stream_id(string(i, "segment"));
    expected.first_frame = number(i, "first_frame"); expected.configuration_revision = d.configuration_revision; expected.timing_revision = d.timing_revision;
    expected.segment_start = i.at("segment_start").as_bool(); expected.fault_only = i.at("fault_only").as_bool();
    for (const auto& value : i.at("events").as_array()) {
        const auto& e = value.as_object(); f::Event event; event.offset = number(e, "offset"); event.type = number(e, "type");
        const auto bytes = unhex(string(e, "value_hex"));
        if (bytes.size() != event.value.size() || expected.event_count >= f::kMaxEvents) throw s7::wire::FormatError("invalid fixture event dimensions");
        std::copy(bytes.begin(), bytes.end(), event.value.begin()); expected.events[expected.event_count++] = event;
    }
    const auto raw = unhex(string(c, "payload_hex"));
    const auto alias = static_cast<std::uint32_t>(number(c, "alias")); const auto sequence = number(c, "sequence");
    if (string(request, "op") == "encode") return {{"status", "ok"}, {"message_hex", hex(codec.encode(alias, sequence, expected, raw))}};
    f::BufferPool pool(1, s7::wire::kMaxPayloadBytes); const auto packet = codec.decode(unhex(string(c, "message_hex")), pool);
    const auto& actual = packet.info;
    if (packet.alias != alias || packet.sequence != sequence || actual.frames != expected.frames || actual.segment != expected.segment ||
        actual.first_frame != expected.first_frame || actual.configuration_revision != expected.configuration_revision ||
        actual.timing_revision != expected.timing_revision || actual.segment_start != expected.segment_start || actual.fault_only != expected.fault_only ||
        actual.event_count != expected.event_count || !std::equal(actual.events.begin(), actual.events.begin() + actual.event_count, expected.events.begin()) ||
        !std::ranges::equal(packet.payload.bytes(), raw)) throw s7::wire::FormatError("fixture fields differ");
    return {{"status", "ok"}};
}
}
int main() {
    for (std::string line; std::getline(std::cin, line);) {
        json::object result;
        try { result = run(json::parse(line).as_object()); }
        catch (const std::exception& e) { result = {{"status", "rejected"}, {"error", e.what()}}; }
        std::cout << json::serialize(result) << std::endl;
    }
}
