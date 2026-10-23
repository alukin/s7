// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: LGPL-3.0-or-later

#include <algorithm>
#include <atomic>
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <random>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include <boost/json.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/beast/websocket.hpp>

#include <s7/control_plane_mvp.hpp>
#include <s7/net.hpp>
#include <s7/runtime_service.hpp>
#include <s7/transport.hpp>

#ifndef S7_TEST_CERT_DIR
#define S7_TEST_CERT_DIR "s7runtime/examples/certs"
#endif

namespace {

// Tests never touch the developer's real node state under $HOME.
std::filesystem::path make_temp_state_dir(const std::string& label)
{
    std::random_device random;
    auto dir = std::filesystem::temp_directory_path() /
               ("s7runtime-test-" + label + "-" + std::to_string(random()));
    std::filesystem::create_directories(dir);
    return dir;
}

} // namespace

int main()
{
    {
        // A recorded source is available through the runtime schema loader,
        // including to services constructed without the CLI registration path.
        const auto state_dir = make_temp_state_dir("recorded-source");
        const auto path = state_dir / "recording.dat";
        {
            std::ofstream file(path);
            file << "# s7 gnuplot sample file\n# format=gnuplot-text\n# version=1\n# signal=F64\n# rate=8/1\n# description=\"Runtime replay test\"\n# columns=sample_index value\n0 1.25\n1 -3.5\n";
        }
        s7::RuntimeSchemaDefinition definition;
        definition.name = "recorded-source";
        definition.elements.push_back({"recording", "RecordedSource",
            {{"file_path", path.string()}, {"format", "gnuplot-text"}, {"sample_rate_hz", 8}, {"end_behavior", "repeat"}}, {}});
        {
            s7::RuntimeService service{definition, state_dir};
            service.initialize_graph();
            for (int n = 0; n < 6; ++n) {
                service.step();
                if (service.active_real_output("recording") != (n % 2 ? -3.5 : 1.25))
                    throw std::runtime_error("Runtime recording replay did not preserve the sample sequence.");
            }
        }
        std::filesystem::remove_all(state_dir);
    }

    {
        // The default binary recording sink is discoverable by the runtime,
        // reports its output path and produces a self-describing replay file.
        s7::register_builtin_elements(s7::ElementRegistry::instance());
        const auto dir = make_temp_state_dir("recording-sink");
        const auto path = dir / "voltage.s7bin";
        s7::RuntimeSchemaDefinition definition;
        definition.name = "record-and-replay";
        definition.elements.push_back({"value", "Constant", {{"value", 2}}, {}});
        definition.elements.push_back({"save", "RecordingSink",
            {{"file_path", path.string()}, {"sample_rate_hz", 8}, {"description", "Voltage [V]"}}, {}});
        definition.connections.push_back({"value", 0, "save", 0});
        {
            s7::RuntimeService service{definition, dir / "writer"};
            if (service.output_files() != std::vector<std::filesystem::path>{path})
                throw std::runtime_error("Runtime did not report the recording output file.");
            service.initialize_graph();
            if (std::filesystem::exists(path)) throw std::runtime_error("Compiling a sink must not create its file.");
            service.step(); service.step();
        }
        definition.connections.clear();
        definition.elements.clear();
        definition.elements.push_back({"replay", "RecordedSource", {{"file_path", path.string()}}, {}});
        {
            s7::RuntimeService service{definition, dir / "reader"};
            service.initialize_graph();
            for (int n = 0; n < 2; ++n) {
                service.step();
                if (service.active_real_output("replay") != 2)
                    throw std::runtime_error("Runtime binary recording round trip failed.");
            }
        }
        std::filesystem::remove_all(dir);
    }

    s7::ControlNodeDescriptor descriptor;
    descriptor.node_uuid = "11111111-2222-3333-4444-555555555555";
    descriptor.node_name = "node-a";
    descriptor.display_name = "Node A";
    descriptor.instance_id = "aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee";
    descriptor.started_at = "2026-09-20T00:00:00Z";
    descriptor.identity_certificate_file = "/tmp/s7-runtime-node.cert.pem";
    descriptor.identity_certificate_common_name = "s7-runtime-node";
    descriptor.identity_certificate_fingerprint_sha256 = "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
    descriptor.os_family = "linux";
    descriptor.os_version = "unknown";
    descriptor.architecture = "x86_64";
    descriptor.target_triple = "x86_64-linux";
    descriptor.listen_url = "ws://127.0.0.1:38393";
    descriptor.element_types = {"PlutoSource", "Fft", "NetSink"};
    descriptor.hardware_capabilities = {"hw:plutosdr"};
    descriptor.libraries.push_back(s7::ControlLibraryInfo{
        .library_id = "s7sdr",
        .kind = "plugin",
        .version = "1.2.0",
        .target_triple = "x86_64-linux",
        .origin = "test",
        .status = "ready",
        .status_reason = {},
        .elements = {"PlutoSource"},
    });

    auto control = std::make_shared<s7::ControlPlaneMvp>(descriptor);
    control->add_seed_peer("ws://seed.local:8443");

    s7::transport::BeastRuntimeNetEndpoint server;
    s7::transport::BeastRuntimeNetEndpoint client;
    server.set_control_request_handler([control](const s7::transport::ControlPeer& peer, const std::string& request_json) {
        return control->handle_request_json(peer, request_json);
    });
    const auto port = server.configure_server(0);

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    client.configure_client("ws://127.0.0.1:" + std::to_string(port));

    {
        const auto response_text = client.request_control_json(R"json({"type":"HELLO","request_id":"hello-1","protocol_version":1})json");
        const auto response = boost::json::parse(response_text).as_object();
        assert(response.at("type").as_string() == "HELLO_OK");
        assert(response.at("node_uuid").as_string() == descriptor.node_uuid);
        const auto& certificate = response.at("certificate").as_object();
        assert(certificate.at("common_name").as_string() == descriptor.identity_certificate_common_name);
        assert(certificate.at("fingerprint_sha256").as_string() == descriptor.identity_certificate_fingerprint_sha256);
    }

    {
        const auto response_text = client.request_control_json(R"json({"type":"GET_NODE_INFO","request_id":"node-info-1","protocol_version":1})json");
        const auto response = boost::json::parse(response_text).as_object();
        assert(response.at("type").as_string() == "NODE_INFO");
        const auto& node = response.at("node").as_object();
        const auto& certificate = node.at("certificate").as_object();
        assert(certificate.at("common_name").as_string() == descriptor.identity_certificate_common_name);
        assert(certificate.at("fingerprint_sha256").as_string() == descriptor.identity_certificate_fingerprint_sha256);
    }

    {
        const auto request = R"json({
            "type":"PEER_ANNOUNCE",
            "request_id":"peer-1",
            "protocol_version":1,
            "self":{
              "endpoint":"ws://node-b.local:8443",
              "node_uuid":"bbbbbbbb-2222-3333-4444-555555555555",
              "node_name":"node-b",
              "certificate":{"common_name":"node-b","fingerprint_sha256":"bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"},
              "platform":{"os_family":"linux","architecture":"arm64"}
            }
        })json";
        const auto response_text = client.request_control_json(request);
        const auto response = boost::json::parse(response_text).as_object();
        assert(response.at("type").as_string() == "PEER_ANNOUNCE_OK");
    }

    {
        const auto response_text = client.request_control_json(R"json({"type":"GET_PEERS","request_id":"peers-1","protocol_version":1})json");
        const auto response = boost::json::parse(response_text).as_object();
        assert(response.at("type").as_string() == "PEERS");
        assert(response.at("peers").as_array().size() >= 2);
        bool found_announced_peer = false;
        for (const auto& peer_value : response.at("peers").as_array()) {
            if (!peer_value.is_object()) {
                continue;
            }
            const auto& peer = peer_value.as_object();
            if (peer.at("endpoint").as_string() != "ws://node-b.local:8443") {
                continue;
            }
            const auto& certificate = peer.at("certificate").as_object();
            assert(certificate.at("common_name").as_string() == "node-b");
            assert(certificate.at("fingerprint_sha256").as_string() == "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb");
            found_announced_peer = true;
        }
        assert(found_announced_peer);
    }

    {
        // Configuration changes are refused over plain WebSocket, whatever the JSON claims.
        const auto request = R"json({
          "type":"PREPARE_DEPLOYMENT",
          "request_id":"prep-plain",
          "protocol_version":1,
          "studio":{"node_uuid":"22222222-3333-4444-5555-666666666666","role":"control"},
          "deployment":{"deployment_id":"dep-plain","revision":1,"base_revision":0}
        })json";
        for (const auto* type : {"PREPARE_DEPLOYMENT", "COMMIT_DEPLOYMENT", "ABORT_DEPLOYMENT"}) {
            auto message = boost::json::parse(request).as_object();
            message["type"] = type;
            const auto response = boost::json::parse(client.request_control_json(boost::json::serialize(message))).as_object();
            assert(response.at("type").as_string() == "ERROR");
            assert(response.at("code").as_string() == "UNAUTHORIZED");
        }
    }

    for (const bool require_client_cert : {true, false}) {
        const auto cert_dir = std::filesystem::path(S7_TEST_CERT_DIR);
        assert(std::filesystem::exists(cert_dir / "s7_root_ca.cert.pem"));
        assert(std::filesystem::exists(cert_dir / "s7_runtime_node.cert.pem"));
        assert(std::filesystem::exists(cert_dir / "s7_runtime_node.key.pem"));

        s7::transport::TlsConfig tls_cfg;
        tls_cfg.enabled = true;
        tls_cfg.cert_file = (cert_dir / "s7_runtime_node.cert.pem").string();
        tls_cfg.key_file = (cert_dir / "s7_runtime_node.key.pem").string();
        tls_cfg.ca_file = (cert_dir / "s7_root_ca.cert.pem").string();
        tls_cfg.require_client_cert = require_client_cert;

        s7::transport::BeastRuntimeNetEndpoint tls_server;
        s7::transport::BeastRuntimeNetEndpoint tls_client;
        tls_server.set_control_request_handler([control](const s7::transport::ControlPeer& peer, const std::string& request_json) {
            return control->handle_request_json(peer, request_json);
        });
        const auto tls_port = tls_server.configure_server(0, tls_cfg);
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        tls_client.configure_client("wss://127.0.0.1:" + std::to_string(tls_port), tls_cfg);

        const auto response_text = tls_client.request_control_json(R"json({"type":"HELLO","request_id":"hello-tls","protocol_version":1})json");
        const auto response = boost::json::parse(response_text).as_object();
        assert(response.at("type").as_string() == "HELLO_OK");

        // The live conformance script also connects without a certificate.
        // Optional certificates admit that peer, without authorizing mutations.
        boost::asio::io_context io;
        boost::asio::ssl::context context{boost::asio::ssl::context::tls_client};
        context.load_verify_file(tls_cfg.ca_file);
        context.set_verify_mode(boost::asio::ssl::verify_peer);
        boost::beast::websocket::stream<boost::beast::ssl_stream<boost::beast::tcp_stream>> anonymous{io, context};
        bool handshake_rejected = false;
        try {
            auto& socket = boost::beast::get_lowest_layer(anonymous);
            socket.expires_after(std::chrono::seconds(5));
            socket.connect({boost::asio::ip::make_address("127.0.0.1"), tls_port});
            anonymous.next_layer().handshake(boost::asio::ssl::stream_base::client);
            anonymous.handshake("127.0.0.1", "/control");
        } catch (const std::exception&) {
            if (!require_client_cert) throw;
            handshake_rejected = true;
        }
        assert(handshake_rejected == require_client_cert);
        if (!require_client_cert) {
            const auto send = [&anonymous](const boost::json::object& request) {
                anonymous.write(boost::asio::buffer(boost::json::serialize(request)));
                boost::beast::flat_buffer buffer;
                anonymous.read(buffer);
                return boost::json::parse(boost::beast::buffers_to_string(buffer.data())).as_object();
            };
            const auto hello = send({{"type", "HELLO"}, {"request_id", "anonymous-hello"}, {"protocol_version", 1}});
            assert(hello.at("type").as_string() == "HELLO_OK");
            for (const auto* type : {"PREPARE_DEPLOYMENT", "COMMIT_DEPLOYMENT", "ABORT_DEPLOYMENT"}) {
                const auto denied = send({{"type", type}, {"request_id", "anonymous-mutation"}, {"protocol_version", 1},
                                          {"studio", {{"role", "control"}}}});
                assert(denied.at("type").as_string() == "ERROR");
                assert(denied.at("code").as_string() == "CERTIFICATE_REQUIRED");
            }
            anonymous.close(boost::beast::websocket::close_code::normal);
        }
    }

    {
        const auto cert_dir = std::filesystem::path(S7_TEST_CERT_DIR);
        s7::RuntimeSchemaDefinition definition;
        definition.name = "identity-test";
        definition.server.enabled = true;
        definition.server.port = 0;
        definition.server.tls.enabled = true;
        definition.server.tls.cert_file = (cert_dir / "s7_runtime_node.cert.pem").string();
        definition.server.tls.key_file = (cert_dir / "s7_runtime_node.key.pem").string();
        definition.server.tls.ca_file = (cert_dir / "s7_root_ca.cert.pem").string();

        const auto state_dir = make_temp_state_dir("identity");
        {
            s7::RuntimeService service{definition, state_dir};
            assert(service.runtime_instance_uuid() == "11111111-2222-3333-4444-555555555555");

            // A second runtime cannot share the state directory.
            bool rejected = false;
            try {
                s7::RuntimeService second{s7::RuntimeSchemaDefinition{}, state_dir};
            } catch (const std::runtime_error&) {
                rejected = true;
            }
            assert(rejected);
        }

        // The stored identity must agree with the certificate.
        {
            std::ofstream(state_dir / "node_uuid.txt", std::ios::trunc) << "aaaaaaaa-2222-3333-4444-555555555555\n";
            bool rejected = false;
            try {
                s7::RuntimeService service{definition, state_dir};
            } catch (const std::runtime_error&) {
                rejected = true;
            }
            assert(rejected);
        }
        std::filesystem::remove_all(state_dir);
    }

    {
        // Without a certificate, distinct state directories give distinct, persistent node UUIDs.
        const auto dir_a = make_temp_state_dir("node-a");
        const auto dir_b = make_temp_state_dir("node-b");
        std::string uuid_a;
        {
            s7::RuntimeService a{s7::RuntimeSchemaDefinition{}, dir_a};
            s7::RuntimeService b{s7::RuntimeSchemaDefinition{}, dir_b};
            uuid_a = a.runtime_instance_uuid();
            assert(uuid_a != b.runtime_instance_uuid());
        }
        {
            s7::RuntimeService a{s7::RuntimeSchemaDefinition{}, dir_a};
            assert(a.runtime_instance_uuid() == uuid_a);
        }
        std::filesystem::remove_all(dir_a);
        std::filesystem::remove_all(dir_b);
    }

    {
        // A served node steps Constant -> NetSink at the configured rate, and a peer receives the packets.
        auto endpoint = std::make_shared<s7::transport::InMemoryRuntimeNetEndpoint>();
        s7::RuntimeNetContext::set_endpoint(endpoint);
        const auto config = s7::net_stream_config("6f1c2e1a-0b7d-4c56-9a0e-1f2d3c4b5a69");
        const auto receiver = endpoint->open_consumer(config);

        s7::Schema schema{"step_loop"};
        schema.set_rate(s7::Rate::from_hz(1));
        schema.add_element("source", [] { return std::make_unique<s7::Constant>(7.0); });
        schema.add_element("sink", [config] { return std::make_unique<s7::NetSink>(config); });
        schema.connect("source", 0, "sink", 0);
        s7::NodeRuntime runtime{s7::Compiler::compile(schema)};

        const std::atomic<bool> never_stop{false};
        const auto stats = s7::run_step_loop([&runtime] { runtime.step(); }, 200.0, never_stop,
                                             std::chrono::steady_clock::now() + std::chrono::milliseconds(250));
        assert(stats.stop_reason == s7::StepLoopStats::StopReason::Deadline);
        assert(stats.steps == runtime.step_count());
        assert(stats.steps >= 25 && stats.steps <= 60);

        // Every published value arrives exactly once.
        std::vector<double> values(stats.steps);
        const auto result = endpoint->receive(receiver, values.data(), values.size());
        assert(result.fault == s7::stream::FaultCode::None && result.received == stats.steps);
        assert(std::ranges::all_of(values, [](double v) { return v == 7.0; }));
        for (const auto& status : endpoint->stream_statuses()) {
            assert(status.buffered_elements == 0);
        }

        // A stop request ends the loop promptly even at a low step rate.
        std::atomic<bool> stop{false};
        std::thread stopper([&stop] {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            stop.store(true);
        });
        const auto started = std::chrono::steady_clock::now();
        const auto stopped = s7::run_step_loop([&runtime] { runtime.step(); }, 1.0, stop);
        stopper.join();
        assert(stopped.stop_reason == s7::StepLoopStats::StopReason::StopRequested);
        assert(std::chrono::steady_clock::now() - started < std::chrono::milliseconds(500));
        assert(stopped.steps == 1);

        endpoint->close_stream(receiver);
        s7::RuntimeNetContext::set_endpoint(nullptr);
    }

    return 0;
}



