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
    if (requests != 4 || !found.contains(QStringLiteral("seed")) || !found.contains(QStringLiteral("peer")))
        return 1;
    const auto peer = found.value(QStringLiteral("peer"));
    if (peer.trustState != QStringLiteral("unverified") || peer.directlyChecked || peer.dataEndpoint != QString{} ||
        !peer.discoveredBy.contains(QStringLiteral("ws://seed")))
        return 2;

    cancelled.store(true);
    requests = 0;
    if (!NodeDiscovery::discover(config, cancelled, request).empty() || requests != 0)
        return 3;
    return 0;
}
