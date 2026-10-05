#include <cassert>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <memory>
#include <map>
#include <optional>
#include <random>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include <boost/json.hpp>

#include <s7/builtin_element_registry.hpp>
#include <s7/canonical_json.hpp>
#include <s7/deployment_client.hpp>
#include <s7/net.hpp>
#include <s7/control_plane_mvp.hpp>
#include <s7/element_registry.hpp>
#include <s7/runtime_service.hpp>
#include <s7/transport.hpp>

#ifndef S7_TEST_CERT_DIR
#define S7_TEST_CERT_DIR "s7runtime/examples/certs"
#endif

namespace {

namespace json = boost::json;
using s7::transport::ControlPeer;

// Development placeholder for the control-node role (see tools/generate_test_runtime_certs.sh).
constexpr const char* kStudioKeyPurposeOid = "2.25.4266950079767543379770189370854073200";

std::string cert_path(const std::string& name)
{
    return (std::filesystem::path(S7_TEST_CERT_DIR) / name).string();
}

std::filesystem::path make_temp_state_dir(const std::string& label)
{
    std::random_device random;
    auto dir = std::filesystem::temp_directory_path() / ("s7runtime-test-" + label + "-" + std::to_string(random()));
    std::filesystem::create_directories(dir);
    return dir;
}

ControlPeer verified_peer(const std::string& certificate_name)
{
    return ControlPeer{.tls = true, .certificate = s7::transport::inspect_certificate_file(cert_path(certificate_name))};
}

s7::ControlAuthorization studio_authorization()
{
    return s7::ControlAuthorization{
        .studio_key_purpose_oid = kStudioKeyPurposeOid,
        .studio_certificate_sha256 = s7::transport::inspect_certificate_file(cert_path("s7_studio.cert.pem")).fingerprint_sha256,
    };
}

json::object constant_gain_fragment(const double value)
{
    auto fragment = json::parse(R"json({
        "name": "fragment",
        "elements": [
            { "id": "source", "type": "Constant", "params": { "value": 0.0 } },
            { "id": "gain", "type": "Gain", "params": { "factor": 1.0 } }
        ],
        "connections": [ { "from_element": "source", "from_port": 0, "to_element": "gain", "to_port": 0 } ]
    })json").as_object();
    fragment["elements"].as_array()[0].as_object()["params"].as_object()["value"] = value;
    return fragment;
}

// The node the test control planes run as (also the SAN of s7_runtime_node.cert.pem).
constexpr const char* kNodeUuid = "11111111-2222-3333-4444-555555555555";

// A diagram with every element of the fragment placed on `node_uuid`.
json::object diagram_on_node(const json::object& fragment, const std::string& node_uuid = kNodeUuid)
{
    auto schema = fragment;
    if (auto* elements = schema.if_contains("elements"); elements && elements->is_array()) {
        for (auto& element : elements->as_array()) {
            element.as_object()["node"] = node_uuid;
        }
    }
    return json::object{
        {"document_kind", "s7.diagram"},
        {"runtime_nodes", json::array{json::object{{"node_uuid", node_uuid}, {"name", "test"}, {"endpoint", ""}}}},
        {"schema", std::move(schema)},
    };
}

json::object prepare_request(const std::string& deployment_id,
                             const std::uint64_t revision,
                             const std::uint64_t base_revision,
                             const json::object& fragment,
                             const std::optional<json::object>& required_caps = std::nullopt,
                             const std::string& node_uuid = kNodeUuid)
{
    json::object configuration{
        {"project_revision", "git:" + deployment_id + "-" + std::to_string(revision)},
        {"diagram", diagram_on_node(fragment, node_uuid)},
    };
    configuration["configuration_digest"] = s7::configuration_digest(configuration);

    json::object deployment{
        {"deployment_id", deployment_id},
        {"revision", revision},
        {"base_revision", base_revision},
        {"configuration", std::move(configuration)},
    };
    if (required_caps) {
        deployment["requires"] = *required_caps;
    }
    return json::object{
        {"type", "PREPARE_DEPLOYMENT"},
        {"request_id", "prep-" + deployment_id},
        {"protocol_version", 1},
        {"deployment", std::move(deployment)},
    };
}

json::object token_request(const std::string& type,
                           const std::string& deployment_id,
                           const std::uint64_t revision,
                           const std::string& token)
{
    return json::object{
        {"type", type},
        {"request_id", type + "-" + deployment_id},
        {"protocol_version", 1},
        {"deployment_id", deployment_id},
        {"revision", revision},
        {"prepare_token", token},
    };
}

void expect(const bool condition, const std::string& what)
{
    if (!condition) {
        throw std::runtime_error("Check failed: " + what);
    }
}

void expect_error(const json::object& response, const std::string& code, const std::string& what)
{
    const auto* type = response.if_contains("type");
    const auto* actual = response.if_contains("code");
    expect(type && type->as_string() == "ERROR" && actual && actual->as_string() == code,
           what + ": expected " + code + ", got " + json::serialize(response));
}

// Stands in for the runtime: records which configuration is active.
class FakeHost final : public s7::DeploymentHost {
public:
    struct Staged final : s7::StagedConfiguration {
        double value{};
    };

    auto stage(const json::object& fragment, const std::vector<s7::transport::DataPeer>&) -> std::unique_ptr<s7::StagedConfiguration> override
    {
        if (fragment.at("elements").as_array()[0].as_object().at("params").as_object().contains("broken")) {
            throw std::invalid_argument("fragment is broken");
        }
        auto staged = std::make_unique<Staged>();
        staged->value = fragment.at("elements").as_array()[0].as_object().at("params").as_object().at("value").to_number<double>();
        return staged;
    }

    void activate(s7::StagedConfiguration& staged) override
    {
        if (fail_next_activation) {
            fail_next_activation = false;
            throw std::runtime_error("device unavailable");
        }
        active_value = dynamic_cast<Staged&>(staged).value;
    }

    auto execution_state() const -> std::string override { return "stepping"; }

    std::optional<double> active_value;
    bool fail_next_activation{false};
};

class Client {
public:
    Client(s7::ControlPlaneMvp& control, ControlPeer peer) : control_(control), peer_(std::move(peer)) {}

    json::object send(const json::object& request)
    {
        return s7::parse_json_precise(control_.handle_request_json(peer_, json::serialize(request))).as_object();
    }

private:
    s7::ControlPlaneMvp& control_;
    ControlPeer peer_;
};

void test_canonical_json()
{
    // RFC 8785 number serialization examples.
    const std::vector<std::pair<std::string, std::string>> numbers{
        {"0", "0"}, {"-0.0", "0"}, {"1", "1"}, {"-1", "-1"}, {"4.50", "4.5"}, {"0.002", "0.002"},
        {"1e-7", "1e-7"}, {"1e-6", "0.000001"}, {"1e21", "1e+21"}, {"1e20", "100000000000000000000"},
        {"1e30", "1e+30"}, {"333333333.33333329", "333333333.3333333"}, {"9007199254740992", "9007199254740992"},
        {"-1.5e-10", "-1.5e-10"}, {"123456789012345680000", "123456789012345680000"},
    };
    for (const auto& [input, expected] : numbers) {
        const auto actual = s7::canonical_json(s7::parse_json_precise(input));
        expect(actual == expected, "canonical number " + input + " -> " + actual + ", expected " + expected);
    }

    // Keys sort by UTF-16 code units; strings use minimal escaping.
    const auto sorted = s7::canonical_json(s7::parse_json_precise(
        R"({"\u20ac":1,"\r":2,"\ufb33":3,"1":4,"\ud83d\ude00":5,"\u0080":6,"\u00f6":7,"b":[true,null,"a\"\\\u0001\u007f/"]})"));
    expect(sorted == "{\"\\r\":2,\"1\":4,\"b\":[true,null,\"a\\\"\\\\\\u0001\x7f/\"],\"\xc2\x80\":6,\"\xc3\xb6\":7,"
                     "\"\xe2\x82\xac\":1,\"\xf0\x9f\x98\x80\":5,\"\xef\xac\xb3\":3}",
           "canonical key order and escaping: " + sorted);

    expect(s7::sha256_hex("abc") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", "sha256(abc)");

    bool rejected = false;
    try {
        static_cast<void>(s7::canonical_json(json::value(9007199254740993ULL)));
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    expect(rejected, "integers beyond 2^53 are rejected");
}

void test_authorization()
{
    auto host = std::make_shared<FakeHost>();
    std::vector<std::string> log;
    s7::ControlPlaneMvp control{s7::ControlNodeDescriptor{.node_uuid = kNodeUuid},
                                s7::ControlPlaneOptions{
                                    .authorization = studio_authorization(),
                                    .deployment_host = host,
                                    .log = [&log](const std::string& line) { log.push_back(line); },
                                }};
    const auto request = prepare_request("dep-auth", 1, 0, constant_gain_fragment(2.0));

    expect_error(Client(control, ControlPeer{}).send(request), "UNAUTHORIZED", "plain connection");
    expect_error(Client(control, ControlPeer{.tls = true}).send(request), "CERTIFICATE_REQUIRED", "TLS without certificate");

    // A runtime-node certificate is not Studio authority, whatever the JSON says.
    auto claiming = request;
    claiming["studio"] = json::object{{"certificate_sha256", studio_authorization().studio_certificate_sha256}, {"role", "control"}};
    const auto node_response = Client(control, verified_peer("s7_runtime_node.cert.pem")).send(claiming);
    expect_error(node_response, "UNAUTHORIZED", "runtime-node certificate");
    expect(node_response.at("details").as_object().contains("peer_certificate_sha256"), "denial names the peer certificate");

    // A certificate with the Studio role but not the pinned one is refused.
    s7::ControlPlaneMvp other_pin{s7::ControlNodeDescriptor{.node_uuid = kNodeUuid},
                                  s7::ControlPlaneOptions{
                                      .authorization = {.studio_key_purpose_oid = kStudioKeyPurposeOid,
                                                        .studio_certificate_sha256 = std::string(64, 'a')},
                                      .deployment_host = host,
                                  }};
    expect_error(Client(other_pin, verified_peer("s7_studio.cert.pem")).send(request), "UNAUTHORIZED", "unpinned Studio");

    // A node without configured Studio authorization accepts no changes.
    s7::ControlPlaneMvp unconfigured{s7::ControlNodeDescriptor{.node_uuid = kNodeUuid}, s7::ControlPlaneOptions{.deployment_host = host}};
    expect_error(Client(unconfigured, verified_peer("s7_studio.cert.pem")).send(request), "UNAUTHORIZED", "no Studio configured");

    const auto ok = Client(control, verified_peer("s7_studio.cert.pem")).send(request);
    expect(ok.at("prepare_status").as_string() == "READY", "authorized Studio prepares: " + json::serialize(ok));
    expect(!host->active_value, "prepare does not activate");

    const auto studio_fingerprint = studio_authorization().studio_certificate_sha256;
    expect(std::ranges::any_of(log, [&](const std::string& line) {
               return line.starts_with("control.authorized type=PREPARE_DEPLOYMENT") &&
                      line.find(studio_fingerprint) != std::string::npos;
           }),
           "authorized request is logged with the Studio certificate");
    expect(std::ranges::any_of(log, [](const std::string& line) { return line.starts_with("control.denied"); }),
           "denied requests are logged");
}

void test_deployment_lifecycle()
{
    auto host = std::make_shared<FakeHost>();
    s7::ControlPlaneMvp control{
        s7::ControlNodeDescriptor{.node_uuid = kNodeUuid, .element_types = {"Constant", "Gain"}},
        s7::ControlPlaneOptions{.authorization = studio_authorization(), .deployment_host = host}};
    Client studio(control, verified_peer("s7_studio.cert.pem"));

    // Request shape and digest are validated.
    auto tampered = prepare_request("dep-1", 1, 0, constant_gain_fragment(2.0));
    tampered["deployment"].as_object()["configuration"].as_object()["project_revision"] = "git:other";
    expect_error(studio.send(tampered), "INVALID_REQUEST", "digest mismatch");

    auto zero_revision = prepare_request("dep-1", 0, 0, constant_gain_fragment(2.0));
    expect_error(studio.send(zero_revision), "INVALID_REQUEST", "revision 0");

    auto huge_revision = prepare_request("dep-1", 1, 0, constant_gain_fragment(2.0));
    huge_revision["deployment"].as_object()["revision"] = 9007199254740993ULL;
    expect_error(studio.send(huge_revision), "INVALID_REQUEST", "revision beyond 2^53");

    auto no_base = prepare_request("dep-1", 1, 0, constant_gain_fragment(2.0));
    no_base["deployment"].as_object().erase("base_revision");
    expect_error(studio.send(no_base), "INVALID_REQUEST", "missing base_revision");

    for (const auto* malformed : {R"({"elements":"Gain"})", R"({"elements":[42]})", R"({"libraries":[{"version":">=1"}]})",
                                  R"({"hardware":["hw:x"]})", R"({"libraries":[{"library_id":"x","version":"~1"}]})"}) {
        expect_error(studio.send(prepare_request("dep-1", 1, 0, constant_gain_fragment(2.0), json::parse(malformed).as_object())),
                     "INVALID_REQUEST", std::string{"malformed requires "} + malformed);
    }

    // Missing capabilities and invalid configurations are NOT_READY.
    const auto missing = studio.send(prepare_request("dep-1", 1, 0, constant_gain_fragment(2.0),
                                                     json::object{{"elements", json::array{"PlutoSource"}}}));
    expect(missing.at("prepare_status").as_string() == "NOT_READY", "missing element is NOT_READY");
    expect(missing.at("incompatibilities").as_array()[0].as_object().at("code").as_string() == "MISSING_ELEMENT", "issue code");

    auto broken = constant_gain_fragment(2.0);
    broken["elements"].as_array()[0].as_object()["params"].as_object()["broken"] = true;
    const auto invalid = studio.send(prepare_request("dep-broken", 1, 0, broken));
    expect(invalid.at("prepare_status").as_string() == "NOT_READY", "stage failure is NOT_READY");
    expect(invalid.at("incompatibilities").as_array()[0].as_object().at("code").as_string() == "INVALID_CONFIGURATION",
           "stage failure issue code");

    // Prepare is idempotent and the token stays out of listings.
    const auto request = prepare_request("dep-1", 1, 0, constant_gain_fragment(2.0),
                                         json::object{{"elements", json::array{"Constant", "Gain"}}});
    const auto prepared = studio.send(request);
    expect(prepared.at("prepare_status").as_string() == "READY", "prepare READY");
    const std::string token = prepared.at("prepare_token").as_string().c_str();
    expect(token.size() == 5 + 64 && token.starts_with("ptok_"), "256-bit token");
    expect(studio.send(request).at("prepare_token").as_string() == token, "repeated prepare returns the same token");

    const auto listing = studio.send(json::object{{"type", "LIST_DEPLOYMENTS"}, {"protocol_version", 1}});
    expect(listing.at("state").as_string() == "idle", "idle before commit");
    expect(listing.at("prepared_deployments").as_array().size() == 1, "one prepared deployment");
    expect(json::serialize(listing).find(token) == std::string::npos, "listing does not reveal the token");

    // Commit with the wrong token fails; with the right token it activates.
    expect_error(studio.send(token_request("COMMIT_DEPLOYMENT", "dep-1", 1, "ptok_wrong")), "PREPARE_TOKEN_INVALID", "wrong token");
    const auto committed = studio.send(token_request("COMMIT_DEPLOYMENT", "dep-1", 1, token));
    expect(committed.at("committed").as_bool() && committed.at("state").as_string() == "active", "commit: " + json::serialize(committed));
    expect(committed.at("configuration_digest") == prepared.at("configuration_digest"), "commit reports the prepared digest");
    expect(host->active_value == 2.0, "commit activated the staged configuration");

    const auto again = studio.send(token_request("COMMIT_DEPLOYMENT", "dep-1", 1, token));
    expect(again.at("committed").as_bool() && again.at("committed_at") == committed.at("committed_at"), "commit is idempotent");
    expect_error(studio.send(token_request("ABORT_DEPLOYMENT", "dep-1", 1, token)), "INVALID_REQUEST", "abort after commit");

    const auto active_listing = studio.send(json::object{{"type", "LIST_DEPLOYMENTS"}, {"protocol_version", 1}});
    expect(active_listing.at("state").as_string() == "active" && active_listing.at("active_revision").to_number<std::uint64_t>() == 1,
           "listing shows the active revision");

    // base_revision must match the active revision.
    expect_error(studio.send(prepare_request("dep-2", 2, 0, constant_gain_fragment(3.0))), "REVISION_CONFLICT", "stale base");

    // A failed activation leaves the previous configuration active.
    const auto second = studio.send(prepare_request("dep-2", 2, 1, constant_gain_fragment(3.0)));
    host->fail_next_activation = true;
    expect_error(studio.send(token_request("COMMIT_DEPLOYMENT", "dep-2", 2, second.at("prepare_token").as_string().c_str())),
                 "INTERNAL_ERROR", "failed activation");
    expect(host->active_value == 2.0, "previous configuration stays active after failed activation");
    expect(studio.send(json::object{{"type", "LIST_DEPLOYMENTS"}, {"protocol_version", 1}}).at("active_revision").to_number<std::uint64_t>() == 1,
           "active revision unchanged after failed activation");

    // Two preparations on the same base: committing one supersedes the other.
    const auto a = studio.send(prepare_request("dep-3a", 3, 1, constant_gain_fragment(4.0)));
    const auto b = studio.send(prepare_request("dep-3b", 3, 1, constant_gain_fragment(5.0)));
    expect(studio.send(token_request("COMMIT_DEPLOYMENT", "dep-3a", 3, a.at("prepare_token").as_string().c_str())).at("committed").as_bool(),
           "first commit");
    expect_error(studio.send(token_request("COMMIT_DEPLOYMENT", "dep-3b", 3, b.at("prepare_token").as_string().c_str())),
                 "REVISION_CONFLICT", "superseded preparation");
    expect(host->active_value == 4.0, "superseded deployment did not activate");

    // Abort is idempotent.
    const auto c = studio.send(prepare_request("dep-4", 4, 3, constant_gain_fragment(6.0)));
    const std::string c_token = c.at("prepare_token").as_string().c_str();
    expect_error(studio.send(token_request("ABORT_DEPLOYMENT", "dep-4", 4, "ptok_wrong")), "PREPARE_TOKEN_INVALID", "abort wrong token");
    expect(studio.send(token_request("ABORT_DEPLOYMENT", "dep-4", 4, c_token)).at("aborted").as_bool(), "abort");
    expect(studio.send(token_request("ABORT_DEPLOYMENT", "dep-4", 4, c_token)).at("aborted").as_bool(), "repeated abort");
    expect_error(studio.send(token_request("COMMIT_DEPLOYMENT", "dep-4", 4, c_token)), "PREPARE_TOKEN_INVALID", "commit after abort");
}

void test_prepare_expiry()
{
    auto host = std::make_shared<FakeHost>();
    s7::ControlPlaneMvp control{s7::ControlNodeDescriptor{.node_uuid = kNodeUuid},
                                s7::ControlPlaneOptions{.authorization = studio_authorization(),
                                                        .deployment_host = host,
                                                        .prepare_ttl = std::chrono::seconds(0)}};
    Client studio(control, verified_peer("s7_studio.cert.pem"));
    const auto prepared = studio.send(prepare_request("dep-x", 1, 0, constant_gain_fragment(1.0)));
    const std::string token = prepared.at("prepare_token").as_string().c_str();
    expect_error(studio.send(token_request("COMMIT_DEPLOYMENT", "dep-x", 1, token)), "PREPARE_EXPIRED", "expired token");
    expect_error(studio.send(token_request("COMMIT_DEPLOYMENT", "dep-x", 1, token)), "PREPARE_EXPIRED", "expired token, again");
    expect(!host->active_value, "expired deployment did not activate");
}

// End to end: a runtime node over mTLS, deployed by the pinned Studio.
void test_runtime_deployment_over_mtls()
{
    auto definition = s7::JsonSchemaLoader::load_from_json(constant_gain_fragment(1.0));
    definition.server.enabled = true;
    definition.server.port = 0; // a free port
    definition.server.tls = {.enabled = true,
                             .cert_file = cert_path("s7_runtime_node.cert.pem"),
                             .key_file = cert_path("s7_runtime_node.key.pem"),
                             .ca_file = cert_path("s7_root_ca.cert.pem"),
                             .require_client_cert = true};
    definition.control = studio_authorization();

    const auto state_dir = make_temp_state_dir("deploy");
    {
        s7::RuntimeService service{definition, state_dir};
        service.initialize_graph();
        const auto port = service.start_websocket_server();
        service.step();
        expect(service.active_real_output("gain") == 1.0, "initial graph runs");

        auto client_for = [port](const std::string& name, const std::string& ca) {
            auto client = std::make_unique<s7::transport::BeastRuntimeNetEndpoint>();
            client->configure_client("wss://127.0.0.1:" + std::to_string(port),
                                     {.enabled = true,
                                      .cert_file = cert_path(name + ".cert.pem"),
                                      .key_file = cert_path(name + ".key.pem"),
                                      .ca_file = cert_path(ca)});
            return client;
        };
        auto send = [](s7::transport::BeastRuntimeNetEndpoint& client, const json::object& request) {
            return json::parse(client.request_control_json(json::serialize(request))).as_object();
        };

        const auto request = prepare_request("dep-live", 1, 0, constant_gain_fragment(5.0));

        // A valid runtime-node certificate is authenticated but not authorized.
        auto node_client = client_for("s7_runtime_node", "s7_root_ca.cert.pem");
        expect_error(send(*node_client, request), "UNAUTHORIZED", "runtime-node certificate over mTLS");

        // A Studio-role certificate from an untrusted CA cannot complete the handshake.
        bool untrusted_rejected = false;
        try {
            auto untrusted = client_for("s7_untrusted_studio", "s7_root_ca.cert.pem");
            const auto response = send(*untrusted, request);
            untrusted_rejected = response.at("type").as_string() == "ERROR";
        } catch (const std::exception&) {
            untrusted_rejected = true;
        }
        expect(untrusted_rejected, "untrusted Studio certificate is rejected");

        auto studio_client = client_for("s7_studio", "s7_root_ca.cert.pem");
        const auto prepared = send(*studio_client, request);
        expect(prepared.at("prepare_status").as_string() == "READY", "prepare over mTLS: " + json::serialize(prepared));
        service.step();
        expect(service.active_real_output("gain") == 1.0, "prepared graph is not active yet");

        // A diagram with nothing placed on this node is not for it.
        const auto elsewhere = send(*studio_client, prepare_request("dep-elsewhere", 1, 0, constant_gain_fragment(7.0), std::nullopt,
                                                                    "99999999-0000-4000-8000-000000000000"));
        expect(elsewhere.at("prepare_status").as_string() == "NOT_READY", "a diagram without this node is NOT_READY");

        // A graph that does not compile is NOT_READY and changes nothing.
        auto unknown = constant_gain_fragment(7.0);
        unknown["elements"].as_array()[1].as_object()["type"] = "NoSuchElement";
        const auto not_ready = send(*studio_client, prepare_request("dep-bad", 1, 0, unknown));
        expect(not_ready.at("prepare_status").as_string() == "NOT_READY", "unknown element type is NOT_READY");

        const auto committed = send(*studio_client, token_request("COMMIT_DEPLOYMENT", "dep-live", 1,
                                                                  prepared.at("prepare_token").as_string().c_str()));
        expect(committed.at("committed").as_bool(), "commit over mTLS: " + json::serialize(committed));
        service.step();
        expect(service.active_real_output("gain") == 5.0, "committed graph is the running graph");
        expect(service.step_count() == 1, "the new graph starts at step 0");
    }
    std::filesystem::remove_all(state_dir);
}

} // namespace

void test_stream_status_and_recovery()
{
    auto network = std::make_shared<s7::transport::InMemoryRuntimeNetEndpoint>();
    int recoveries = 0;
    s7::ControlPlaneMvp control{s7::ControlNodeDescriptor{.node_uuid = kNodeUuid},
                                s7::ControlPlaneOptions{.authorization = studio_authorization(),
                                                        .network = network,
                                                        .on_stream_recovered = [&recoveries] { ++recoveries; }}};
    s7::stream::StreamConfig config;
    config.stream_id = s7::wire::parse_stream_id("5d4c3b2a-1f0e-4d9c-8b7a-6f5e4d3c2b1a");
    config.max_wait = std::chrono::milliseconds(20);
    const auto consumer = network->open_consumer(config);
    double value = 0;
    expect(network->receive(consumer, &value, 1).fault == s7::stream::FaultCode::Underrun, "consumer starves");

    const json::object status_request{{"type", "GET_STREAM_STATUS"}, {"request_id", "st-1"}, {"protocol_version", 1}};
    auto status = Client(control, ControlPeer{}).send(status_request);
    expect(status.at("type").as_string() == "STREAM_STATUS", "stream status is read-only");
    const auto& stream = status.at("streams").as_array().at(0).as_object();
    expect(stream.at("stream_id").as_string() == "5d4c3b2a-1f0e-4d9c-8b7a-6f5e4d3c2b1a" &&
               stream.at("role").as_string() == "consumer" && stream.at("data_type").as_string() == "F64" &&
               stream.at("state").as_string() == "faulted" && stream.at("fault").as_string() == "UNDERRUN",
           "faulted stream reported: " + json::serialize(stream));

    json::object recover{{"type", "RECOVER_STREAM"}, {"request_id", "rec-1"}, {"protocol_version", 1},
                         {"stream_id", "5d4c3b2a-1f0e-4d9c-8b7a-6f5e4d3c2b1a"}};
    expect_error(Client(control, ControlPeer{}).send(recover), "UNAUTHORIZED", "recovery needs Studio authority");
    expect(recoveries == 0, "refused recovery does not resume stepping");
    const auto recovered = Client(control, verified_peer("s7_studio.cert.pem")).send(recover);
    expect(recovered.at("type").as_string() == "RECOVER_STREAM_RESULT" && recovered.at("recovered").as_bool(),
           "Studio recovers the stream");
    expect(recoveries == 1, "recovery resumes stepping");
    status = Client(control, ControlPeer{}).send(status_request);
    expect(status.at("streams").as_array().at(0).as_object().at("state").as_string() == "active", "stream active again");

    recover["stream_id"] = "00000000-0000-4000-8000-000000000000";
    expect(!Client(control, verified_peer("s7_studio.cert.pem")).send(recover).at("recovered").as_bool(), "unknown stream");
    expect(recoveries == 1, "nothing recovered, nothing resumed");
    recover["stream_id"] = "not-a-uuid";
    expect_error(Client(control, verified_peer("s7_studio.cert.pem")).send(recover), "INVALID_REQUEST", "malformed stream id");
}

// Records what it is given to run; fails staging on request.
class RecordingHost final : public s7::DeploymentHost {
public:
    struct Staged final : s7::StagedConfiguration {
        json::object schema;
        std::vector<s7::transport::DataPeer> peers;
    };
    auto stage(const json::object& schema, const std::vector<s7::transport::DataPeer>& peers) -> std::unique_ptr<s7::StagedConfiguration> override
    {
        if (fail) {
            throw std::invalid_argument("device busy");
        }
        auto staged = std::make_unique<Staged>();
        staged->schema = schema;
        staged->peers = peers;
        return staged;
    }
    void activate(s7::StagedConfiguration& staged) override
    {
        active = dynamic_cast<Staged&>(staged).schema;
        peers = dynamic_cast<Staged&>(staged).peers;
    }
    auto execution_state() const -> std::string override { return "not_stepping"; }

    bool fail{false};
    std::optional<json::object> active;
    std::vector<s7::transport::DataPeer> peers;
};

void test_deploy_client()
{
    constexpr const char* kOther = "33333333-4444-5555-6666-777777777777";
    auto host_a = std::make_shared<RecordingHost>();
    auto host_b = std::make_shared<RecordingHost>();
    s7::ControlPlaneMvp node_a{s7::ControlNodeDescriptor{.node_uuid = kNodeUuid},
                               s7::ControlPlaneOptions{.authorization = studio_authorization(), .deployment_host = host_a}};
    s7::ControlPlaneMvp node_b{s7::ControlNodeDescriptor{.node_uuid = kOther},
                               s7::ControlPlaneOptions{.authorization = studio_authorization(), .deployment_host = host_b}};
    std::map<std::string, s7::ControlPlaneMvp*> by_endpoint{{"wss://a", &node_a}, {"wss://b", &node_b}};
    auto request = [&](const s7::deployment::RuntimeNode& node, const json::object& message) {
        return Client(*by_endpoint.at(node.endpoint), verified_peer("s7_studio.cert.pem")).send(message);
    };

    auto diagram = json::parse(R"json({
        "document_kind": "s7.diagram",
        "runtime_nodes": [
            { "node_uuid": "11111111-2222-3333-4444-555555555555", "endpoint": "wss://a" },
            { "node_uuid": "33333333-4444-5555-6666-777777777777", "endpoint": "wss://b" }
        ],
        "schema": {
            "name": "pair",
            "elements": [
                { "id": "source", "type": "Constant", "params": { "value": 4.0 }, "node": "11111111-2222-3333-4444-555555555555" },
                { "id": "gain", "type": "Gain", "params": { "factor": 2.0 }, "node": "33333333-4444-5555-6666-777777777777" }
            ],
            "connections": [ { "from_element": "source", "from_port": 0, "to_element": "gain", "to_port": 0 } ]
        }
    })json").as_object();
    const auto types = s7::deployment::infer_output_types(diagram);

    // A node that cannot run its part: nothing is committed, the other preparation is aborted.
    host_b->fail = true;
    auto failed = s7::deployment::deploy_diagram(diagram, types, request);
    expect(!failed.committed && failed.nodes.size() == 2, "deployment fails as a whole");
    expect(failed.nodes[0].status == "ready" && failed.nodes[1].status == "not_ready" &&
               failed.nodes[1].message.find("device busy") != std::string::npos,
           "per-node outcome reported: " + failed.nodes[1].message);
    expect(!host_a->active && !host_b->active, "nothing activated");
    const json::object list{{"type", "LIST_DEPLOYMENTS"}, {"request_id", "l"}, {"protocol_version", 1}};
    const auto history = Client(node_a, ControlPeer{}).send(list);
    expect(history.at("prepared_deployments").as_array().empty() &&
               history.at("history").as_array().back().as_object().at("outcome").as_string() == "aborted",
           "the ready node's preparation was aborted");

    host_b->fail = false;
    const auto deployed = s7::deployment::deploy_diagram(diagram, types, request);
    expect(deployed.committed && deployed.revision == 1, "deployed to both nodes");
    expect(host_a->active && host_b->active, "both nodes run their part");
    expect(host_b->peers == std::vector<s7::transport::DataPeer>{{"wss://a", kNodeUuid}}, "the consumer connects to the producer");
    const auto& b_elements = host_b->active->at("elements").as_array();
    expect(b_elements.size() == 2 && b_elements[1].as_object().at("type").as_string() == "NetSource", "B consumes a stream");

    const json::object get{{"type", "GET_NETWORK_CONFIGURATION"}, {"request_id", "g"}, {"protocol_version", 1}};
    const auto configuration = Client(node_b, ControlPeer{}).send(get);
    expect(configuration.at("type").as_string() == "NETWORK_CONFIGURATION" && configuration.at("revision").to_number<std::uint64_t>() == 1 &&
               configuration.at("diagram").as_object().at("streams") == deployed.diagram.at("streams") &&
               configuration.at("configuration_digest").as_string() == deployed.configuration_digest,
           "the node returns the deployed diagram");

    // The next deployment builds on both nodes' active revision and keeps the stream.
    auto next = deployed.diagram;
    next["schema"].as_object()["elements"].as_array()[1].as_object()["params"].as_object()["factor"] = 3.0;
    const auto again = s7::deployment::deploy_diagram(next, types, request);
    expect(again.committed && again.revision == 2, "redeployed at revision 2");
    expect(again.diagram.at("streams") == deployed.diagram.at("streams"), "the stream keeps its id");
    auto old_revision = get;
    old_revision["revision"] = 1;
    expect_error(Client(node_b, ControlPeer{}).send(old_revision), "REVISION_NOT_FOUND", "only the active revision is retained");

    // An endpoint answering as another node is refused before anything is prepared.
    by_endpoint["wss://b"] = &node_a;
    const auto wrong = s7::deployment::deploy_diagram(again.diagram, types, request);
    expect(!wrong.committed && !wrong.errors.empty() && wrong.errors.front().find("not 33333333") != std::string::npos,
           "node identity checked: " + (wrong.errors.empty() ? std::string() : wrong.errors.front()));
}

int main()
try {
    s7::register_builtin_elements(s7::ElementRegistry::instance());
    test_canonical_json();
    test_authorization();
    test_deployment_lifecycle();
    test_prepare_expiry();
    test_stream_status_and_recovery();
    s7::register_net_elements(s7::ElementRegistry::instance());
    test_deploy_client();
    test_runtime_deployment_over_mtls();
    return 0;
} catch (const std::exception& ex) {
    std::cerr << "s7runtime deployment smoke: " << ex.what() << '\n';
    return 1;
}
