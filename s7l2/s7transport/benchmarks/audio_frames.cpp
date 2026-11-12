// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: LGPL-3.0-or-later
#include <s7/frame_transport.hpp>
#include <boost/json.hpp>
#include <cmath>
#include <cstring>
#include <iostream>
#include <random>

namespace f = s7::frames;
using Clock = s7::stream::Clock;
namespace {
struct Options {
    std::string codec{"raw"}, type{"F32"}, pattern{"tone"};
    std::uint32_t frames{128}, channels{2}, blocks{400}, rate{48000};
};
Options options(int argc, char** argv) {
    Options o;
    for (int i = 1; i < argc; ++i) {
        const std::string key = argv[i];
        if (key == "--help") {
            std::cout << "audio_frames [--codec raw|lz4|zstd] [--type I16|F32] [--pattern silence|tone|noise|impulse] "
                         "[--frames N] [--channels N] [--blocks N] [--rate N]\n"; std::exit(0);
        }
        if (++i == argc) throw std::invalid_argument("missing option value");
        const std::string value = argv[i];
        if (key == "--codec") o.codec = value;
        else if (key == "--type") o.type = value;
        else if (key == "--pattern") o.pattern = value;
        else {
            if (value.empty() || value.find_first_not_of("0123456789") != std::string::npos) throw std::invalid_argument("invalid number");
            const auto n = std::stoull(value); if (!n || n > 1000000) throw std::invalid_argument("number outside benchmark bounds");
            if (key == "--frames") o.frames = n;
            else if (key == "--channels") o.channels = n;
            else if (key == "--blocks") o.blocks = n;
            else if (key == "--rate") o.rate = n;
            else throw std::invalid_argument("unknown option");
        }
    }
    if (o.codec != "raw" && o.codec != "lz4" && o.codec != "zstd") throw std::invalid_argument("unknown codec");
    if (o.type != "I16" && o.type != "F32") throw std::invalid_argument("unknown type");
    if (o.channels > 64) throw std::invalid_argument("channel count exceeds frame profile limit");
    if (o.pattern != "silence" && o.pattern != "tone" && o.pattern != "noise" && o.pattern != "impulse") throw std::invalid_argument("unknown pattern");
    return o;
}
boost::json::object histogram(const s7::stream::LatencyHistogram& hist) {
    const auto s = hist.stats();
    return {{"samples", s.samples}, {"mean_ns", s.mean_ns}, {"p50_upper_ns", s.p50_ns}, {"p99_upper_ns", s.p99_ns}, {"max_ns", s.max_ns}};
}
template<class T> boost::json::object run(const Options& o) {
    f::Descriptor d; d.type = std::is_same_v<T, float> ? s7::SignalType::F32 : s7::SignalType::I16;
    d.encoding = o.codec == "raw" ? f::Encoding::Raw : o.codec == "lz4" ? f::Encoding::Lz4 : f::Encoding::Zstd;
    d.channels.clear(); for (unsigned i = 0; i < o.channels; ++i) d.channels.push_back(std::to_string(i));
    d.frame_rate = {o.rate, 1}; d.max_frames_per_packet = o.frames; d.window_packets = 2; d.window_frames = o.frames * 2ull; d.validate();
    auto budget = std::make_shared<f::BufferBudget>(64 * 1024 * 1024);
    auto send = std::make_shared<f::BufferPool>(2, f::kMaxMessageBytes, budget);
    auto receive = std::make_shared<f::BufferPool>(2, s7::wire::kMaxPayloadBytes, budget);
    s7::stream::MemorySequenceStore seq; f::FrameSink sink(d, seq, send); f::FrameSource source(d, receive); sink.resume("audio", 0);
    std::vector<T> input(std::size_t(o.frames) * o.channels), output(input.size());
    std::mt19937 random(713); std::uint64_t wire_bytes{};
    s7::stream::LatencyHistogram publish, decode;
    const auto reserved = budget->reserved_bytes(); const auto start = Clock::now();
    for (std::uint32_t block = 0; block < o.blocks; ++block) {
        for (std::size_t i = 0; i < input.size(); ++i) {
            const auto frame = std::uint64_t(block) * o.frames + i / o.channels;
            double v{};
            if (o.pattern == "tone") v = 0.75 * std::sin(2 * 3.141592653589793 * 1000 * double(frame) / o.rate);
            else if (o.pattern == "noise") v = (int(random() & 65535) - 32768) / 32768.0;
            else if (o.pattern == "impulse") v = frame % o.rate == 0 ? 0.75 : 0;
            if constexpr (std::is_same_v<T, float>) input[i] = float(v);
            else input[i] = static_cast<T>(v * 32767);
        }
        auto t = Clock::now(); const auto p = sink.publish_copy(input.data(), o.frames);
        publish.add(std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - t).count(), {});
        if (p.frames != o.frames || p.status != f::Status::Ok) throw std::runtime_error("audio publication failed");
        auto message = sink.next_message("audio"); wire_bytes += message.bytes().size();
        t = Clock::now();
        if (!source.on_message(message.bytes())) throw std::runtime_error("audio admission failed");
        const auto r = source.receive_copy(output.data(), o.frames);
        decode.add(std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - t).count(), {});
        if (r.frames != o.frames || r.status != f::Status::Ok || std::memcmp(input.data(), output.data(), input.size() * sizeof(T)))
            throw std::runtime_error("audio bitwise mismatch");
        sink.acknowledge("audio", source.consumed_up_to());
        if (budget->reserved_bytes() != reserved || sink.log_depth()) throw std::runtime_error("audio resource accounting changed");
    }
    const auto seconds = std::chrono::duration<double>(Clock::now() - start).count();
    const auto raw_bytes = std::uint64_t(o.frames) * o.channels * o.blocks * sizeof(T);
    return {{"status", "verified"}, {"codec", o.codec}, {"type", o.type}, {"pattern", o.pattern}, {"channels", o.channels},
        {"frame_rate", o.rate}, {"frames_per_block", o.frames}, {"blocks", o.blocks}, {"block_duration_ms", o.frames * 1000.0 / o.rate},
        {"raw_bytes", raw_bytes}, {"wire_bytes_including_headers", wire_bytes}, {"wire_to_raw_ratio", double(wire_bytes) / raw_bytes},
        {"wall_seconds", seconds}, {"payload_MB_s", raw_bytes / seconds / 1e6}, {"reserved_buffer_workspace_bytes", reserved},
        {"publish_call_time", histogram(publish)}, {"admit_decode_copy_time", histogram(decode)},
        {"paced", false}, {"hardware_audio_measured", false}, {"graph_execution_measured", false}, {"sustained_rate_qualified", false}};
}
}
int main(int argc, char** argv) {
    try { const auto o = options(argc, argv); std::cout << boost::json::serialize(o.type == "F32" ? run<float>(o) : run<std::int16_t>(o)) << '\n'; }
    catch (const std::exception& e) { std::cerr << boost::json::serialize(boost::json::object{{"status", "failed"}, {"error", e.what()}}) << '\n'; return 1; }
}
