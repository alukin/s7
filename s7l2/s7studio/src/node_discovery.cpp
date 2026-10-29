// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-S7-plugin-permission

#include "node_discovery.hpp"
#include "studio_control_client.hpp"

#include <boost/json.hpp>
#include <boost/system/error_code.hpp>

#include <chrono>
#include <exception>
#include <string>
#include <utility>

namespace s7studio::ui {
namespace {

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

bool sameCertificate(const DiscoveryReply& first, const DiscoveryReply& second)
{
    if (first.certificate.has_value() != second.certificate.has_value()) return false;
    return !first.certificate || first.certificate->fingerprint_sha256 == second.certificate->fingerprint_sha256;
}

} // namespace

auto NodeDiscovery::discover(const DiscoveryConfig& config, const std::atomic_bool& cancelled, Request request)
    -> QMap<QString, NodeSnapshot>
{
    if (!request)
        request = [&config, &cancelled](const QString& endpoint, const QString& message) {
            DiscoveryReply reply;
            try {
                s7::transport::TlsConfig tls;
                tls.enabled = endpoint.startsWith(QStringLiteral("wss://"));
                tls.ca_file = config.caFile.toStdString();
                tls.cert_file = config.certFile.toStdString();
                tls.key_file = config.keyFile.toStdString();
                auto response = requestStudioControl(endpoint.toStdString(), message.toStdString(), tls, cancelled,
                                                     std::chrono::seconds(3));
                reply.text = QString::fromStdString(response.text);
                reply.certificate = std::move(response.certificate);
            } catch (const std::exception& ex) {
                reply.error = QString::fromUtf8(ex.what());
            }
            return reply;
        };
    const auto requestHello = QStringLiteral("{\"type\":\"HELLO\",\"request_id\":\"studio-hello\",\"protocol_version\":1}");
    const auto requestNodeInfo = QStringLiteral("{\"type\":\"GET_NODE_INFO\",\"request_id\":\"studio-node-info\",\"protocol_version\":1}");
    const auto requestPeers = QStringLiteral(
        "{\"type\":\"GET_PEERS\",\"request_id\":\"studio-peers\",\"protocol_version\":1,\"include_unverified\":true,\"max_entries\":256}");

    QMap<QString, NodeSnapshot> discovered;
    auto addOrUpdate = [&discovered](const QString& key, const NodeSnapshot& node) {
        if (key.isEmpty()) return;
        auto merged = discovered.value(key);
        const bool authoritative = node.directlyChecked || !merged.directlyChecked;
        if (authoritative) {
            if (!node.endpoint.isEmpty()) merged.endpoint = node.endpoint;
            if (node.directlyChecked || !node.nodeUuid.isEmpty()) merged.nodeUuid = node.nodeUuid;
            if (node.directlyChecked || !node.nodeName.isEmpty()) merged.nodeName = node.nodeName;
            if (node.directlyChecked) {
                merged.dataEndpoint = node.dataEndpoint;
                merged.dataSecurity = node.dataSecurity;
            }
            if (node.directlyChecked || !node.certificateCommonName.isEmpty()) merged.certificateCommonName = node.certificateCommonName;
            if (node.directlyChecked || !node.certificateFingerprintSha256.isEmpty())
                merged.certificateFingerprintSha256 = node.certificateFingerprintSha256;
            if (!node.trustState.isEmpty()) merged.trustState = node.trustState;
            merged.directlyChecked = merged.directlyChecked || node.directlyChecked;
            if (!node.reachability.isEmpty()) merged.reachability = node.reachability;
            if (node.directlyChecked || !node.osFamily.isEmpty()) merged.osFamily = node.osFamily;
            if (node.directlyChecked || !node.architecture.isEmpty()) merged.architecture = node.architecture;
            if (node.directlyChecked || !node.activeDeployment.isEmpty()) merged.activeDeployment = node.activeDeployment;
            if (!node.detailText.isEmpty()) merged.detailText = node.detailText;
        }
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
            addOrUpdate(endpoint, node);
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
        const bool infoSamePeer = nodeInfo.text.isEmpty() || sameCertificate(hello, nodeInfo);
        if (!nodeInfo.text.isEmpty() && infoSamePeer) {
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
                    if (!listenUrl.isEmpty()) node.endpoint = listenUrl;
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
        node.trustState = infoSamePeer
            ? QString::fromStdString(s7::transport::to_string(s7::transport::check_node_identity(hello.certificate, node.nodeUuid.toStdString())))
            : QStringLiteral("identity_mismatch");
        node.directlyChecked = true;
        if (hello.certificate) node.certificateFingerprintSha256 = QString::fromStdString(hello.certificate->fingerprint_sha256);
        if (!infoSamePeer) node.detailText.prepend(QStringLiteral("GET_NODE_INFO used a different server certificate.\n"));
        if (node.trustState == QStringLiteral("identity_mismatch")) {
            const auto presented = hello.certificate ? s7::transport::node_uuid_of(*hello.certificate) : std::nullopt;
            node.detailText = QStringLiteral("Certificate identity %1 does not match the claimed node_uuid %2.\n")
                                  .arg(presented ? QString::fromStdString(*presented) : QStringLiteral("(none)"), node.nodeUuid) +
                              node.detailText;
        }
        addOrUpdate(endpoint, node);

        const auto peers = request(endpoint, requestPeers);
        if (cancelled.load())
            break;
        if (peers.text.isEmpty()) {
            continue;
        }
        if (!sameCertificate(hello, peers)) {
            node.trustState = QStringLiteral("identity_mismatch");
            node.detailText.prepend(QStringLiteral("GET_PEERS used a different server certificate.\n"));
            addOrUpdate(endpoint, node);
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
            peerNode.discoveredBy.insert(endpoint);
            addOrUpdate(peerEndpoint, peerNode);

            if (!visited.contains(peerEndpoint) && !queue.contains(peerEndpoint)) {
                queue.push_back(peerEndpoint);
            }
        }
    }

    return discovered;
}

} // namespace s7studio::ui
