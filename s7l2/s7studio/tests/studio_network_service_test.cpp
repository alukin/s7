// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-S7-plugin-permission

#include "studio_network_service.hpp"

#include <atomic>
#include <filesystem>
#include <stdexcept>

int main()
{
    std::atomic_bool cancelled = false;
    int requests = 0;
    auto service = s7studio::ui::makeStudioNetworkService(
        [&](const s7::deployment::RuntimeNode& node, const boost::json::object& request,
            const s7::transport::TlsConfig&, const std::atomic_bool&) -> boost::json::object {
            ++requests;
            if (node.node_uuid != "node-a" || request.at("type").as_string() != "GET_NETWORK_CONFIGURATION")
                throw std::runtime_error("unexpected request");
            return {{"type", "NETWORK_CONFIGURATION"}, {"revision", 7},
                    {"diagram", boost::json::object{{"document_kind", "s7.diagram"}}}};
        });
    const s7::deployment::RuntimeNode node{"node-a", {}, "wss://node-a"};
    const auto fetched = service->fetchDeployedDiagram(node, {}, cancelled);
    if (!fetched.found || fetched.revision != 7 || !fetched.diagram.is_object() || requests != 1) return 1;

    cancelled.store(true);
    try {
        static_cast<void>(service->fetchDeployedDiagram(node, {}, cancelled));
        return 2;
    } catch (const std::runtime_error&) {
        if (requests != 1) return 3;
    }

    const auto cert = [](const char* name) { return (std::filesystem::path(S7_TEST_CERT_DIR) / name).string(); };
    s7::transport::TlsConfig serverTls{.enabled = true, .cert_file = cert("s7_runtime_node.cert.pem"),
        .key_file = cert("s7_runtime_node.key.pem"), .ca_file = cert("s7_root_ca.cert.pem"), .require_client_cert = true};
    s7::transport::BeastRuntimeNetEndpoint server;
    const auto port = server.configure_server(0, serverTls);
    std::atomic_int received = 0;
    server.set_control_request_handler([&](const s7::transport::ControlPeer&, const std::string&) {
        ++received;
        return std::string{"{\"type\":\"NETWORK_CONFIGURATION\",\"revision\":1,\"diagram\":{}}"};
    });
    s7::transport::TlsConfig clientTls{.enabled = true, .cert_file = cert("s7_studio.cert.pem"),
        .key_file = cert("s7_studio.key.pem"), .ca_file = cert("s7_root_ca.cert.pem")};
    cancelled.store(false);
    const s7::deployment::RuntimeNode wrongNode{"33333333-4444-5555-6666-777777777777", {},
        "wss://127.0.0.1:" + std::to_string(port)};
    try {
        static_cast<void>(s7studio::ui::makeStudioNetworkService()->fetchDeployedDiagram(wrongNode, clientTls, cancelled));
        return 4;
    } catch (const std::runtime_error&) {
        if (received.load() != 0) return 5;
    }
    return 0;
}
