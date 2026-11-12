// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: LGPL-3.0-or-later
#include <s7/frame_transport.hpp>
#include <atomic>
#include <algorithm>
#include <bit>
#include <cstring>
#include <future>
#include <iostream>
#include <random>
#include <thread>

namespace f = s7::frames;
namespace w = s7::wire;
using s7::SignalType;
using namespace std::chrono_literals;
namespace {
unsigned checks{};
void check(bool value, const char* error) { ++checks; if (!value) throw std::runtime_error(error); }
template<class F> void rejects(F fn) {
    bool rejected{}; try { fn(); } catch (const std::exception&) { rejected = true; }
    check(rejected, "invalid input accepted");
}
auto pool(std::size_t slots = 4, bool send = true) { return std::make_shared<f::BufferPool>(slots, send ? f::kMaxMessageBytes : w::kMaxPayloadBytes); }
f::Descriptor descriptor(f::Encoding encoding = f::Encoding::Raw, SignalType type = SignalType::I16) {
    f::Descriptor d; d.type = type; d.encoding = encoding; d.channels = {"L", "R"};
    d.max_frames_per_packet = 8; d.window_packets = 4; d.window_frames = 32; return d;
}
std::vector<std::uint8_t> native_bytes(SignalType type, std::size_t values) {
    std::vector<std::uint8_t> out;
    s7::visit_signal_type(type, [&]<SignalType T>() {
        using V = s7::signal_storage_t<T>;
        std::vector<V> v(values);
        for (std::size_t i = 0; i < values; ++i) {
            if constexpr (T == SignalType::F32) v[i] = std::bit_cast<float>(std::uint32_t(0x7fc00001 + i));
            else if constexpr (T == SignalType::CI12) v[i] = {std::int16_t(i % 2048), std::int16_t(-int(i % 2048))};
            else if constexpr (T == SignalType::I16) v[i] = static_cast<V>(i * 17 - 70);
        }
        out.resize(v.size() * sizeof(V)); std::memcpy(out.data(), v.data(), out.size());
    }); return out;
}
void pools() {
    f::BufferPool p(1, 16); const std::array<std::uint8_t, 3> value{1, 2, 3};
    auto lease = p.copy(value); auto slice = lease.slice(1, 1); lease = {};
    check(p.leased_slots() == 1 && !p.copy(value), "retained slice did not retain budget");
    check(slice.bytes()[0] == 2, "slice bytes changed");
    rejects([&] { static_cast<void>(slice.slice(2, 1)); });
    auto released = std::async(std::launch::async, [&] { return p.wait_copy(value, s7::stream::Clock::now() + 2s); });
    slice = {}; check(bool(released.get()), "lease release did not wake waiter");
    auto held = p.copy(value); check(bool(held), "pool not reusable");
    std::stop_source stop;
    auto cancelled = std::async(std::launch::async, [&] { return p.wait_copy(value, s7::stream::Clock::now() + 2s, stop.get_token()); });
    stop.request_stop(); check(!cancelled.get(), "cancelled pool wait succeeded");
    auto closing = std::async(std::launch::async, [&] { return p.wait_copy(value, s7::stream::Clock::now() + 2s); });
    p.close(); check(!closing.get() && held.bytes()[2] == 3, "close did not wake/preserve lease");
    f::BufferPool::Lease survives;
    { f::BufferPool temporary(1, 16); survives = temporary.copy(value); }
    check(survives.bytes()[0] == 1, "lease lifetime depends on pool object");
    auto budget = std::make_shared<f::BufferBudget>(16);
    {
        f::BufferPool limited(1, 16, budget); survives = limited.copy(value);
        rejects([&] { f::BufferPool excess(1, 1, budget); });
    }
    check(budget->reserved_bytes() == 16, "pool reservation released with live lease");
    survives = {}; check(budget->reserved_bytes() == 0, "node budget leaked");
    rejects([&] { f::Codec excess(descriptor(), budget); });
    check(budget->reserved_bytes() == 0, "failed codec admission leaked budget");
}
void transfer(f::Encoding encoding, SignalType type) {
    auto d = descriptor(encoding, type); auto send = pool(), receive = pool(4, false);
    s7::stream::MemorySequenceStore seq;
    f::FrameSink sink(d, seq, send); f::FrameSource source(d, receive);
    sink.resume("a", 0); sink.resume("b", 0);
    auto input = native_bytes(type, 32), output = input; std::fill(output.begin(), output.end(), 0);
    std::array events{f::clock_anchor(3, -123), f::clock_anchor(10, 777)};
    auto result = sink.publish_copy(input.data(), 16, events);
    check(result.frames == 16 && result.status == f::Status::Ok, "publish failed");
    std::vector<std::vector<std::uint8_t>> sent;
    while (auto message = sink.next_message("a")) {
        sent.emplace_back(message.bytes().begin(), message.bytes().end());
        check(source.on_message(message.bytes()), "frame rejected");
    }
    check(source.next_expected() == 2 && source.consumed_up_to() == 0, "receipt acknowledged consumption");
    check(!source.on_message(sent[0]) && receive->leased_slots() == 2, "duplicate reallocated/accepted");
    sink.detach("a"); sink.resume("a", source.next_expected());
    check(!sink.next_message("a"), "resume replayed accepted packet");
    auto retained = source.front(); check(bool(retained), "no readable lease");
    auto first = source.receive_copy(output.data(), 3);
    check(first.frames == 3 && first.info.event_count == 0 && source.consumed_up_to() == 0, "partial read ACK/event error");
    const auto stride = d.channels.size() * s7::signal_type_info(type).storage_bytes;
    auto rest = source.receive_copy(output.data() + 3 * stride, 100);
    check(rest.frames == 5 && rest.info.events[0].offset == 0 && rest.info.first_frame == 3, "event slicing failed");
    auto second = source.receive_copy(output.data() + 8 * stride, 100);
    check(second.frames == 8 && second.info.events[0].offset == 2 && input == output, "bitwise round trip failed");
    sink.acknowledge("a", source.consumed_up_to()); check(sink.log_depth() == 2, "slow peer data evicted");
    sink.detach("b"); sink.resume("b", 0);
    std::size_t index{};
    while (auto replay = sink.next_message("b")) {
        check(std::ranges::equal(replay.bytes(), sent[index++]), "replay changed sealed bytes");
    }
    sink.acknowledge("b", 2); check(sink.log_depth() == 0, "ACK did not trim log");
    check(receive->leased_slots() == 1, "external lease uncharged after consumption");
    retained = {}; check(receive->leased_slots() == 0, "lease not released");
    std::stop_source stop; stop.request_stop();
    check(sink.publish_copy(input.data(), 1, {}, stop.get_token()).status == f::Status::Cancelled, "cancelled publish admitted data");
}
void faults(f::Encoding encoding) {
    auto d = descriptor(encoding); d.window_packets = 1; d.window_frames = 8;
    auto send = pool(1), receive = pool(1, false); s7::stream::MemorySequenceStore seq;
    f::FrameSink sink(d, seq, send); f::FrameSource source(d, receive); sink.resume("a", 0);
    auto input = native_bytes(d.type, 32);
    const auto p = sink.publish_copy(input.data(), 16);
    check(p.frames == 8 && p.status == f::Status::Backpressure, "partial publication incorrect");
    sink.fault(f::Status::SourceOverflow); check(sink.log_depth() == 2 && sink.state() == f::SourceState::DrainingFault, "no reserved terminal slot");
    while (auto message = sink.next_message("a")) check(source.on_message(message.bytes()), "valid prefix/terminal rejected");
    check(source.status() == f::Status::Ok, "terminal fault preceded valid prefix");
    auto r = source.receive_copy(input.data(), 20);
    check(r.frames == 8 && r.status == f::Status::SourceOverflow && source.consumed_up_to() == 2, "read did not return prefix with terminal");
    sink.acknowledge("a", 2); check(sink.state() == f::SourceState::Faulted, "terminal ACK did not finish drain");
    auto segment = w::random_stream_id(); sink.recover(segment); source.recover(2, segment);
    check(sink.recent_faults().size() == 1 && source.recent_faults().size() == 1 && !source.recent_faults()[0].lost_frames,
          "recovery erased or invented loss history");
    check(sink.publish_copy(input.data(), 1).frames == 1, "explicit recovery did not restart");
    auto message = sink.next_message("a"); check(source.on_message(message.bytes()), "new authorized segment rejected");
    auto after = source.receive_copy(input.data(), 1);
    check(after.info.first_frame == 0 && after.info.segment == segment && after.status == f::Status::Ok, "recovery reused old grid");
    source.sink_underflow(11); const auto ack = source.consumed_up_to();
    const auto underflow = source.receive_copy(input.data(), 1);
    check(underflow.status == f::Status::SinkUnderflow && underflow.lost_frames == 11 && source.consumed_up_to() == ack, "underflow advanced ACK/lost count");
    source.close(); sink.close(); check(source.status() == f::Status::Closed && sink.publish_copy(input.data(), 1).status == f::Status::Closed, "close accepted data");
    // Source loss before the first sample is still an ordered, consumable fault.
    d.stream_id = w::random_stream_id(); f::FrameSink empty(d, seq, pool(1)); f::FrameSource target(d, pool(1, false));
    empty.resume("a", 0); empty.fault(f::Status::ContinuityLost, 17);
    auto marker = empty.next_message("a"); check(target.on_message(marker.bytes()), "initial terminal rejected");
    const auto initial = target.consume(0);
    check(initial.status == f::Status::ContinuityLost && initial.lost_frames == 17, "initial fault/count not visible");
}
void malformed(f::Encoding encoding) {
    auto d = descriptor(encoding); f::Codec codec(d); auto p = pool(4, false);
    f::Info info; info.frames = 8; info.segment_start = true; info.segment = w::random_stream_id();
    const std::vector<std::uint8_t> raw(32, 0);
    auto view = codec.encode(1, 0, info, raw); std::vector<std::uint8_t> valid(view.begin(), view.end());
    check(codec.decode(valid, *p).payload.bytes().size() == 32, "codec round trip");
    rejects([&] { static_cast<void>(w::decode_data_packet(valid)); });
    for (std::size_t n = 0; n < valid.size(); ++n) rejects([&] { static_cast<void>(codec.decode(std::span(valid.data(), n), *p)); });
    for (auto offset : {0u, 1u, 2u, 16u, 18u, 20u, 22u, 24u, 66u, 70u}) {
        auto damaged = valid; damaged[offset] ^= 0x80;
        rejects([&] { static_cast<void>(codec.decode(damaged, *p)); });
    }
    if (encoding != f::Encoding::Raw) {
        auto damaged = valid; damaged.back() ^= 1;
        rejects([&] { static_cast<void>(codec.decode(damaged, *p)); });
    }
    auto trailer = valid; trailer.push_back(0); rejects([&] { static_cast<void>(codec.decode(trailer, *p)); });
    rejects([&] { static_cast<void>(codec.decode(valid, *p, 7)); });
    check(p->leased_slots() == 0, "malformed decode retained allocation");
    f::FrameSource source(d, p);
    auto gap = valid; gap[8] = 1;
    check(!source.on_message(gap) && source.status() == f::Status::SequenceGap && source.next_expected() == 0, "gap mutated sequence");
    f::FrameSource channel(d, pool(4, false));
    auto bad = valid; bad[4] = 2;
    check(!channel.on_message(bad) && channel.status() == f::Status::Malformed, "alias mismatch admitted");
    f::FrameSource duplicate_header(d, pool(4, false));
    check(duplicate_header.on_message(valid), "duplicate header setup failed");
    auto wrong_version = valid; wrong_version[0] = 2;
    check(!duplicate_header.on_message(wrong_version) && duplicate_header.status() == f::Status::Malformed && duplicate_header.next_expected() == 1,
          "duplicate sequence bypassed header validation");
    // Noise-like bytes exercise raw fallback and preserve arbitrary bit patterns.
    std::vector<std::uint8_t> noise(32); std::mt19937 gen(777);
    for (auto& b : noise) b = static_cast<std::uint8_t>(gen());
    auto noisy = codec.encode(1, 0, info, noise);
    check(std::ranges::equal(codec.decode(noisy, *p).payload.bytes(), noise), "raw fallback changed bits");
}
void ownership_and_resume() {
    auto d = descriptor(); auto send = pool(); auto receive = pool(4, false);
    auto budget = std::make_shared<f::BufferBudget>(64 * 1024 * 1024);
    f::BufferPool input_pool(1, 32, budget);
    std::array<std::uint8_t, 32> bytes{}; auto input = input_pool.copy(bytes);
    s7::stream::MemorySequenceStore sequences;
    f::FrameSink sink(d, sequences, send); f::FrameSource source(d, receive); sink.resume("a", 0);
    check(sink.publish_shared(input, 8).frames == 8, "shared block rejected"); input = {};
    check(input_pool.leased_slots() == 1, "published shared allocation released before replay ACK");
    auto message = sink.next_message("a"); check(source.on_message(message.bytes()), "shared packet rejected"); message = {};
    sink.detach("a"); sink.resume("a", source.next_expected());
    check(sink.log_depth() == 1 && input_pool.leased_slots() == 1, "RESUME incorrectly acknowledged receipt");
    check(source.consume(8).frames == 8, "shared input consume failed");
    sink.acknowledge("a", source.consumed_up_to());
    check(input_pool.leased_slots() == 0 && sink.log_depth() == 0, "ACK did not release shared storage");
    // Retained user leases keep the receiver's physical storage bounded even
    // after logical consumption frees its frame window.
    auto tiny = descriptor(); tiny.window_packets = 1; tiny.window_frames = 8;
    f::Codec codec(tiny); f::Info info; info.segment = w::random_stream_id(); info.segment_start = true; info.frames = 8;
    auto rx = pool(1, false); f::FrameSource slow(tiny, rx);
    check(slow.on_message(codec.encode(1, 0, info, bytes)), "retention setup failed");
    auto retained = slow.front(); check(slow.consume(8).frames == 8, "retained frame consumption"); info.segment_start = false; info.first_frame = 8;
    check(!slow.on_message(codec.encode(1, 1, info, bytes)) && slow.next_expected() == 1 && rx->leased_slots() == 1,
          "retained receive storage bypassed quota");
    check(retained.bytes()[0] == 0, "failed admission corrupted retained lease");
}
void wire_boundaries(f::Encoding encoding) {
    auto d = descriptor(encoding); f::Codec codec(d); auto p = pool(4, false);
    f::Info info; info.frames = 1; info.segment_start = true; info.segment[0] = 1;
    const std::array<std::uint8_t, 4> value{1, 0, 2, 0};
    if (encoding == f::Encoding::Raw) {
        std::array<std::uint8_t, 84> golden{};
        golden[0] = 1; golden[1] = 1; golden[2] = 5; golden[4] = 1;
        golden[16] = 60; golden[18] = 2; golden[19] = 128; golden[20] = 56;
        golden[22] = 1; golden[24] = 1; golden[26] = 1; golden[30] = 1;
        golden[80] = 1; golden[82] = 2;
        check(std::ranges::equal(codec.encode(1, 0, info, value), golden), "raw profile differs from golden wire bytes");
    }
    info.event_count = 2; info.events[0] = f::clock_anchor(0, 1); info.events[1] = info.events[0];
    rejects([&] { static_cast<void>(codec.encode(1, 0, info, value)); });
    info.event_count = 1; info.events[0].type = 0x8003;
    rejects([&] { static_cast<void>(codec.encode(1, 0, info, value)); });
    info.event_count = 0; info.first_frame = UINT64_MAX; info.segment_start = false;
    rejects([&] { static_cast<void>(codec.encode(1, 0, info, value)); });
    d.max_frames_per_packet = w::kMaxPayloadBytes / d.frame_bytes(); d.window_frames = d.max_frames_per_packet * 4ull;
    f::Codec maximum(d); info.first_frame = 0; info.segment_start = true; info.frames = d.max_frames_per_packet;
    std::vector<std::uint8_t> largest(w::kMaxPayloadBytes);
    auto message = maximum.encode(1, 0, info, largest);
    check(maximum.decode(message, *p).payload.bytes().size() == w::kMaxPayloadBytes, "exact global bound failed");
    largest.push_back(0); rejects([&] { static_cast<void>(maximum.encode(1, 0, info, largest)); });
    for (auto type : s7::kAllSignalTypes) {
        if (type == SignalType::I12 || type == SignalType::U12) continue;
        auto typed = descriptor(encoding, type); f::Codec c(typed);
        info.frames = 8;
        std::vector<std::uint8_t> payload(info.frames * typed.frame_bytes());
        std::mt19937 random(573); for (auto& b : payload) b = static_cast<std::uint8_t>(random());
        const auto encoded = c.encode(1, 0, info, payload);
        check(std::ranges::equal(c.decode(encoded, *p).payload.bytes(), payload), "native type byte round trip failed");
    }
}
void concurrent_transfer() {
    auto d = descriptor(); s7::stream::MemorySequenceStore sequences;
    f::FrameSink sink(d, sequences, pool()); f::FrameSource source(d, pool(4, false)); sink.resume("a", 0);
    std::array<std::int16_t, 16> input{};
    for (int i = 0; i < 16; ++i) input[i] = i;
    const auto deadline = s7::stream::Clock::now() + 5s;
    auto writer = std::async(std::launch::async, [&] {
        for (int i = 0; i < 256; ++i) {
            while (sink.publish_copy(input.data(), 8).frames != 8) {
                if (s7::stream::Clock::now() > deadline) return false;
                std::this_thread::yield();
            }
        }
        return true;
    });
    for (int i = 0; i < 256; ++i) {
        f::BufferPool::Lease message;
        while (!(message = sink.next_message("a"))) {
            if (s7::stream::Clock::now() > deadline) throw std::runtime_error("concurrent transfer timeout");
            std::this_thread::yield();
        }
        check(source.on_message(message.bytes()), "concurrent admission failed");
        std::array<std::int16_t, 16> output{};
        const auto r = source.receive_copy(output.data(), 8);
        check(r.frames == 8 && output == input && r.info.first_frame == std::uint64_t(i) * 8, "concurrent sample/order mismatch");
        sink.acknowledge("a", source.consumed_up_to());
    }
    check(writer.get() && sink.log_depth() == 0, "concurrent writer failed");
}
} // namespace
int main() {
    try {
        pools(); ownership_and_resume(); concurrent_transfer();
        for (auto encoding : {f::Encoding::Raw, f::Encoding::Lz4, f::Encoding::Zstd}) {
            if (!f::encoding_available(encoding)) { std::cout << "codec " << int(encoding) << " unavailable\n"; continue; }
            for (auto type : {SignalType::I16, SignalType::F32, SignalType::CI12}) transfer(encoding, type);
            faults(encoding); malformed(encoding); wire_boundaries(encoding);
        }
        auto d = descriptor(); d.channels = {"x", "x"}; rejects([&] { d.validate(); });
        d = descriptor(); d.type = SignalType::I12; rejects([&] { d.validate(); });
        d = descriptor(); d.max_frames_per_packet = UINT32_MAX; rejects([&] { d.validate(); });
        std::cout << checks << " frame transport checks passed\n";
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
