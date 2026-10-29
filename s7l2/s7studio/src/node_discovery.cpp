// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-S7-plugin-permission

#include "node_discovery.hpp"

#include <boost/asio/connect.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl/context.hpp>
#include <boost/beast/core/buffers_to_string.hpp>
#include <boost/beast/core/flat_buffer.hpp>
#include <boost/beast/core/tcp_stream.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/json.hpp>
#include <boost/system/error_code.hpp>

#include <chrono>
#include <cstring>
#include <stdexcept>
#include <string>
#include <utility>

namespace s7studio::ui {
namespace {

namespace websocket = boost::beast::websocket;
namespace ssl = boost::asio::ssl;
using Tcp = boost::asio::ip::tcp;

[[nodiscard]] auto tlsConfigForRequest(const QString& caFile, const QString& certFile, const QString& keyFile, bool enabled)
    -> s7::transport::TlsConfig
{
    s7::transport::TlsConfig tls;
    tls.enabled = enabled;
    tls.ca_file = caFile.toStdString();
    tls.cert_file = certFile.toStdString();
    tls.key_file = keyFile.toStdString();
    return tls;
}

void configure_client_tls_context(ssl::context& ctx, const s7::transport::TlsConfig& tls)
{
    if (!tls.enabled) {
        return;
    }

    if (tls.ca_file.empty() || tls.cert_file.empty() || tls.key_file.empty()) {
        throw std::invalid_argument("Missing TLS files (CA/cert/key) for wss endpoint.");
    }

    ctx.load_verify_file(tls.ca_file);
    ctx.use_certificate_chain_file(tls.cert_file);
    ctx.use_private_key_file(tls.key_file, ssl::context::pem);
    ctx.set_verify_mode(ssl::verify_peer);
}

[[nodiscard]] auto jsonString(const boost::json::object& object, const char* key) -> QString
{
    const auto it = object.find(key);
    if (it == object.end() || !it->value().is_string()) {
        return {};
    }
    return QString::fromStdString(std::string(it->value().as_string().c_str()));
}

[[nodiscard]] auto jsonObject(const boost::json::object& object, const char* key) -> const boost::json::object*
{
    const auto it = object.find(key);
    if (it == object.end() || !it->value().is_object()) {
        return nullptr;
    }
    return &it->value().as_object();
}

[[nodiscard]] auto certificateInfoFromObject(const boost::json::object& object) -> std::pair<QString, QString>
{
    QString commonName;
    QString fingerprint;

    if (const auto* certificate = jsonObject(object, "certificate")) {
        commonName = jsonString(*certificate, "common_name");
        if (commonName.isEmpty()) {
            commonName = jsonString(*certificate, "subject_common_name");
        }
        fingerprint = jsonString(*certificate, "fingerprint_sha256");
    }

    if (commonName.isEmpty()) {
        commonName = jsonString(object, "certificate_common_name");
    }
    if (fingerprint.isEmpty()) {
        fingerprint = jsonString(object, "certificate_fingerprint_sha256");
    }

    return {commonName, fingerprint};
}

[[nodiscard]] auto control_target_for_parsed(const s7::transport::ParsedWebSocketUrl& parsed) -> std::string
{
    if (parsed.target.empty() || parsed.target == "/") {
        return "/control";
    }
    if (parsed.target.ends_with("/control")) {
        return parsed.target;
    }
    if (parsed.target.ends_with("/data")) {
        auto base = parsed.target;
        base.resize(base.size() - std::strlen("/data"));
        if (base.empty()) {
            return "/control";
        }
        return base + "/control";
    }
    return parsed.target + "/control";
}

auto request_control_once(const QString& endpoint, const QString& requestJson, const QString& caFile, const QString& certFile,
                          const QString& keyFile, QString* error,
                          std::optional<s7::transport::PeerCertificate>* serverCertificate = nullptr) -> QString
{
    try {
        const auto parsed = s7::transport::parse_ws_url(endpoint.toStdString());
        const auto host_port = (parsed.host.find(':') != std::string::npos ? "[" + parsed.host + "]" : parsed.host) + ":" + parsed.port;
        const auto target = control_target_for_parsed(parsed);
        const auto tls = tlsConfigForRequest(caFile, certFile, keyFile, parsed.secure);

        boost::asio::io_context io;
        Tcp::resolver resolver(io);
        auto results = resolver.resolve(parsed.host, parsed.port);

        if (parsed.secure) {
            ssl::context ctx(ssl::context::tls_client);
            configure_client_tls_context(ctx, tls);

            websocket::stream<boost::beast::ssl_stream<boost::beast::tcp_stream>> ws(io, ctx);
            boost::beast::get_lowest_layer(ws).expires_after(std::chrono::seconds(2));
            boost::beast::get_lowest_layer(ws).connect(results);
            ws.next_layer().handshake(ssl::stream_base::client);
            if (serverCertificate) {
                *serverCertificate = s7::transport::verified_peer_certificate_of(ws.next_layer().native_handle());
            }
            ws.set_option(websocket::stream_base::timeout::suggested(boost::beast::role_type::client));
            ws.handshake(host_port, target);
            ws.write(boost::asio::buffer(requestJson.toStdString()));
            boost::beast::flat_buffer buffer;
            ws.read(buffer);
            boost::system::error_code close_ec;
            ws.close(websocket::close_code::normal, close_ec);
            return QString::fromStdString(boost::beast::buffers_to_string(buffer.data()));
        }

        websocket::stream<boost::beast::tcp_stream> ws(io);
        boost::beast::get_lowest_layer(ws).expires_after(std::chrono::seconds(2));
        boost::beast::get_lowest_layer(ws).connect(results);
        ws.set_option(websocket::stream_base::timeout::suggested(boost::beast::role_type::client));
        ws.handshake(host_port, target);
        ws.write(boost::asio::buffer(requestJson.toStdString()));
        boost::beast::flat_buffer buffer;
        ws.read(buffer);
        boost::system::error_code close_ec;
        ws.close(websocket::close_code::normal, close_ec);
        return QString::fromStdString(boost::beast::buffers_to_string(buffer.data()));
    } catch (const std::exception& ex) {
        if (error) {
            *error = QString::fromUtf8(ex.what());
        }
        return {};
    }
}

auto nodeKeyForSnapshot(const NodeSnapshot& node) -> QString
{
    if (!node.nodeUuid.isEmpty()) {
        return node.nodeUuid;
    }
    if (!node.certificateFingerprintSha256.isEmpty()) {
        return node.certificateFingerprintSha256;
    }
    return node.endpoint;
}

} // namespace

auto NodeDiscovery::discover(const DiscoveryConfig& config, const std::atomic_bool& cancelled, Request request)
    -> QMap<QString, NodeSnapshot>
{
    if (!request)
        request = [&config](const QString& endpoint, const QString& message) {
            DiscoveryReply reply;
            reply.text =
                request_control_once(endpoint, message, config.caFile, config.certFile, config.keyFile, &reply.error, &reply.certificate);
            return reply;
        };
    const auto requestHello = QStringLiteral("{\"type\":\"HELLO\",\"request_id\":\"studio-hello\",\"protocol_version\":1}");
    const auto requestNodeInfo = QStringLiteral("{\"type\":\"GET_NODE_INFO\",\"request_id\":\"studio-node-info\",\"protocol_version\":1}");
    const auto requestPeers = QStringLiteral(
        "{\"type\":\"GET_PEERS\",\"request_id\":\"studio-peers\",\"protocol_version\":1,\"include_unverified\":true,\"max_entries\":256}");

    QMap<QString, NodeSnapshot> discovered;
    auto addOrUpdate = [&discovered](const NodeSnapshot& node) {
        const auto key = nodeKeyForSnapshot(node);
        if (key.isEmpty()) {
            return;
        }

        auto merged = discovered.value(key);
        if (merged.endpoint.isEmpty() || (!node.endpoint.isEmpty() && merged.endpoint == key)) {
            merged.endpoint = node.endpoint;
        }
        if (!node.nodeUuid.isEmpty())
            merged.nodeUuid = node.nodeUuid;
        if (!node.nodeName.isEmpty())
            merged.nodeName = node.nodeName;
        if (node.directlyChecked) {
            merged.dataEndpoint = node.dataEndpoint;
            merged.dataSecurity = node.dataSecurity;
        }
        if (!node.certificateCommonName.isEmpty())
            merged.certificateCommonName = node.certificateCommonName;
        if (!node.certificateFingerprintSha256.isEmpty())
            merged.certificateFingerprintSha256 = node.certificateFingerprintSha256;
        // Trust established by contacting the node itself is never
        // replaced by what another node reports about it.
        if (!node.trustState.isEmpty() && (node.directlyChecked || !merged.directlyChecked)) {
            merged.trustState = node.trustState;
            merged.directlyChecked = merged.directlyChecked || node.directlyChecked;
        }
        if (!node.reachability.isEmpty())
            merged.reachability = node.reachability;
        if (!node.osFamily.isEmpty())
            merged.osFamily = node.osFamily;
        if (!node.architecture.isEmpty())
            merged.architecture = node.architecture;
        if (!node.activeDeployment.isEmpty())
            merged.activeDeployment = node.activeDeployment;
        if (!node.detailText.isEmpty())
            merged.detailText = node.detailText;
        merged.discoveredBy.unite(node.discoveredBy);
        discovered.insert(key, merged);
    };

    QStringList queue = config.seeds;
    QSet<QString> visited;
    while (!cancelled.load() && !queue.isEmpty() && visited.size() < 128) {
        const auto endpoint = queue.takeFirst();
        if (visited.contains(endpoint)) {
            continue;
        }
        visited.insert(endpoint);

        NodeSnapshot node;
        node.endpoint = endpoint;
        node.reachability = QStringLiteral("offline");
        node.trustState = QStringLiteral("unverified");

        const auto hello = request(endpoint, requestHello);
        if (cancelled.load())
            break;
        if (hello.text.isEmpty()) {
            node.detailText = QStringLiteral("HELLO failed: %1").arg(hello.error);
            addOrUpdate(node);
            continue;
        }

        node.reachability = QStringLiteral("online");
        {
            boost::system::error_code ec;
            const auto parsed = boost::json::parse(hello.text.toStdString(), ec);
            if (!ec && parsed.is_object()) {
                const auto& obj = parsed.as_object();
                node.nodeUuid = jsonString(obj, "node_uuid");
                node.nodeName = jsonString(obj, "node_name");
                const auto [certificateCommonName, certificateFingerprint] = certificateInfoFromObject(obj);
                node.certificateCommonName = certificateCommonName;
                node.certificateFingerprintSha256 = certificateFingerprint;
                if (node.nodeUuid.isEmpty() && !node.certificateCommonName.isEmpty()) {
                    node.nodeUuid = node.certificateCommonName;
                }
            }
        }

        const auto nodeInfo = request(endpoint, requestNodeInfo);
        if (cancelled.load())
            break;
        if (!nodeInfo.text.isEmpty()) {
            boost::system::error_code ec;
            const auto parsed = boost::json::parse(nodeInfo.text.toStdString(), ec);
            if (!ec && parsed.is_object()) {
                const auto& object = parsed.as_object();
                if (const auto nodeIt = object.find("node"); nodeIt != object.end() && nodeIt->value().is_object()) {
                    const auto& nodeObject = nodeIt->value().as_object();
                    if (const auto value = jsonString(nodeObject, "node_uuid"); !value.isEmpty()) {
                        node.nodeUuid = value;
                    }
                    if (const auto value = jsonString(nodeObject, "node_name"); !value.isEmpty()) {
                        node.nodeName = value;
                    }
                    if (const auto value = jsonString(nodeObject, "display_name"); !value.isEmpty() && node.nodeName.isEmpty()) {
                        node.nodeName = value;
                    }
                    const auto [certificateCommonName, certificateFingerprint] = certificateInfoFromObject(nodeObject);
                    if (!certificateCommonName.isEmpty()) {
                        node.certificateCommonName = certificateCommonName;
                    }
                    if (!certificateFingerprint.isEmpty()) {
                        node.certificateFingerprintSha256 = certificateFingerprint;
                    }
                }
                if (const auto platformIt = object.find("platform"); platformIt != object.end() && platformIt->value().is_object()) {
                    node.osFamily = jsonString(platformIt->value().as_object(), "os_family");
                    node.architecture = jsonString(platformIt->value().as_object(), "architecture");
                }
                if (const auto networkIt = object.find("network"); networkIt != object.end() && networkIt->value().is_object()) {
                    const auto listenUrl = jsonString(networkIt->value().as_object(), "listen_url");
                    node.dataEndpoint = jsonString(networkIt->value().as_object(), "data_listen_url");
                    node.dataSecurity = jsonString(networkIt->value().as_object(), "data_security");
                    if (!listenUrl.isEmpty()) {
                        node.endpoint = listenUrl;
                    }
                }
                if (const auto stateIt = object.find("state"); stateIt != object.end() && stateIt->value().is_object()) {
                    const auto& state = stateIt->value().as_object();
                    if (const auto activeIt = state.find("active_deployment"); activeIt != state.end() && activeIt->value().is_string()) {
                        node.activeDeployment = QString::fromStdString(std::string(activeIt->value().as_string().c_str()));
                    }
                }
                node.detailText = QString::fromStdString(boost::json::serialize(parsed));
            }
        }

        // Trust comes from the certificate the node presented: its
        // identity (SAN spiffe://s7/node/<uuid>) must be the claimed node_uuid.
        node.trustState = QString::fromStdString(
            s7::transport::to_string(s7::transport::check_node_identity(hello.certificate, node.nodeUuid.toStdString())));
        node.directlyChecked = true;
        if (node.trustState == QStringLiteral("identity_mismatch")) {
            const auto presented = hello.certificate ? s7::transport::node_uuid_of(*hello.certificate) : std::nullopt;
            node.detailText = QStringLiteral("Certificate identity %1 does not match the claimed node_uuid %2.\n")
                                  .arg(presented ? QString::fromStdString(*presented) : QStringLiteral("(none)"), node.nodeUuid) +
                              node.detailText;
        }
        addOrUpdate(node);

        const auto peers = request(endpoint, requestPeers);
        if (cancelled.load())
            break;
        if (peers.text.isEmpty()) {
            continue;
        }
        boost::system::error_code ec;
        const auto parsed = boost::json::parse(peers.text.toStdString(), ec);
        if (ec || !parsed.is_object()) {
            continue;
        }

        const auto& obj = parsed.as_object();
        const auto peersIt = obj.find("peers");
        if (peersIt == obj.end() || !peersIt->value().is_array()) {
            continue;
        }

        for (const auto& peerValue : peersIt->value().as_array()) {
            if (!peerValue.is_object()) {
                continue;
            }
            const auto& peerObject = peerValue.as_object();
            const auto peerEndpoint = jsonString(peerObject, "endpoint");
            if (peerEndpoint.isEmpty()) {
                continue;
            }

            NodeSnapshot peerNode;
            peerNode.endpoint = peerEndpoint;
            peerNode.nodeUuid = jsonString(peerObject, "node_uuid");
            peerNode.nodeName = jsonString(peerObject, "node_name");
            const auto [peerCertificateCommonName, peerCertificateFingerprint] = certificateInfoFromObject(peerObject);
            peerNode.certificateCommonName = peerCertificateCommonName;
            peerNode.certificateFingerprintSha256 = peerCertificateFingerprint;
            if (peerNode.nodeUuid.isEmpty() && !peerNode.certificateCommonName.isEmpty()) {
                peerNode.nodeUuid = peerNode.certificateCommonName;
            }
            // What a peer reports is a claim until this node is contacted directly.
            peerNode.trustState = QStringLiteral("unverified");
            if (const auto claimed = jsonString(peerObject, "trust_state"); !claimed.isEmpty()) {
                peerNode.detailText = QStringLiteral("Trust reported by %1: %2 (not verified by Studio)").arg(endpoint, claimed);
            }
            peerNode.reachability = jsonString(peerObject, "reachability");
            if (peerNode.reachability.isEmpty()) {
                peerNode.reachability = QStringLiteral("offline");
            }
            if (const auto platformIt = peerObject.find("platform"); platformIt != peerObject.end() && platformIt->value().is_object()) {
                peerNode.osFamily = jsonString(platformIt->value().as_object(), "os_family");
                peerNode.architecture = jsonString(platformIt->value().as_object(), "architecture");
            }
            if (const auto networkIt = peerObject.find("network"); networkIt != peerObject.end() && networkIt->value().is_object()) {
                const auto listenUrl = jsonString(networkIt->value().as_object(), "listen_url");
                if (!listenUrl.isEmpty()) {
                    peerNode.endpoint = listenUrl;
                }
            }
            peerNode.discoveredBy.insert(endpoint);
            addOrUpdate(peerNode);

            if (!visited.contains(peerEndpoint) && !queue.contains(peerEndpoint)) {
                queue.push_back(peerEndpoint);
            }
        }
    }

    return discovered;
}

} // namespace s7studio::ui
