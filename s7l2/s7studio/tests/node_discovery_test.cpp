// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-S7-plugin-permission

#include "node_discovery.hpp"

#include <boost/json.hpp>

#include <atomic>

int main()
{
    using s7studio::ui::DiscoveryConfig;
    using s7studio::ui::DiscoveryReply;
    using s7studio::ui::NodeDiscovery;
    const DiscoveryConfig config{{
                                     QStringLiteral("ws://seed"),
                                 },
                                 {},
                                 {},
                                 {}};
    std::atomic_bool cancelled = false;
    int requests = 0;
    const auto request = [&](const QString& endpoint, const QString& json) -> DiscoveryReply {
        ++requests;
        const auto type = boost::json::parse(json.toStdString()).as_object().at("type").as_string();
        if (endpoint == QStringLiteral("ws://peer")) {
            return {{}, QStringLiteral("offline"), {}};
        }
        if (type == "HELLO")
            return {QStringLiteral("{\"node_uuid\":\"seed\",\"node_name\":\"Seed\"}"), {}, {}};
        if (type == "GET_NODE_INFO")
            return {QStringLiteral("{\"node\":{\"node_uuid\":\"seed\",\"node_name\":\"Seed\"}}"), {}, {}};
        if (type == "GET_PEERS")
            return {QStringLiteral("{\"peers\":[{\"endpoint\":\"ws://"
                                   "peer\",\"node_uuid\":\"peer\",\"trust_state\":\"verified\",\"reachability\":\"online\"}]}"),
                    {},
                    {}};
        return {};
    };
    const auto found = NodeDiscovery::discover(config, cancelled, request);
    if (requests != 4 || !found.contains(QStringLiteral("ws://seed")) || !found.contains(QStringLiteral("ws://peer")))
        return 1;
    const auto peer = found.value(QStringLiteral("ws://peer"));
    if (peer.trustState != QStringLiteral("unverified") || peer.directlyChecked || peer.dataEndpoint != QString{} ||
        !peer.discoveredBy.contains(QStringLiteral("ws://seed")))
        return 2;

    cancelled.store(true);
    requests = 0;
    if (!NodeDiscovery::discover(config, cancelled, request).empty() || requests != 0)
        return 3;

    cancelled.store(false);
    const auto certificate = [](const std::string& fingerprint, const std::string& uuid) {
        return s7::transport::PeerCertificate{fingerprint, {"spiffe://s7/node/" + uuid}, {}};
    };
    DiscoveryConfig collisions{{QStringLiteral("wss://genuine"), QStringLiteral("wss://impostor"),
                                QStringLiteral("wss://reporter"), QStringLiteral("wss://switch"),
                                QStringLiteral("wss://peer-switch")}, {}, {}, {}};
    const auto identities = [&](const QString& endpoint, const QString& json) -> DiscoveryReply {
        const auto type = boost::json::parse(json.toStdString()).as_object().at("type").as_string();
        const auto claimed = endpoint == QStringLiteral("wss://reporter") ? "reporter" : "same";
        const auto cert = endpoint == QStringLiteral("wss://impostor")
            ? certificate("impostor", "other") : endpoint == QStringLiteral("wss://reporter")
            ? certificate("reporter", "reporter") : endpoint == QStringLiteral("wss://switch") && type == "GET_NODE_INFO"
            ? certificate("switched", "other") : endpoint == QStringLiteral("wss://peer-switch") && type == "GET_PEERS"
            ? certificate("switched", "other") : certificate("genuine", "same");
        if (type == "HELLO")
            return {QStringLiteral("{\"node_uuid\":\"%1\",\"node_name\":\"%2\"}")
                        .arg(QString::fromUtf8(claimed), endpoint == QStringLiteral("wss://genuine") ? QStringLiteral("Genuine") : QStringLiteral("Other")), {}, cert};
        if (type == "GET_NODE_INFO")
            return {QStringLiteral("{\"node\":{\"node_uuid\":\"%1\"},\"network\":{\"data_listen_url\":\"ws://data\"}}")
                        .arg(QString::fromUtf8(claimed)), {}, cert};
        if (endpoint == QStringLiteral("wss://reporter"))
            return {QStringLiteral("{\"peers\":[{\"endpoint\":\"wss://genuine\",\"node_uuid\":\"same\","
                                   "\"node_name\":\"Spoofed\",\"certificate_fingerprint_sha256\":\"spoofed\","
                                   "\"reachability\":\"offline\"}]}"), {}, cert};
        return {QStringLiteral("{\"peers\":[]}"), {}, cert};
    };
    const auto checked = NodeDiscovery::discover(collisions, cancelled, identities);
    if (checked.size() != 5 || checked.value(QStringLiteral("wss://genuine")).trustState != QStringLiteral("verified") ||
        checked.value(QStringLiteral("wss://genuine")).nodeName != QStringLiteral("Genuine") ||
        checked.value(QStringLiteral("wss://genuine")).certificateFingerprintSha256 != QStringLiteral("genuine") ||
        checked.value(QStringLiteral("wss://genuine")).reachability != QStringLiteral("online")) return 4;
    if (checked.value(QStringLiteral("wss://impostor")).trustState != QStringLiteral("identity_mismatch") ||
        checked.value(QStringLiteral("wss://impostor")).endpoint == checked.value(QStringLiteral("wss://genuine")).endpoint) return 5;
    if (checked.value(QStringLiteral("wss://switch")).trustState != QStringLiteral("identity_mismatch") ||
        !checked.value(QStringLiteral("wss://switch")).dataEndpoint.isEmpty()) return 6;
    if (checked.value(QStringLiteral("wss://peer-switch")).trustState != QStringLiteral("identity_mismatch")) return 7;
    return 0;
}
