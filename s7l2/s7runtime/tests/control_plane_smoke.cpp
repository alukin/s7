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

#include <boost/json.hpp>

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
    constexpr std::uint16_t port = 38393;

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
    server.set_control_request_handler([control](const std::string& request_json) {
        return control->handle_request_json(request_json);
    });
    server.configure_server(port);

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    client.configure_client("ws://127.0.0.1:38393");

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

    std::string prepare_token;
    {
        const auto request = R"json({
          "type":"PREPARE_DEPLOYMENT",
          "request_id":"prep-ok",
          "protocol_version":1,
          "deployment":{
            "deployment_id":"dep-ok",
            "revision":1,
            "schema":{"name":"demo","elements":[],"connections":[]},
            "placement":{
              "node_uuid":"11111111-2222-3333-4444-555555555555",
              "assigned_elements":["src"],
              "requires":{
                "libraries":[{"library_id":"s7sdr","version":">=1.2.0"}],
                "elements":["PlutoSource"],
                "hardware":[{"capability":"hw:plutosdr","min_devices":1}]
              }
            },
            "stream_bindings":[]
          }
        })json";
        const auto response_text = client.request_control_json(request);
        const auto response = boost::json::parse(response_text).as_object();
        assert(response.at("type").as_string() == "PREPARE_DEPLOYMENT_RESULT");
        assert(response.at("prepare_status").as_string() == "READY");
        prepare_token = std::string(response.at("prepare_token").as_string().c_str());
        assert(!prepare_token.empty());
    }

    {
        const auto response_text = client.request_control_json(R"json({"type":"LIST_DEPLOYMENTS","request_id":"list-before-commit","protocol_version":1})json");
        const auto response = boost::json::parse(response_text).as_object();
        assert(response.at("type").as_string() == "DEPLOYMENTS");
        assert(response.at("state").as_string() == "idle");
        assert(!response.at("prepared_deployments").as_array().empty());
    }

    {
        boost::json::object commit;
        commit["type"] = "COMMIT_DEPLOYMENT";
        commit["request_id"] = "commit-1";
        commit["protocol_version"] = 1;
        commit["deployment_id"] = "dep-ok";
        commit["revision"] = 1;
        commit["prepare_token"] = prepare_token;
        const auto response_text = client.request_control_json(boost::json::serialize(commit));
        const auto response = boost::json::parse(response_text).as_object();
        assert(response.at("type").as_string() == "COMMIT_DEPLOYMENT_RESULT");
        assert(response.at("committed").as_bool());
    }

    {
        const auto response_text = client.request_control_json(R"json({"type":"LIST_DEPLOYMENTS","request_id":"list-after-commit","protocol_version":1})json");
        const auto response = boost::json::parse(response_text).as_object();
        assert(response.at("type").as_string() == "DEPLOYMENTS");
        assert(response.at("state").as_string() == "running");
        const auto& active = response.at("active_deployment").as_object();
        assert(active.at("deployment_id").as_string() == "dep-ok");
    }

    {
        const auto request = R"json({
          "type":"PREPARE_DEPLOYMENT",
          "request_id":"prep-fail",
          "protocol_version":1,
          "deployment":{
            "deployment_id":"dep-fail",
            "revision":1,
            "schema":{"name":"demo","elements":[],"connections":[]},
            "placement":{
              "node_uuid":"11111111-2222-3333-4444-555555555555",
              "assigned_elements":["src"],
              "requires":{
                "elements":["MissingElement"],
                "hardware":[{"capability":"hw:not-present"}]
              }
            },
            "stream_bindings":[]
          }
        })json";
        const auto response_text = client.request_control_json(request);
        const auto response = boost::json::parse(response_text).as_object();
        assert(response.at("prepare_status").as_string() == "NOT_READY");
        assert(!response.at("incompatibilities").as_array().empty());
    }

    {
        const auto cert_dir = std::filesystem::path(S7_TEST_CERT_DIR);
        assert(std::filesystem::exists(cert_dir / "s7_root_ca.cert.pem"));
        assert(std::filesystem::exists(cert_dir / "s7_runtime_node.cert.pem"));
        assert(std::filesystem::exists(cert_dir / "s7_runtime_node.key.pem"));

        constexpr std::uint16_t tls_port = 38394;
        s7::transport::TlsConfig tls_cfg;
        tls_cfg.enabled = true;
        tls_cfg.cert_file = (cert_dir / "s7_runtime_node.cert.pem").string();
        tls_cfg.key_file = (cert_dir / "s7_runtime_node.key.pem").string();
        tls_cfg.ca_file = (cert_dir / "s7_root_ca.cert.pem").string();
        tls_cfg.require_client_cert = true;

        s7::transport::BeastRuntimeNetEndpoint tls_server;
        s7::transport::BeastRuntimeNetEndpoint tls_client;
        tls_server.set_control_request_handler([control](const std::string& request_json) {
            return control->handle_request_json(request_json);
        });
        tls_server.configure_server(tls_port, tls_cfg);
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        tls_client.configure_client("wss://127.0.0.1:38394", tls_cfg);

        const auto response_text = tls_client.request_control_json(R"json({"type":"HELLO","request_id":"hello-tls","protocol_version":1})json");
        const auto response = boost::json::parse(response_text).as_object();
        assert(response.at("type").as_string() == "HELLO_OK");
    }

    {
        const auto cert_dir = std::filesystem::path(S7_TEST_CERT_DIR);
        s7::RuntimeSchemaDefinition definition;
        definition.name = "identity-test";
        definition.server.enabled = true;
        definition.server.port = 38401;
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
        const auto receiver = endpoint->open_endpoint({
            .endpoint_name = "peer",
            .stream_name = "step.loop",
            .allow_publish = false,
            .allow_receive = true,
        });
        assert(receiver.ok);

        s7::Schema schema{"step_loop"};
        schema.add_element("source", [] { return std::make_unique<s7::Constant>(7.0); });
        schema.add_element("sink", [] { return std::make_unique<s7::NetSink>("step.loop"); });
        schema.connect("source", 0, "sink", 0);
        s7::NodeRuntime runtime{s7::Compiler::compile(schema)};

        const std::atomic<bool> never_stop{false};
        const auto stats = s7::run_step_loop(runtime, 200.0, never_stop,
                                             std::chrono::steady_clock::now() + std::chrono::milliseconds(250));
        assert(stats.stop_reason == s7::StepLoopStats::StopReason::Deadline);
        assert(stats.steps == runtime.step_count());
        assert(stats.steps >= 25 && stats.steps <= 60);

        std::uint64_t received = 0;
        s7::transport::DataPacket packet;
        while (true) {
            const auto result = endpoint->receive(receiver.handle, packet);
            assert(result.ok);
            if (!result.has_packet) {
                break;
            }
            assert(s7::transport::decode_f64_payload(packet.payload) == 7.0);
            ++received;
        }
        assert(received == stats.steps);

        // A stop request ends the loop promptly even at a low step rate.
        std::atomic<bool> stop{false};
        std::thread stopper([&stop] {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            stop.store(true);
        });
        const auto started = std::chrono::steady_clock::now();
        const auto stopped = s7::run_step_loop(runtime, 1.0, stop);
        stopper.join();
        assert(stopped.stop_reason == s7::StepLoopStats::StopReason::StopRequested);
        assert(std::chrono::steady_clock::now() - started < std::chrono::milliseconds(500));
        assert(stopped.steps == 1);

        endpoint->close_endpoint(receiver.handle);
        s7::RuntimeNetContext::set_endpoint(nullptr);
    }

    return 0;
}



