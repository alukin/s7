#include <cassert>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <thread>

#include <boost/json.hpp>

#include <s7/control_plane_mvp.hpp>
#include <s7/runtime_service.hpp>
#include <s7/transport.hpp>

#ifndef S7_TEST_CERT_DIR
#define S7_TEST_CERT_DIR "s7runtime/examples/certs"
#endif

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

        s7::RuntimeService service{definition};
        assert(service.runtime_instance_uuid() == "s7-runtime-node");
    }

    return 0;
}



