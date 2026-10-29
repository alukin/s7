// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-S7-plugin-permission

#pragma once

#include <s7/transport.hpp>

#include <QMap>
#include <QSet>
#include <QString>
#include <QStringList>

#include <atomic>
#include <functional>
#include <optional>

namespace s7studio::ui {

struct NodeSnapshot
{
    QString endpoint;
    QString nodeUuid;
    QString nodeName;
    QString certificateCommonName;
    QString certificateFingerprintSha256;
    QString trustState;
    QString dataEndpoint;
    QString dataSecurity;
    QString reachability;
    QString osFamily;
    QString architecture;
    QString activeDeployment;
    QString detailText;
    QSet<QString> discoveredBy;
    bool directlyChecked = false;
};

struct DiscoveryConfig
{
    QStringList seeds;
    QString caFile;
    QString certFile;
    QString keyFile;
};

struct DiscoveryReply
{
    QString text;
    QString error;
    std::optional<s7::transport::PeerCertificate> certificate;
};

// Synchronous discovery on a worker thread. An injected requester makes
// parsing, peer merging, and certificate-identity policy testable without Qt
// widgets or a real network. Only the contacted peer's certificate can verify
// a node; JSON fields from peers are claims.
class NodeDiscovery final
{
public:
    using Request = std::function<DiscoveryReply(const QString& endpoint, const QString& requestJson)>;

    [[nodiscard]] static auto discover(const DiscoveryConfig& config, const std::atomic_bool& cancelled, Request request = {})
        -> QMap<QString, NodeSnapshot>;
};

} // namespace s7studio::ui
