// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later
// Opt-in node A: real NodeRuntime + Beast/mTLS; dispatch reordering is local
// to this restricted fixture. See CROSS_NODE_CAUSALITY_RESEARCH.md L1/L7/T2.
#include <s7/core.hpp>
#include <s7/net.hpp>
#include <openssl/sha.h>
#include <boost/json.hpp>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <sstream>
#include <thread>
#if defined(__linux__)
#include <sys/utsname.h>
#endif

namespace json = boost::json;
using namespace s7;
const std::string kA = "aaaaaaaa-0000-4000-8000-000000000001";
const std::string kB = "bbbbbbbb-0000-4000-8000-000000000002";
const std::string kC = "cccccccc-0000-4000-8000-000000000003";

std::string file_hash(const std::string& file) {
    std::ifstream input(file, std::ios::binary);
    if (!input) throw std::runtime_error("cannot hash " + file);
    const std::string bytes(std::istreambuf_iterator<char>{input}, {});
    unsigned char digest[SHA256_DIGEST_LENGTH];
    SHA256(reinterpret_cast<const unsigned char*>(bytes.data()), bytes.size(), digest);
    std::ostringstream out;
    for (auto byte : digest) out << std::hex << std::setw(2) << std::setfill('0') << unsigned(byte);
    return out.str();
}

int main(int argc, char** argv)
try {
    std::map<std::string, std::string> options;
    bool wait_start = false;
    for (int i = 1; i < argc; ++i) {
        const std::string option(argv[i]);
        if (option == "--wait-start") { wait_start = true; continue; }
        if (++i == argc) throw std::invalid_argument("missing option value");
        options[option] = argv[i];
    }
    const auto approach = options.at("--approach"), topology = options.at("--topology");
    if (options.at("--role") != "A" || (approach != "fixed" && approach != "projected") ||
        (topology != "feedback" && topology != "independent")) throw std::invalid_argument("unsupported fixture");
    const auto directory = options.at("--credentials");
    const auto window = std::stoul(options.at("--window"));
    const auto packet = std::stoul(options.at("--packet-frames"));
    const auto steps = std::stoul(options.at("--steps"));
    if ((window != 1 && window != 8) || (packet != 1 && packet != 4) || steps == 0 || steps > 10000)
        throw std::invalid_argument("invalid resource limits");
    auto config = [&](const char* id) {
        auto c = net_stream_config(id);
        c.max_elements_per_packet = static_cast<std::uint32_t>(packet);
        c.send_log_max_packets = static_cast<std::uint32_t>(window);
        c.max_batch_latency = std::chrono::milliseconds(5);
        c.max_block = c.max_wait = std::chrono::milliseconds(4000);
        c.sample_rate = stream::SampleRate{1000, 1};
        return c;
    };
    auto outgoing = config("00000000-0000-4000-8000-000000000001");
    outgoing.consumer_nodes = {kB};
    auto incoming = config("00000000-0000-4000-8000-000000000003");
    incoming.producer_node = kC;
    Schema schema("phase2_A");
    schema.set_rate(Rate::from_hz(1000));
    if (topology == "feedback") {
        schema.add_element("state", [] { return std::make_unique<Delay>(0.0); });
        schema.add_element("bridge", [] { return std::make_unique<Gain>(1.0); });
        schema.connect("state", 0, "bridge", 0);
        schema.connect("rx", 0, "state", 0);
    } else schema.add_element("bridge", [] { return std::make_unique<Constant>(2.0); });
    schema.add_element("tx", [outgoing] { return std::make_unique<NetSink>(outgoing); });
    schema.add_element("rx", [incoming] { return std::make_unique<NetSource>(incoming); });
    schema.connect("bridge", 0, "tx", 0);
    auto graph = Compiler::compile(schema);
    if (approach == "projected") {
        graph.causal_order = {"bridge", "tx", "rx"};
        graph.causal_execution.clear();
        for (const auto& id : graph.causal_order) graph.causal_execution.push_back(graph.elements.at(id).get());
        if (graph.domains.size() != 1) throw std::logic_error("single-rate fixture only");
        graph.domains[0].causal_execution = graph.causal_execution;
    }
    json::array order;
    for (const auto& id : graph.causal_order) order.emplace_back(id);
    transport::TlsConfig tls{true, directory + "/node.cert.pem", directory + "/node.key.pem",
                             directory + "/ca.cert.pem", true};
    auto endpoint = std::make_shared<transport::BeastRuntimeNetEndpoint>();
    endpoint->set_node_uuid(kA);
    endpoint->set_clock_usable(false);
    const auto listen = std::stoul(options.at("--listen"));
    if (listen == 0 || listen > 65535) throw std::invalid_argument("invalid listener port");
    static_cast<void>(endpoint->configure_server(static_cast<std::uint16_t>(listen), tls, "127.0.0.1",
                                                transport::ListenerChannels::Data));
    RuntimeNetContext::set_endpoint(endpoint);
    NodeRuntime runtime(std::move(graph));
    std::cout << "{\"ready\":\"A\"}" << std::endl;
    std::string line;
    if (wait_start && (!std::getline(std::cin, line) || line != "start")) throw std::runtime_error("missing start");
    const auto expected_peer = options.contains("--wrong-peer") ? kB : kC;
    endpoint->set_data_peers({{"wss://127.0.0.1:" + options.at("--next"), expected_peer, false}}, tls);
    json::array samples;
    std::string error;
    std::size_t max_log = 0, max_buffer = 0;
    const auto started = std::chrono::steady_clock::now();
    try {
        for (std::size_t n = 0; n < steps; ++n) {
            runtime.step();
            const auto value = runtime.real_output_of("rx");
            if (value != (topology == "feedback" ? static_cast<double>(n + 1) : 5.0))
                throw std::runtime_error("reference mismatch");
            samples.emplace_back(value);
            for (const auto& status : endpoint->stream_statuses()) {
                max_log = std::max(max_log, status.send_log_depth);
                max_buffer = std::max(max_buffer, status.buffered_elements);
            }
        }
        const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (true) {
            bool drained = true;
            for (const auto& status : endpoint->stream_statuses())
                if (status.role == transport::StreamRole::Producer)
                    drained &= status.send_log_depth == 0 && status.elements == steps;
            if (drained) break;
            if (std::chrono::steady_clock::now() > until) throw std::runtime_error("ACK drain timeout");
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    } catch (const std::exception& ex) { error = ex.what(); }
    json::array statuses, errors;
    for (const auto& e : endpoint->take_transport_errors()) errors.emplace_back(e);
    for (const auto& s : endpoint->stream_statuses())
        statuses.emplace_back(json::object{{"state", s.state}, {"fault", stream::to_string(s.fault)},
            {"send_log_depth", s.send_log_depth}, {"buffered_elements", s.buffered_elements},
            {"unauthorized", s.unauthorized}, {"malformed", s.malformed}, {"elements", s.elements}, {"packets", s.packets}});
    std::string host = "development", system = "unqualified";
#if defined(__linux__)
    utsname info{};
    if (uname(&info) == 0) { host = info.nodename; system = std::string(info.sysname) + " " + info.release + " " + info.machine; }
#endif
    json::object result{{"engine", "s7cpp_beast_mtls"}, {"execution", "production_NodeRuntime_fixture_dispatch"},
        {"role", "A"}, {"run_id", options.at("--run-id")}, {"topology", topology}, {"approach", approach},
        {"window", window}, {"packet_frames", packet}, {"steps", samples.size()}, {"samples", std::move(samples)},
        {"completed", error.empty()}, {"error", error.empty() ? json::value(nullptr) : json::value(error)},
        {"sampled_max_send_log", max_log}, {"sampled_max_receive_buffer", max_buffer},
        {"causal_order", std::move(order)}, {"stream_statuses", std::move(statuses)}, {"transport_errors", std::move(errors)},
        {"elapsed_seconds", std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count()},
        {"hostname", host}, {"platform", system}, {"runner_sha256", file_hash(argv[0])}};
    std::cout << json::serialize(result) << std::endl;
    if (wait_start) static_cast<void>(std::getline(std::cin, line));
    RuntimeNetContext::set_endpoint(nullptr);
    return error.empty() ? 0 : 2;
} catch (const std::exception& ex) {
    std::cerr << "S7 node spike: " << ex.what() << '\n';
    return 1;
}
