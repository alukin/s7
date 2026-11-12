// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: LGPL-3.0-or-later

#include <s7/transport.hpp>
#include <s7/net.hpp>
#include <boost/json.hpp>
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>
#if defined(__unix__) || defined(__APPLE__)
#include <sys/resource.h>
#endif

namespace {
namespace wire = s7::wire;
namespace stream = s7::stream;
namespace net = s7::transport;
using Clock = std::chrono::steady_clock;
struct Options {
    std::string mode = "codec", type = "CI16", cert_dir, execution = "bulk", direction = "forward";
    std::size_t packet_bytes = 32768, packets = 256, channels = 1, chunk = 0, rate = 0, queue_period = 0, corrupt_frame = 0;
};

std::size_t number(const std::string& value, std::uint64_t maximum = 10000000) {
    if (value.empty() || value.find_first_not_of("0123456789") != std::string::npos)
        throw std::invalid_argument("Expected a positive integer: " + value);
    const auto n = std::stoull(value);
    if (!n || n > maximum) throw std::invalid_argument("Integer out of benchmark bounds");
    return static_cast<std::size_t>(n);
}

Options options(int argc, char** argv) {
    Options o;
    for (int i = 1; i < argc; ++i) {
        const std::string key = argv[i];
        if (key == "--help") {
            std::cout << "s7transport_sample_benchmark [--mode codec|protocol|memory|ws|tls] "
                         "[--type CI12|CI16|C32] [--packet-bytes N] [--packets N] "
                         "[--channels 1..16] [--chunk-elements N] [--cert-dir PATH] "
                         "[--execution bulk|graph] [--direction forward|reverse|duplex] [--rate-frames N] "
                         "[--queue-sample-every-blocks N] [--corrupt-frame N]\n";
            std::exit(0);
        }
        if (++i == argc) throw std::invalid_argument("Missing option value: " + key);
        const std::string value = argv[i];
        if (key == "--mode") o.mode = value;
        else if (key == "--type") o.type = value;
        else if (key == "--cert-dir") o.cert_dir = value;
        else if (key == "--packet-bytes") o.packet_bytes = number(value);
        else if (key == "--packets") o.packets = number(value);
        else if (key == "--channels") o.channels = number(value);
        else if (key == "--chunk-elements") o.chunk = number(value);
        else if (key == "--execution") o.execution = value;
        else if (key == "--direction") o.direction = value;
        else if (key == "--rate-frames") o.rate = number(value, 1000000000);
        else if (key == "--queue-sample-every-blocks") o.queue_period = number(value);
        else if (key == "--corrupt-frame") o.corrupt_frame = number(value);
        else throw std::invalid_argument("Unknown option: " + key);
    }
    if (o.channels > 16 || o.packet_bytes > wire::kMaxPayloadBytes)
        throw std::invalid_argument("Channel count or payload exceeds benchmark bounds");
    if (o.mode != "codec" && o.mode != "protocol" && o.mode != "memory" && o.mode != "ws" && o.mode != "tls")
        throw std::invalid_argument("Unknown benchmark mode");
    if (o.type != "CI12" && o.type != "CI16" && o.type != "C32")
        throw std::invalid_argument("Unsupported benchmark type");
    if (o.mode == "tls" && o.cert_dir.empty())
        throw std::invalid_argument("TLS mode requires --cert-dir (repository test certificates)");
    if (o.execution != "bulk" && o.execution != "graph") throw std::invalid_argument("Unknown execution path");
    if (o.direction != "forward" && o.direction != "reverse" && o.direction != "duplex")
        throw std::invalid_argument("Unknown data direction");
    if ((o.mode == "codec" || o.mode == "protocol") &&
        (o.execution != "bulk" || o.direction != "forward" || o.rate || o.queue_period))
        throw std::invalid_argument("Execution, direction and pacing require endpoint modes");
    if (o.execution == "graph" && (o.chunk || o.rate || o.queue_period))
        throw std::invalid_argument("Graph path is scalar and unpaced; chunk/pacing/queue sampling are bulk-only options");
    return o;
}

template <s7::SignalType Type>
auto sample(std::uint64_t index, std::size_t channel) -> s7::signal_storage_t<Type> {
    const auto re = static_cast<std::int16_t>((index * 17 + channel * 257) % 4096) - 2048;
    const auto im = static_cast<std::int16_t>((index * 31 + channel * 131 + 7) % 4096) - 2048;
    if constexpr (Type == s7::SignalType::C32) return {float(re) / 2048, float(im) / 2048};
    else return {static_cast<std::int16_t>(re), static_cast<std::int16_t>(im)};
}

template <s7::SignalType Type>
void fill(std::vector<s7::signal_storage_t<Type>>& data, std::uint64_t first, std::size_t channel, std::size_t corrupt_frame = 0) {
    for (std::size_t i = 0; i < data.size(); ++i) data[i] = sample<Type>(first + i, channel);
    if (corrupt_frame && channel == 0 && first <= corrupt_frame && corrupt_frame - first < data.size())
        data[corrupt_frame - first] = sample<Type>(corrupt_frame + 1, channel);
}

template <s7::SignalType Type>
void verify(const std::vector<s7::signal_storage_t<Type>>& data, std::size_t count,
            std::uint64_t first, std::size_t channel) {
    for (std::size_t i = 0; i < count; ++i)
        if (data[i] != sample<Type>(first + i, channel))
            throw std::runtime_error("Sample/order mismatch on channel " + std::to_string(channel) +
                                     " at element " + std::to_string(first + i));
}

stream::StreamConfig configuration(s7::SignalType type, std::size_t frames, std::size_t channel, std::size_t rate = 1000000) {
    stream::StreamConfig c;
    c.stream_id = wire::parse_stream_id("0cb33d3a-07cf-47a2-a727-c83559a16a00");
    c.stream_id.back() = static_cast<std::uint8_t>(channel);
    c.type = type;
    c.max_elements_per_packet = static_cast<std::uint32_t>(frames);
    c.send_log_max_packets = 128;
    c.max_block = c.max_wait = std::chrono::seconds(5);
    c.max_wait += std::chrono::milliseconds((std::uint64_t(frames) * 1000 + rate - 1) / rate);
    // A deterministic synthetic model grid, not a device/UTC timestamp.
    c.sample_rate = stream::SampleRate{rate, 1};
    c.timestamp_roles = {wire::TimestampRole::ModelTime};
    return c;
}

net::TlsConfig tls(const Options& o) {
    net::TlsConfig t;
    t.enabled = t.require_client_cert = true;
    const std::filesystem::path root(o.cert_dir);
    t.cert_file = (root / "s7_runtime_node.cert.pem").string();
    t.key_file = (root / "s7_runtime_node.key.pem").string();
    t.ca_file = (root / "s7_root_ca.cert.pem").string();
    return t;
}

template <s7::SignalType Type>
void local(const Options& o, std::size_t frames) {
    using T = s7::signal_storage_t<Type>;
    std::vector<T> input(frames), output(frames);
    std::vector<std::unique_ptr<stream::StreamProducer>> producers;
    std::vector<std::unique_ptr<stream::StreamConsumer>> consumers;
    stream::MemorySequenceStore sequences;
    if (o.mode == "protocol") for (std::size_t c = 0; c < o.channels; ++c) {
        producers.push_back(std::make_unique<stream::StreamProducer>(configuration(Type, frames, c), sequences));
        consumers.push_back(std::make_unique<stream::StreamConsumer>(configuration(Type, frames, c), 1));
        producers.back()->set_stamp_send_time(false);
        producers.back()->resume("benchmark", 1, 0);
    }
    for (std::size_t p = 0; p < o.packets; ++p) for (std::size_t c = 0; c < o.channels; ++c) {
        const auto first = std::uint64_t(p) * frames;
        fill<Type>(input, first, c, o.corrupt_frame);
        wire::DataPacket packet;
        if (o.mode == "codec") {
            packet.type = Type; packet.sequence = p; packet.stream_alias = static_cast<std::uint32_t>(c + 1);
            wire::encode_elements(Type, input.data(), frames, packet.payload);
            packet = wire::decode_data_packet(wire::encode_data_packet(packet));
            if (packet.sequence != p || packet.stream_alias != c + 1 ||
                wire::payload_element_count(Type, packet.payload.size()) != frames)
                throw std::runtime_error("Header/count mismatch");
            wire::decode_elements(Type, packet.payload, output.data());
        } else {
            auto& producer = *producers[c]; auto& consumer = *consumers[c];
            const auto now = Clock::now();
            if (producer.append(input.data(), frames, now) != frames) throw std::runtime_error("Unexpected backpressure");
            const auto bytes = producer.next_message("benchmark");
            if (!bytes) throw std::runtime_error("No sealed packet");
            consumer.on_packet(wire::decode_data_packet(*bytes), now);
            if (consumer.fault_code() != stream::FaultCode::None || consumer.take(output.data(), frames) != frames)
                throw std::runtime_error("Delivery fault");
            if (const auto ack = consumer.ack_due(now)) producer.acknowledge("benchmark", ack->next_expected);
        }
        verify<Type>(output, frames, first, c);
    }
}

// Benchmark-only elements use the ordinary scalar runtime, including publication
// and state handling. No processing shortcut or alternative scheduler is used.
template <s7::SignalType Type, bool Source>
class SampleElement final : public s7::Element {
    s7::signal_storage_t<Type> value_{};
    std::uint64_t index_ = 0;
    std::size_t channel_;
    std::size_t corrupt_frame_;
public:
    explicit SampleElement(std::size_t channel, std::size_t corrupt_frame = 0)
        : Element(Source ? "BenchmarkSource" : "BenchmarkCheck", true), channel_(channel), corrupt_frame_(corrupt_frame) {
        if constexpr (Source) register_output(0, "out", Type, &value_);
        else register_input(0, "in", Type, nullptr);
    }
    void step() override {
        if constexpr (Source) value_ = sample<Type>(index_ + (channel_ == 0 && corrupt_frame_ && index_ == corrupt_frame_), channel_);
        else if (read_input<s7::signal_storage_t<Type>>(0) != sample<Type>(index_, channel_))
            throw std::runtime_error("Graph sample/order mismatch at " + std::to_string(index_));
        ++index_;
    }
};

template <s7::SignalType Type>
std::unique_ptr<s7::NodeRuntime> graph(const std::vector<stream::StreamConfig>& configs, bool source, std::size_t first_channel, std::size_t corrupt_frame) {
    s7::Schema schema(source ? "benchmark_sender" : "benchmark_receiver");
    const auto rate = s7::Rate::reduced(configs.front().sample_rate->numerator, 1);
    schema.set_rate(rate);
    for (std::size_t c = 0; c < configs.size(); ++c) {
        const auto sample_id = "sample" + std::to_string(c), net_id = "net" + std::to_string(c);
        const auto config = configs[c];
        const auto channel = first_channel + c;
        if (source) {
            schema.add_element(sample_id, [channel, corrupt_frame] { return std::make_unique<SampleElement<Type, true>>(channel, corrupt_frame); });
            schema.add_element(net_id, [config] { return std::make_unique<s7::NetSinkT<Type>>(config); });
            schema.connect(sample_id, 0, net_id, 0);
        } else {
            schema.add_element(net_id, [config] { return std::make_unique<s7::NetSourceT<Type>>(config); });
            schema.add_element(sample_id, [channel] { return std::make_unique<SampleElement<Type, false>>(channel); });
            schema.connect(net_id, 0, sample_id, 0);
        }
    }
    auto runtime = std::make_unique<s7::NodeRuntime>(s7::Compiler::compile(schema));
    runtime->set_model_time_rate(rate);
    return runtime;
}

struct Flow {
    net::RuntimeNetEndpointPtr source, target;
    std::vector<stream::StreamConfig> configs;
    std::vector<net::StreamHandle> producers, consumers;
    std::unique_ptr<s7::NodeRuntime> sending, receiving;
    std::vector<std::string> errors;
    std::vector<stream::LatencyHistogram> latency;
    std::size_t first_channel = 0, late_blocks = 0;
    std::int64_t max_lateness_ns = 0;
    std::size_t queue_samples = 0, observed_log_packets = 0, observed_buffered_values = 0;
};

boost::json::object latency_json(const stream::LatencyStats& s) {
    return {{"samples", s.samples}, {"min_ns", s.min_ns}, {"mean_ns", s.mean_ns},
            {"p50_upper_ns", s.p50_ns}, {"p99_upper_ns", s.p99_ns}, {"max_ns", s.max_ns}};
}

template <s7::SignalType Type>
boost::json::array endpoint(const Options& o, std::size_t frames) {
    using T = s7::signal_storage_t<Type>;
    const auto setup_start = Clock::now();
    net::RuntimeNetEndpointPtr left, right;
    if (o.mode == "memory") left = right = std::make_shared<net::InMemoryRuntimeNetEndpoint>();
    else {
        auto server = std::make_shared<net::BeastRuntimeNetEndpoint>();
        auto client = std::make_shared<net::BeastRuntimeNetEndpoint>();
        const auto security = o.mode == "tls" ? tls(o) : net::TlsConfig{};
        const auto port = server->configure_server(0, security, "127.0.0.1");
        client->configure_client(std::string(o.mode == "tls" ? "wss" : "ws") + "://127.0.0.1:" + std::to_string(port), security);
        left = server; right = client;
    }
    left->set_clock_usable(false); right->set_clock_usable(false);
    const auto chunk = o.chunk ? o.chunk : frames;
    if (chunk > frames) throw std::invalid_argument("Chunk cannot exceed packet element count");
    const auto directions = o.direction == "duplex" ? 2u : 1u;
    std::vector<Flow> flows(directions);
    // RuntimeNetContext is process-global. Prime all lazy graph bindings serially
    // before starting workers, then restore it. Workers only use cached endpoints.
    struct ContextRestore {
        net::RuntimeNetEndpointPtr previous = s7::RuntimeNetContext::endpoint();
        ~ContextRestore() { s7::RuntimeNetContext::set_endpoint(previous); }
    } context;
    for (std::size_t d = 0; d < directions; ++d) {
        auto& f = flows[d];
        const bool reverse = o.direction == "reverse" || d == 1;
        f.source = reverse ? right : left; f.target = reverse ? left : right;
        f.first_channel = d * o.channels;
        f.errors.resize(o.channels + 1); f.latency.resize(o.channels);
        for (std::size_t c = 0; c < o.channels; ++c) {
            f.configs.push_back(configuration(Type, frames, f.first_channel + c, o.rate ? o.rate : 1000000));
            if (o.execution == "bulk") {
                f.producers.push_back(f.source->open_producer(f.configs.back()));
                f.consumers.push_back(f.target->open_consumer(f.configs.back()));
            }
        }
        if (o.execution == "graph") {
            f.sending = graph<Type>(f.configs, true, f.first_channel, o.corrupt_frame);
            f.receiving = graph<Type>(f.configs, false, f.first_channel, 0);
            s7::RuntimeNetContext::set_endpoint(f.source); f.sending->step();
            s7::RuntimeNetContext::set_endpoint(f.target); f.receiving->step();
        }
    }
    s7::RuntimeNetContext::set_endpoint(context.previous);
    const auto total = std::uint64_t(o.packets) * frames;
    const auto origin = Clock::now();
    auto scheduled = [&](std::uint64_t index) {
        return origin + std::chrono::nanoseconds(static_cast<std::int64_t>(static_cast<long double>(index) * 1000000000 / o.rate));
    };
    std::vector<std::jthread> readers, writers;
    for (auto& flow : flows) {
        auto* f = &flow;
        if (o.execution == "graph") readers.emplace_back([&, f] {
            try { for (std::uint64_t i = 1; i < total; ++i) f->receiving->step(); }
            catch (const std::exception& e) { f->errors[0] = e.what(); }
        });
        else for (std::size_t c = 0; c < o.channels; ++c) readers.emplace_back([&, f, c] {
            try {
                std::vector<T> values(chunk);
                for (std::uint64_t first = 0; first < total;) {
                    const auto n = static_cast<std::size_t>(std::min<std::uint64_t>(chunk, total - first));
                    const auto received = f->target->receive(f->consumers[c], values.data(), n);
                    if (received.fault != stream::FaultCode::None || received.received != n)
                        throw std::runtime_error("Receive: " + stream::to_string(received.fault));
                    if (o.rate) f->latency[c].add(std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - scheduled(first)).count(), {});
                    verify<Type>(values, n, first, f->first_channel + c); first += n;
                }
            } catch (const std::exception& e) { f->errors[c] = e.what(); }
        });
    }
    for (auto& flow : flows) {
        auto* f = &flow;
        writers.emplace_back([&, f] {
            try {
                if (o.execution == "graph") {
                    for (std::uint64_t i = 1; i < total; ++i) f->sending->step();
                    return;
                }
                std::vector<T> input(frames);
                for (std::size_t p = 0; p < o.packets; ++p) {
                    if (o.rate) {
                        // The block is available only after its LAST frame exists.
                        const auto ready = scheduled((std::uint64_t(p) + 1) * frames - 1);
                        std::this_thread::sleep_until(ready);
                        const auto late = std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - ready).count();
                        f->max_lateness_ns = std::max<std::int64_t>(f->max_lateness_ns, late);
                        if (late > 1000000) ++f->late_blocks;
                    }
                    for (std::size_t c = 0; c < o.channels; ++c) {
                        fill<Type>(input, std::uint64_t(p) * frames, f->first_channel + c, o.corrupt_frame);
                        for (std::size_t first = 0; first < frames;) {
                            const auto n = std::min(chunk, frames - first);
                            const auto sent = f->source->publish(f->producers[c], input.data() + first, n);
                            if (sent.fault != stream::FaultCode::None || sent.accepted != n)
                                throw std::runtime_error("Publish: " + stream::to_string(sent.fault));
                            first += n;
                        }
                    }
                    if (o.queue_period && p % o.queue_period == 0) {
                        std::size_t logged = 0, buffered = 0;
                        const auto matches = [&](const auto& s) {
                            return std::ranges::any_of(f->configs, [&](const auto& c) { return c.stream_id == s.stream_id; });
                        };
                        for (const auto& s : f->source->stream_statuses())
                            if (s.role == net::StreamRole::Producer && matches(s)) logged += s.send_log_depth;
                        for (const auto& s : f->target->stream_statuses())
                            if (s.role == net::StreamRole::Consumer && matches(s)) buffered += s.buffered_elements;
                        ++f->queue_samples;
                        f->observed_log_packets = std::max(f->observed_log_packets, logged);
                        f->observed_buffered_values = std::max(f->observed_buffered_values, buffered);
                    }
                }
            } catch (const std::exception& e) { f->errors[o.channels] = e.what(); }
        });
    }
    for (auto& writer : writers) writer.join();
    for (auto& reader : readers) reader.join();
    const auto active_seconds = std::chrono::duration<double>(Clock::now() - origin).count();
    const auto setup_seconds = std::chrono::duration<double>(origin - setup_start).count();
    boost::json::array results;
    for (std::size_t d = 0; d < directions; ++d) {
        auto& f = flows[d];
        for (const auto& failure : f.errors) if (!failure.empty()) throw std::runtime_error(failure);
        std::uint64_t packets = 0; std::size_t checked = 0;
        for (const auto& status : f.target->stream_statuses()) {
            if (status.role != net::StreamRole::Consumer || std::ranges::none_of(f.configs, [&](const auto& c) { return c.stream_id == status.stream_id; })) continue;
            if (status.fault != stream::FaultCode::None || status.elements != total || status.next_sequence != status.packets ||
                status.buffered_elements || status.duplicates_discarded || status.malformed) throw std::runtime_error("Consumer counters mismatch");
            packets += status.packets; ++checked;
        }
        if (checked != o.channels) throw std::runtime_error("Missing consumer stream");
        boost::json::array latency;
        for (const auto& histogram : f.latency) if (o.rate) latency.push_back(latency_json(histogram.stats()));
        results.push_back(boost::json::object{{"direction", o.direction == "reverse" || d == 1 ? "reverse" : "forward"},
            {"packets_received", packets}, {"source_blocks_late_over_1ms", f.late_blocks},
            {"max_source_lateness_ns", f.max_lateness_ns}, {"first_frame_delivery_latency", std::move(latency)},
            {"queue_samples", f.queue_samples}, {"observed_send_log_packets_max", f.observed_log_packets},
            {"observed_buffered_values_max", f.observed_buffered_values},
            {"setup_seconds", setup_seconds}, {"active_seconds", active_seconds}});
    }
    return results;
}

template <s7::SignalType Type>
boost::json::object run(const Options& o) {
    const auto frames = wire::payload_element_count(Type, o.packet_bytes);
    if (!frames) throw std::invalid_argument("Payload must contain elements");
    if (o.chunk && (o.mode == "codec" || o.mode == "protocol"))
        throw std::invalid_argument("Chunk selection is for endpoint modes only");
    const auto total_frames = std::uint64_t(o.packets) * frames;
    if (o.corrupt_frame && o.corrupt_frame >= total_frames)
        throw std::invalid_argument("Corrupt frame must lie inside the generated stream");
    if (o.rate && static_cast<long double>(total_frames) / o.rate > 3600)
        throw std::invalid_argument("Paced workload cannot exceed one hour");
    const auto cpu_start = std::clock(); const auto start = Clock::now();
    std::uint64_t packets_received = std::uint64_t(o.packets) * o.channels;
    boost::json::array directions;
    if (o.mode == "codec" || o.mode == "protocol") local<Type>(o, frames);
    else {
        directions = endpoint<Type>(o, frames); packets_received = 0;
        for (const auto& d : directions) packets_received += d.as_object().at("packets_received").to_number<std::uint64_t>();
    }
    const double elapsed = std::chrono::duration<double>(Clock::now() - start).count();
    const double cpu = double(std::clock() - cpu_start) / CLOCKS_PER_SEC;
    const auto direction_count = o.direction == "duplex" ? 2 : 1;
    const auto total_bytes = std::uint64_t(o.packets) * o.packet_bytes * o.channels * direction_count;
    const double active_seconds = directions.empty() ? elapsed : directions.front().as_object().at("active_seconds").to_number<double>();
    const double setup_seconds = directions.empty() ? 0 : directions.front().as_object().at("setup_seconds").to_number<double>();
    const auto primed_frames = o.execution == "graph" ? 1u : 0u;
    const auto active_bytes = (total_frames - primed_frames) * wire::payload_bytes(Type, 1) * o.channels * direction_count;
    boost::json::object result{{"status", "verified"}, {"mode", o.mode}, {"type", o.type},
        {"channel_streams", o.channels}, {"packet_payload_limit_bytes", o.packet_bytes}, {"input_blocks_per_channel", o.packets},
        {"packets_received", packets_received},
        {"frames_per_channel", total_frames}, {"chunk_elements", o.execution == "graph" ? 1 : o.chunk ? o.chunk : frames},
        {"payload_bytes", total_bytes}, {"wall_seconds", elapsed}, {"process_cpu_seconds", cpu},
        {"frames_per_channel_per_second", total_frames / elapsed}, {"payload_bytes_per_second", total_bytes / elapsed},
        {"setup_seconds", setup_seconds}, {"active_seconds", active_seconds}, {"primed_frames_per_channel", primed_frames},
        {"active_payload_bytes_per_second", active_seconds > 0 ? active_bytes / active_seconds : 0},
        {"execution", o.execution}, {"direction", o.direction}, {"directions", std::move(directions)},
        {"offered_frames_per_channel_per_second", o.rate},
        {"queue_sample_every_blocks", o.queue_period},
        {"offered_payload_bytes_per_second", o.rate * std::uint64_t(o.channels) * wire::payload_bytes(Type, 1) * direction_count},
        {"compiler", __VERSION__}, {"hardware_threads", std::thread::hardware_concurrency()},
        {"build_type", S7_BENCH_BUILD_TYPE}, {"send_log_max_packets", 128},
        {"includes_setup_generation_and_verification", true}, {"send_time_stamped", false},
        {"graph_execution_measured", o.execution == "graph"}, {"paced", o.rate != 0}, {"paired_delivery_qualified", false}};
#if defined(__unix__) || defined(__APPLE__)
    rusage usage{};
    if (getrusage(RUSAGE_SELF, &usage) == 0) {
        const auto bytes = std::uint64_t(usage.ru_maxrss) *
#if defined(__APPLE__)
            1;
#else
            1024;
#endif
        result["process_peak_rss_bytes"] = bytes;
    }
#endif
    return result;
}
} // namespace

int main(int argc, char** argv) try {
    const auto o = options(argc, argv);
    const auto result = o.type == "CI12" ? run<s7::SignalType::CI12>(o) :
                        o.type == "CI16" ? run<s7::SignalType::CI16>(o) : run<s7::SignalType::C32>(o);
    std::cout << boost::json::serialize(result) << '\n';
    return 0;
} catch (const std::exception& e) {
    std::cerr << boost::json::serialize(boost::json::object{{"status", "failed"}, {"error", e.what()}}) << '\n';
    return 1;
}
