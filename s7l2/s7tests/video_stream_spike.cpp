// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later
// V0 research driver. External PipeWire adapters supply/consume BGRA8 through
// bounded FIFOs. Real Compiler/NodeRuntime/NetFrame elements execute each pixel.
// No production element registration, device owner or protocol change.
#include <s7/core.hpp>
#include <s7/net_frames.hpp>
#include <boost/json.hpp>
#include <openssl/evp.h>
#include <algorithm>
#include <array>
#include <cerrno>
#include <csignal>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <iostream>
#include <poll.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <thread>
#include <unistd.h>

namespace j = boost::json;
using namespace s7;
using Clock = std::chrono::steady_clock;
volatile std::sig_atomic_t stopped = 0;
constexpr auto producer_id = "aaaaaaaa-0000-4000-8000-000000000001";
constexpr auto consumer_id = "bbbbbbbb-0000-4000-8000-000000000002";
void require(bool value, const std::string& message) { if (!value) throw std::runtime_error(message); }

struct Config {
    std::size_t width{}, height{}, images{}, fps{}, packet{}, window{};
    bool send{}, scalar{}, staged{}, pace_synthetic{true};
    std::string input, output, snapshot, peer;
    transport::TlsConfig tls;
    transport::FrameStreamConfig stream;
    std::size_t pixels() const { return width * height; }
    Rate rate() const { return Rate::from_hz(pixels() * fps); }
};
Config config(const j::object& v) {
    Config c;
    auto number = [&](const char* name, std::size_t maximum) {
        require(v.at(name).is_int64() || v.at(name).is_uint64(), std::string(name) + " must be an integer");
        const auto n = v.at(name).to_number<std::int64_t>();
        require(n > 0 && std::uint64_t(n) <= maximum, std::string("invalid ") + name);
        return std::size_t(n);
    };
    auto string = [&](const char* name) { return std::string(v.at(name).as_string()); };
    const auto role = string("role"), mode = string("mode"), encoding = string("encoding");
    require(role == "send" || role == "receive", "role must be send or receive");
    require(mode == "scalar" || mode == "bulk" || mode == "staged", "mode must be scalar, staged or bulk");
    c.send = role == "send"; c.scalar = mode != "bulk"; c.staged = mode == "staged";
    if (const auto* paced = v.if_contains("pace_synthetic")) c.pace_synthetic = paced->as_bool();
    c.width = number("width", 1920); c.height = number("height", 1080);
    c.images = number("images", 3000); c.fps = number("fps", 120);
    c.packet = number("packet_pixels", 65536); c.window = number("window_packets", 32);
    for (const auto* name : {"input", "output", "snapshot", "peer"}) {
        if (const auto* p = v.if_contains(name)) {
            require(p->is_string(), std::string(name) + " must be a string");
            const std::string s(p->as_string());
            if (std::string_view(name) == "input") c.input = s;
            if (std::string_view(name) == "output") c.output = s;
            if (std::string_view(name) == "snapshot") c.snapshot = s;
            if (std::string_view(name) == "peer") c.peer = s;
        }
    }
    c.tls.enabled = true;
    c.tls.cert_file = string("cert"); c.tls.key_file = string("key"); c.tls.ca_file = string("ca");
    require(c.send || !c.peer.empty(), "receiver requires peer");
    auto& d = c.stream.descriptor;
    d.stream_id = wire::parse_stream_id(string("stream_id"));
    d.type = SignalType::U8; d.channels = {"B", "G", "R", "A"};
    d.frame_rate = {c.rate().numerator, c.rate().denominator};
    d.max_frames_per_packet = static_cast<std::uint32_t>(c.packet);
    d.window_packets = static_cast<std::uint32_t>(c.window); d.window_frames = c.packet * c.window;
    require(encoding == "raw" || encoding == "lz4" || encoding == "zstd", "unsupported spike codec");
    d.encoding = encoding == "lz4" ? frames::Encoding::Lz4 : encoding == "zstd" ? frames::Encoding::Zstd : frames::Encoding::Raw;
    require(frames::encoding_available(d.encoding), "selected codec was not built");
    d.validate();
    c.stream.producer_node = producer_id; c.stream.consumer_nodes = {consumer_id};
    c.stream.max_batch_latency = std::chrono::milliseconds(2);
    c.stream.max_block = c.stream.max_wait = std::chrono::seconds(10);
    return c;
}

// Instance-owned numeric port storage. A failed sample latches this irreversible
// stream, as the hardware and network boundary contracts require.
class Raster final : public Element {
public:
    explicit Raster(bool source) : Element(source ? "SpikeVideoSource" : "SpikeVideoSink", true), source_(source) {
        const std::array<std::string, 4> names{"B", "G", "R", "A"};
        for (std::size_t i = 0; i < 4; ++i) {
            if (source_) register_output(i, names[i], SignalType::U8, &value_[i]);
            else register_input(i, names[i], SignalType::U8, nullptr);
        }
    }
    void stage(std::span<std::uint8_t> image) {
        require(!failed_ && cursor_ == image_.size() && image.size() % 4 == 0, "incomplete or failed raster stage");
        image_ = image; cursor_ = 0;
    }
    void step() override {
        require(!failed_ && image_.size() - cursor_ >= 4, "raster needs a complete staged image");
        for (std::size_t i = 0; i < 4; ++i) {
            if (source_) value_[i] = image_[cursor_ + i];
            else image_[cursor_ + i] = read_input<std::uint8_t>(i);
        }
        cursor_ += 4;
    }
    void on_step_failure() noexcept override { failed_ = true; }
private:
    bool source_{}, failed_{};
    std::array<std::uint8_t, 4> value_{};
    std::span<std::uint8_t> image_;
    std::size_t cursor_{};
};
struct Graph {
    Raster* raster{};
    Raster* handoff{};
    std::unique_ptr<NodeRuntime> runtime;
};
Graph graph(const Config& c) {
    Schema schema("video-spike"); schema.set_rate(c.rate());
    Graph g;
    schema.add_element("video", [&] { auto e = std::make_unique<Raster>(c.send); g.raster = e.get(); return e; });
    if (c.staged) schema.add_element("net", [&] { return std::make_unique<Raster>(!c.send); });
    else if (c.send) schema.add_element("net", [&] { return std::make_unique<NetFrameSink>(c.stream); });
    else schema.add_element("net", [&] { return std::make_unique<NetFrameSource>(c.stream); });
    for (std::size_t i = 0; i < 4; ++i)
        if (c.send) schema.connect("video", i, "net", i); else schema.connect("net", i, "video", i);
    auto compiled = Compiler::compile(schema);
    // Validation instantiates factories too; retain the actual compiled owner.
    g.raster = dynamic_cast<Raster*>(compiled.elements.at("video").get());
    if (c.staged) g.handoff = dynamic_cast<Raster*>(compiled.elements.at("net").get());
    require(g.raster != nullptr, "compiled raster missing");
    g.runtime = std::make_unique<NodeRuntime>(std::move(compiled));
    g.runtime->set_model_time_rate(c.rate());
    return g;
}
class Digest {
public:
    Digest() : context_(EVP_MD_CTX_new(), EVP_MD_CTX_free) {
        require(context_ && EVP_DigestInit_ex(context_.get(), EVP_sha256(), nullptr) == 1, "SHA256 initialization failed");
    }
    void add(std::span<const std::uint8_t> b) { require(EVP_DigestUpdate(context_.get(), b.data(), b.size()) == 1, "SHA256 update failed"); }
    std::string finish() {
        std::array<unsigned char, EVP_MAX_MD_SIZE> bytes{}; unsigned length{};
        require(EVP_DigestFinal_ex(context_.get(), bytes.data(), &length) == 1, "SHA256 final failed");
        constexpr char digits[] = "0123456789abcdef"; std::string s;
        for (unsigned i = 0; i < length; ++i) { s += digits[bytes[i] >> 4]; s += digits[bytes[i] & 15]; }
        return s;
    }
private:
    std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)> context_;
};
class Fifo {
public:
    Fifo(const std::string& path, bool writing) : writing_(writing) {
        const auto deadline = Clock::now() + std::chrono::seconds(15);
        struct stat info{};
        require(::lstat(path.c_str(), &info) == 0 && S_ISFIFO(info.st_mode), "spike I/O path must be an existing FIFO");
        do {
            fd_ = ::open(path.c_str(), (writing ? O_WRONLY : O_RDONLY) | O_NONBLOCK | O_CLOEXEC);
            if (fd_ >= 0) return;
            require(errno == ENXIO && !stopped && Clock::now() < deadline, "FIFO open failed or timed out");
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        } while (true);
    }
    ~Fifo() { if (fd_ >= 0) ::close(fd_); }
    void transfer(std::span<std::uint8_t> bytes) {
        const auto deadline = Clock::now() + std::chrono::seconds(15);
        std::size_t offset = 0;
        while (offset < bytes.size()) {
            require(!stopped && Clock::now() < deadline, "FIFO transfer cancelled or timed out");
            const auto n = writing_ ? ::write(fd_, bytes.data() + offset, bytes.size() - offset) :
                                     ::read(fd_, bytes.data() + offset, bytes.size() - offset);
            if (n > 0) { offset += std::size_t(n); started_ = true; continue; }
            if (n == 0) require(!started_, "short input image / capture EOF");
            else require(errno == EAGAIN || errno == EINTR, std::string("FIFO error: ") + std::strerror(errno));
            pollfd p{fd_, short(writing_ ? POLLOUT : POLLIN), 0}; ::poll(&p, 1, 20);
            if (!started_ && (p.revents & POLLHUP)) std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }
private:
    int fd_{-1}; bool writing_{}, started_{};
};
void synthetic(std::span<std::uint8_t> b, std::size_t image, std::size_t width) {
    for (std::size_t p = 0; p < b.size() / 4; ++p) {
        b[p*4] = std::uint8_t(p % width + image*3);
        b[p*4+1] = std::uint8_t(p / width + image*5);
        b[p*4+2] = std::uint8_t((p % width) ^ (p / width) ^ image);
        b[p*4+3] = 255;
    }
}
void snapshot(const Config& c, std::span<const std::uint8_t> b) {
    if (c.snapshot.empty()) return;
    std::ofstream f(c.snapshot, std::ios::binary);
    f << "P6\n" << c.width << ' ' << c.height << "\n255\n";
    for (std::size_t i = 0; i < b.size(); i += 4) { f.put(char(b[i+2])); f.put(char(b[i+1])); f.put(char(b[i])); }
    require(bool(f), "snapshot write failed");
}
j::object execute(const Config& c, const transport::RuntimeNetEndpointPtr& endpoint, Graph& g,
                  std::unique_ptr<Fifo>& input, std::unique_ptr<Fifo>& output, bool paced) {
    RuntimeNetContext::set_endpoint(endpoint);
    transport::StreamHandle handle;
    if (!c.scalar || c.staged) handle = c.send ? endpoint->open_frame_producer(c.stream) : endpoint->open_frame_consumer(c.stream);
    std::vector<std::uint8_t> image(c.pixels()*4), expected(image.size()), handoff(c.staged ? image.size() : 0);
    if (c.send && !c.input.empty()) input = std::make_unique<Fifo>(c.input, false);
    if (!c.output.empty()) output = std::make_unique<Fifo>(c.output, true);
    Digest digest;
    const auto began = Clock::now(); const auto cpu = std::clock();
    double io_seconds = 0, graph_seconds = 0;
    const auto transfer = [&](std::span<std::uint8_t> pixels, std::size_t n) {
        for (std::size_t p = 0; p < c.pixels();) {
            const auto count = std::min(c.packet, c.pixels()-p);
            if (c.send) {
                const auto r = endpoint->publish_frames(handle, pixels.data()+p*4, count);
                require(r.status == frames::Status::Ok && r.frames > 0, "bulk publish failed: " + frames::to_string(r.status));
                p += r.frames;
            } else {
                const auto r = endpoint->receive_frames(handle, pixels.data()+p*4, count);
                require(r.frames > 0, "bulk receive failed: " + frames::to_string(r.status));
                require(r.info.first_frame == n*c.pixels()+p, "pixel stream position mismatch");
                p += r.frames;
            }
        }
    };
    for (std::size_t n = 0; n < c.images; ++n) {
        require(!stopped, "cancelled");
        auto mark = Clock::now();
        if (c.send) {
            if (input) input->transfer(image);
            else {
                if (paced) std::this_thread::sleep_until(began + std::chrono::nanoseconds(n * 1000000000ULL / c.fps));
                synthetic(image, n, c.width);
            }
        }
        io_seconds += std::chrono::duration<double>(Clock::now()-mark).count();
        mark = Clock::now();
        if (c.staged && !c.send) transfer(handoff, n);
        if (c.scalar) {
            g.raster->stage(image);
            if (c.staged) g.handoff->stage(handoff);
            for (std::size_t p = 0; p < c.pixels(); ++p) g.runtime->step();
        } else transfer(image, n);
        if (c.staged && c.send) transfer(handoff, n);
        graph_seconds += std::chrono::duration<double>(Clock::now()-mark).count();
        if (!c.send && c.input == "synthetic-reference") {
            synthetic(expected, n, c.width); require(image == expected, "synthetic pixels differ");
        }
        digest.add(image);
        mark = Clock::now();
        if (output) output->transfer(image);
        io_seconds += std::chrono::duration<double>(Clock::now()-mark).count();
    }
    const double wall = std::chrono::duration<double>(Clock::now()-began).count();
    const double cpu_seconds = double(std::clock()-cpu)/CLOCKS_PER_SEC;
    snapshot(c, image);
    j::array statuses;
    for (const auto& s : endpoint->stream_statuses()) {
        require(s.fault == stream::FaultCode::None && !s.unauthorized && !s.malformed, "stream fault or rejected traffic");
        statuses.emplace_back(j::object{{"fault", "NONE"}, {"packets", s.packets}, {"elements", s.elements},
            {"send_log_depth", s.send_log_depth}, {"buffered_elements", s.buffered_elements}});
    }
    rusage usage{}; getrusage(RUSAGE_SELF, &usage);
    return {{"role", c.send ? "send" : "receive"}, {"mode", c.staged ? "staged" : c.scalar ? "scalar" : "bulk"},
        {"images", c.images}, {"pixels", c.images*c.pixels()}, {"graph_steps", c.scalar ? g.runtime->step_count() : 0},
        {"sha256", digest.finish()}, {"wall_seconds", wall}, {"cpu_seconds", cpu_seconds},
        {"io_seconds", io_seconds}, {"graph_or_transfer_seconds", graph_seconds},
        {"images_per_second", double(c.images)/wall}, {"pixel_samples_per_second", double(c.images*c.pixels())/wall},
        {"peak_rss_bytes", usage.ru_maxrss*1024}, {"streams", statuses}};
}

int main(int argc, char** argv) try {
    if (argc == 2 && std::string_view(argv[1]) == "--capabilities") {
        j::array codecs;
        for (const auto& [name, codec] : {std::pair{"raw", frames::Encoding::Raw}, {"lz4", frames::Encoding::Lz4}, {"zstd", frames::Encoding::Zstd}})
            if (frames::encoding_available(codec)) codecs.emplace_back(name);
        std::cout << j::serialize(j::object{{"codecs", codecs}}) << '\n'; return 0;
    }
    require(argc == 2, "usage: s7tests_video_stream_spike config.json");
    std::signal(SIGINT, [](int) { stopped = 1; }); std::signal(SIGTERM, [](int) { stopped = 1; });
    std::signal(SIGPIPE, SIG_IGN); // This opt-in process owns its signal policy.
    std::ifstream file(argv[1]); require(bool(file), "config read failed");
    std::string text(65537, '\0'); file.read(text.data(), static_cast<std::streamsize>(text.size()));
    require(!file.bad() && file.gcount() <= 65536, "config read failed or too large");
    text.resize(static_cast<std::size_t>(file.gcount()));
    auto c = config(j::parse(text).as_object());
    auto endpoint = std::make_shared<transport::BeastRuntimeNetEndpoint>();
    endpoint->set_node_uuid(c.send ? producer_id : consumer_id);
    unsigned port = 0;
    if (c.send) port = endpoint->configure_server(0, c.tls);
    else endpoint->configure_client(c.peer, c.tls, true, producer_id);
    Graph g;
    std::unique_ptr<Fifo> input, output;
    if (c.scalar) g = graph(c);
    // Controller checks the identical immutable manifest before starting roles.
    std::cout << j::serialize(j::object{{"ready", true}, {"port", port}}) << std::endl;
    std::string command; std::getline(std::cin, command); require(command == "start", "expected start");
    std::cout << j::serialize(execute(c, endpoint, g, input, output, c.pace_synthetic)) << std::endl;
    // Keep graph AND endpoint alive until the receiver has consumed the final
    // image. A zero replay depth alone does not account for an unsealed batch.
    std::getline(std::cin, command); require(command == "stop", "expected stop");
    if (c.send) {
        const auto deadline = Clock::now() + std::chrono::seconds(10);
        while (!std::ranges::all_of(endpoint->stream_statuses(), [](const auto& s) { return s.send_log_depth == 0; })) {
            require(!stopped && Clock::now() < deadline, "final consumption ACK did not arrive");
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    }
    RuntimeNetContext::set_endpoint(nullptr);
    return 0;
} catch (const std::exception& e) { std::cerr << "video spike: " << e.what() << '\n'; return 1; }
