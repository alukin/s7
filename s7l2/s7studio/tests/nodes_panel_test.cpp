// The Headless Nodes panel's trust display: a node is trusted only when the
// certificate it presents carries the node_uuid it claims; another node's
// report that it is trusted is a claim, and a plain WebSocket node is
// unverified.
#include <QApplication>
#include <QEventLoop>
#include <QTimer>

#include <boost/json.hpp>

#include <cstdio>
#include <filesystem>
#include <map>
#include <memory>
#include <string>

#include "headless_nodes_panel.hpp"

namespace {

namespace json = boost::json;
namespace transport = s7::transport;

constexpr const char* kNodeA = "11111111-2222-3333-4444-555555555555"; // the identity in s7_runtime_node's certificate
constexpr const char* kNodeB = "33333333-4444-5555-6666-777777777777";
constexpr const char* kAbsent = "cccccccc-0000-4000-8000-00000000000c";
constexpr const char* kPlain = "dddddddd-0000-4000-8000-00000000000d";

std::string cert(const char* name)
{
    return (std::filesystem::path(S7_TEST_CERT_DIR) / name).string();
}

// A control endpoint answering HELLO, GET_NODE_INFO and GET_PEERS as the node it claims to be.
struct FakeNode {
    transport::BeastRuntimeNetEndpoint endpoint;
    std::string url;

    FakeNode(const std::string& claimed_uuid, const std::string& name, const bool tls, json::array peers = {})
    {
        transport::TlsConfig server_tls;
        if (tls) {
            server_tls = {.enabled = true,
                          .cert_file = cert("s7_runtime_node.cert.pem"),
                          .key_file = cert("s7_runtime_node.key.pem"),
                          .ca_file = cert("s7_root_ca.cert.pem"),
                          .require_client_cert = true};
        }
        const auto port = endpoint.configure_server(0, server_tls);
        url = std::string(tls ? "wss" : "ws") + "://127.0.0.1:" + std::to_string(port);
        endpoint.set_control_request_handler([claimed_uuid, name, peers, url = url](const transport::ControlPeer&, const std::string& text) {
            const auto request = json::parse(text).as_object();
            const auto type = std::string(request.at("type").as_string());
            json::object response{{"request_id", request.at("request_id")}, {"protocol_version", 1}};
            if (type == "HELLO") {
                response["type"] = "HELLO_OK";
                response["node_uuid"] = claimed_uuid;
                response["node_name"] = name;
            } else if (type == "GET_NODE_INFO") {
                response["type"] = "NODE_INFO";
                response["node"] = json::object{{"node_uuid", claimed_uuid}, {"node_name", name}};
                response["network"] = json::object{{"listen_url", url}};
            } else {
                response["type"] = "PEERS";
                response["peers"] = peers;
            }
            return json::serialize(response);
        });
    }
};

int fail(const std::string& what)
{
    std::fprintf(stderr, "nodes panel test: %s\n", what.c_str());
    return 1;
}

} // namespace

int main(int argc, char** argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);

    // A: presents node A's certificate and claims A; it reports an absent node as trusted.
    FakeNode honest(kNodeA, "a", true,
                    json::array{json::object{{"endpoint", "wss://127.0.0.1:1"}, {"node_uuid", kAbsent}, {"node_name", "absent"},
                                             {"trust_state", "verified"}, {"reachability", "online"}}});
    // B: presents node A's certificate but claims to be node B.
    FakeNode impostor(kNodeB, "b", true);
    // D: plain WebSocket, no certificate.
    FakeNode plain(kPlain, "d", false);

    s7studio::ui::HeadlessNodesPanel panel;
    panel.setClientCertificate(QString::fromStdString(cert("s7_root_ca.cert.pem")), QString::fromStdString(cert("s7_studio.cert.pem")),
                               QString::fromStdString(cert("s7_studio.key.pem")));
    for (const auto* node : {&honest, &impostor, &plain}) {
        panel.addSeed(QString::fromStdString(node->url));
    }
    QEventLoop loop;
    QObject::connect(&panel, &s7studio::ui::HeadlessNodesPanel::refreshed, &loop, &QEventLoop::quit);
    bool timed_out = false;
    QTimer::singleShot(30000, &loop, [&] {
        timed_out = true;
        loop.quit();
    });
    panel.refreshNow();
    loop.exec();
    if (timed_out) {
        return fail("the refresh did not finish");
    }

    std::map<std::string, std::string> trust;
    for (const auto& node : panel.knownNodes()) {
        trust[node.nodeUuid.toStdString()] = node.trustState.toStdString();
        std::printf("%s %s %s\n", node.nodeUuid.toStdString().c_str(), node.endpoint.toStdString().c_str(),
                    node.trustState.toStdString().c_str());
    }
    const std::map<std::string, std::string> expected{
        {kNodeA, "verified"},          // its certificate carries the node_uuid it claims
        {kNodeB, "identity_mismatch"}, // its certificate is node A's
        {kAbsent, "unverified"},       // only reported as trusted by A
        {kPlain, "unverified"},        // no certificate
    };
    for (const auto& [uuid, state] : expected) {
        if (trust[uuid] != state) {
            return fail("node " + uuid + " is " + (trust.contains(uuid) ? trust[uuid] : std::string("missing")) + ", expected " + state);
        }
    }
    std::printf("nodes panel trust: OK\n");
    return 0;
}
