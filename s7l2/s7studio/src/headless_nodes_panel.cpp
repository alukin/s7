#include "headless_nodes_panel.hpp"

#include <s7/transport.hpp>

#include <QCheckBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMetaObject>
#include <QMutexLocker>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QTabWidget>
#include <QTimer>
#include <QVBoxLayout>

#include <boost/json.hpp>
#include <boost/system/error_code.hpp>

#include <algorithm>
#include <thread>

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

} // namespace

HeadlessNodesPanel::HeadlessNodesPanel(QWidget* parent)
    : QWidget(parent)
{
    setupUi();
}

void HeadlessNodesPanel::setupUi()
{
    auto* root = new QVBoxLayout(this);

    auto* seedGroup = new QGroupBox(QStringLiteral("Seed peers"), this);
    auto* seedLayout = new QVBoxLayout(seedGroup);

    auto* seedInputRow = new QHBoxLayout();
    seedInput_ = new QLineEdit(seedGroup);
    seedInput_->setPlaceholderText(QStringLiteral("ws://127.0.0.1:9002"));
    addSeedButton_ = new QPushButton(QStringLiteral("Add"), seedGroup);
    removeSeedButton_ = new QPushButton(QStringLiteral("Remove"), seedGroup);
    seedInputRow->addWidget(seedInput_, 1);
    seedInputRow->addWidget(addSeedButton_);
    seedInputRow->addWidget(removeSeedButton_);
    seedLayout->addLayout(seedInputRow);

    seedList_ = new QListWidget(seedGroup);
    seedLayout->addWidget(seedList_);

    auto* tlsForm = new QFormLayout();
    caFileEdit_ = new QLineEdit(seedGroup);
    certFileEdit_ = new QLineEdit(seedGroup);
    keyFileEdit_ = new QLineEdit(seedGroup);
    caFileEdit_->setPlaceholderText(QStringLiteral("TLS CA file for wss:// peers (optional for ws://)"));
    certFileEdit_->setPlaceholderText(QStringLiteral("TLS client cert file"));
    keyFileEdit_->setPlaceholderText(QStringLiteral("TLS client key file"));
    tlsForm->addRow(QStringLiteral("CA file"), caFileEdit_);
    tlsForm->addRow(QStringLiteral("Cert file"), certFileEdit_);
    tlsForm->addRow(QStringLiteral("Key file"), keyFileEdit_);
    seedLayout->addLayout(tlsForm);

    auto* controlsRow = new QHBoxLayout();
    refreshButton_ = new QPushButton(QStringLiteral("Refresh"), seedGroup);
    autoRefreshCheck_ = new QCheckBox(QStringLiteral("Auto refresh (3s)"), seedGroup);
    controlsRow->addWidget(refreshButton_);
    controlsRow->addWidget(autoRefreshCheck_);
    controlsRow->addStretch(1);
    seedLayout->addLayout(controlsRow);

    root->addWidget(seedGroup);

    auto* trustedGroup = new QGroupBox(QStringLiteral("Trusted nodes"), this);
    auto* trustedLayout = new QVBoxLayout(trustedGroup);
    trustedList_ = new QListWidget(trustedGroup);
    trustedLayout->addWidget(trustedList_);
    root->addWidget(trustedGroup, 1);

    auto* untrustedGroup = new QGroupBox(QStringLiteral("Untrusted or unknown nodes"), this);
    auto* untrustedLayout = new QVBoxLayout(untrustedGroup);
    untrustedList_ = new QListWidget(untrustedGroup);
    untrustedLayout->addWidget(untrustedList_);
    root->addWidget(untrustedGroup, 1);

    tabs_ = new QTabWidget(this);
    networkOverview_ = new QPlainTextEdit(tabs_);
    networkOverview_->setReadOnly(true);
    tabs_->addTab(networkOverview_, QStringLiteral("Network topology"));
    root->addWidget(tabs_, 2);

    refreshTimer_ = new QTimer(this);
    refreshTimer_->setInterval(3000);

    connect(addSeedButton_, &QPushButton::clicked, this, &HeadlessNodesPanel::addSeedFromInput);
    connect(removeSeedButton_, &QPushButton::clicked, this, &HeadlessNodesPanel::removeSelectedSeed);
    connect(refreshButton_, &QPushButton::clicked, this, &HeadlessNodesPanel::refreshNow);
    connect(autoRefreshCheck_, &QCheckBox::toggled, this, &HeadlessNodesPanel::onAutoRefreshToggled);
    connect(refreshTimer_, &QTimer::timeout, this, &HeadlessNodesPanel::refreshNow);
    connect(trustedList_, &QListWidget::itemDoubleClicked, this, &HeadlessNodesPanel::onNodeItemDoubleClicked);
    connect(untrustedList_, &QListWidget::itemDoubleClicked, this, &HeadlessNodesPanel::onNodeItemDoubleClicked);
}

void HeadlessNodesPanel::addSeedFromInput()
{
    const auto endpoint = normalizeEndpoint(seedInput_->text());
    if (endpoint.isEmpty()) {
        return;
    }

    for (int i = 0; i < seedList_->count(); ++i) {
        if (seedList_->item(i)->text() == endpoint) {
            seedInput_->clear();
            return;
        }
    }

    seedList_->addItem(endpoint);
    seedInput_->clear();
    refreshNow();
}

void HeadlessNodesPanel::removeSelectedSeed()
{
    delete seedList_->takeItem(seedList_->currentRow());
    refreshNow();
}

void HeadlessNodesPanel::onAutoRefreshToggled(const bool enabled)
{
    if (enabled) {
        refreshTimer_->start();
    } else {
        refreshTimer_->stop();
    }
}

void HeadlessNodesPanel::refreshNow()
{
    {
        const QMutexLocker locker(&refreshMutex_);
        if (refreshInProgress_) {
            return;
        }
        refreshInProgress_ = true;
    }

    QStringList seeds;
    for (int i = 0; i < seedList_->count(); ++i) {
        seeds.push_back(seedList_->item(i)->text());
    }
    const auto caFile = caFileEdit_->text().trimmed();
    const auto certFile = certFileEdit_->text().trimmed();
    const auto keyFile = keyFileEdit_->text().trimmed();

    const auto requestHello = QStringLiteral("{\"type\":\"HELLO\",\"request_id\":\"studio-hello\",\"protocol_version\":1}");
    const auto requestNodeInfo = QStringLiteral("{\"type\":\"GET_NODE_INFO\",\"request_id\":\"studio-node-info\",\"protocol_version\":1}");
    const auto requestPeers = QStringLiteral("{\"type\":\"GET_PEERS\",\"request_id\":\"studio-peers\",\"protocol_version\":1,\"include_unverified\":true,\"max_entries\":256}");

    QPointer<HeadlessNodesPanel> self(this);
    std::thread([self,
                 seeds = std::move(seeds),
                 caFile,
                 certFile,
                 keyFile,
                 requestHello,
                 requestNodeInfo,
                 requestPeers]() {
        if (!self) {
            return;
        }

        auto requestJsonSync = [caFile, certFile, keyFile](const QString& endpoint,
                                                           const QString& requestJson,
                                                           QString* error) -> QString {
            try {
                s7::transport::BeastRuntimeNetEndpoint client;
                s7::transport::TlsConfig tls;
                if (endpoint.startsWith(QStringLiteral("wss://"))) {
                    tls.enabled = true;
                    tls.ca_file = caFile.toStdString();
                    tls.cert_file = certFile.toStdString();
                    tls.key_file = keyFile.toStdString();
                    if (tls.ca_file.empty() || tls.cert_file.empty() || tls.key_file.empty()) {
                        if (error) {
                            *error = QStringLiteral("Missing TLS files (CA/cert/key) for wss endpoint.");
                        }
                        return {};
                    }
                }

                client.configure_client(endpoint.toStdString(), tls);
                return QString::fromStdString(client.request_control_json(requestJson.toStdString()));
            } catch (const std::exception& ex) {
                if (error) {
                    *error = QString::fromUtf8(ex.what());
                }
                return {};
            }
        };

        QMap<QString, NodeSnapshot> discovered;
        auto addOrUpdate = [&discovered](const NodeSnapshot& node) {
            auto merged = discovered.value(node.endpoint);
            if (merged.endpoint.isEmpty()) {
                merged.endpoint = node.endpoint;
            }
            if (!node.nodeUuid.isEmpty()) merged.nodeUuid = node.nodeUuid;
            if (!node.nodeName.isEmpty()) merged.nodeName = node.nodeName;
            if (!node.trustState.isEmpty()) merged.trustState = node.trustState;
            if (!node.reachability.isEmpty()) merged.reachability = node.reachability;
            if (!node.osFamily.isEmpty()) merged.osFamily = node.osFamily;
            if (!node.architecture.isEmpty()) merged.architecture = node.architecture;
            if (!node.activeDeployment.isEmpty()) merged.activeDeployment = node.activeDeployment;
            if (!node.detailText.isEmpty()) merged.detailText = node.detailText;
            merged.discoveredBy.unite(node.discoveredBy);
            discovered.insert(node.endpoint, merged);
        };

        QStringList queue = seeds;
        QSet<QString> visited;
        while (!queue.isEmpty() && visited.size() < 128) {
            const auto endpoint = queue.takeFirst();
            if (visited.contains(endpoint)) {
                continue;
            }
            visited.insert(endpoint);

            NodeSnapshot node;
            node.endpoint = endpoint;
            node.reachability = QStringLiteral("offline");
            node.trustState = endpoint.startsWith(QStringLiteral("wss://")) ? QStringLiteral("verified") : QStringLiteral("unverified");

            QString error;
            const auto helloText = requestJsonSync(endpoint, requestHello, &error);
            if (helloText.isEmpty()) {
                node.detailText = QStringLiteral("HELLO failed: %1").arg(error);
                addOrUpdate(node);
                continue;
            }

            node.reachability = QStringLiteral("online");
            {
                boost::system::error_code ec;
                const auto parsed = boost::json::parse(helloText.toStdString(), ec);
                if (!ec && parsed.is_object()) {
                    const auto& obj = parsed.as_object();
                    node.nodeUuid = jsonString(obj, "node_uuid");
                    node.nodeName = jsonString(obj, "node_name");
                }
            }

            const auto nodeInfoText = requestJsonSync(endpoint, requestNodeInfo, &error);
            if (!nodeInfoText.isEmpty()) {
                boost::system::error_code ec;
                const auto parsed = boost::json::parse(nodeInfoText.toStdString(), ec);
                if (!ec && parsed.is_object()) {
                    const auto& object = parsed.as_object();
                    if (const auto nodeIt = object.find("node"); nodeIt != object.end() && nodeIt->value().is_object()) {
                        node.nodeUuid = jsonString(nodeIt->value().as_object(), "node_uuid");
                        node.nodeName = jsonString(nodeIt->value().as_object(), "node_name");
                    }
                    if (const auto platformIt = object.find("platform"); platformIt != object.end() && platformIt->value().is_object()) {
                        node.osFamily = jsonString(platformIt->value().as_object(), "os_family");
                        node.architecture = jsonString(platformIt->value().as_object(), "architecture");
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

            addOrUpdate(node);

            const auto peersText = requestJsonSync(endpoint, requestPeers, &error);
            if (peersText.isEmpty()) {
                continue;
            }
            boost::system::error_code ec;
            const auto parsed = boost::json::parse(peersText.toStdString(), ec);
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
                peerNode.trustState = jsonString(peerObject, "trust_state");
                if (peerNode.trustState.isEmpty()) {
                    peerNode.trustState = QStringLiteral("unverified");
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
                addOrUpdate(peerNode);

                if (!visited.contains(peerEndpoint) && !queue.contains(peerEndpoint)) {
                    queue.push_back(peerEndpoint);
                }
            }
        }

        QMetaObject::invokeMethod(self, [self, discovered = std::move(discovered)]() {
            if (!self) {
                return;
            }
            self->nodes_ = discovered;
            self->updateNodeLists();
            self->updateNetworkOverview();
            const QMutexLocker locker(&self->refreshMutex_);
            self->refreshInProgress_ = false;
        }, Qt::QueuedConnection);
    }).detach();
}

void HeadlessNodesPanel::addOrUpdateNode(const NodeSnapshot& node)
{
    auto merged = nodes_.value(node.endpoint);
    if (merged.endpoint.isEmpty()) {
        merged.endpoint = node.endpoint;
    }
    if (!node.nodeUuid.isEmpty()) {
        merged.nodeUuid = node.nodeUuid;
    }
    if (!node.nodeName.isEmpty()) {
        merged.nodeName = node.nodeName;
    }
    if (!node.trustState.isEmpty()) {
        merged.trustState = node.trustState;
    }
    if (!node.reachability.isEmpty()) {
        merged.reachability = node.reachability;
    }
    if (!node.osFamily.isEmpty()) {
        merged.osFamily = node.osFamily;
    }
    if (!node.architecture.isEmpty()) {
        merged.architecture = node.architecture;
    }
    if (!node.activeDeployment.isEmpty()) {
        merged.activeDeployment = node.activeDeployment;
    }
    if (!node.detailText.isEmpty()) {
        merged.detailText = node.detailText;
    }
    merged.discoveredBy.unite(node.discoveredBy);
    nodes_.insert(node.endpoint, merged);
}

void HeadlessNodesPanel::updateNodeLists()
{
    trustedList_->clear();
    untrustedList_->clear();

    QStringList endpoints = nodes_.keys();
    std::sort(endpoints.begin(), endpoints.end());

    for (const auto& endpoint : endpoints) {
        const auto& node = nodes_[endpoint];
        const auto name = node.nodeName.isEmpty() ? QStringLiteral("(unknown)") : node.nodeName;
        const auto uuid = node.nodeUuid.isEmpty() ? QStringLiteral("no-uuid") : node.nodeUuid;
        const auto label = QStringLiteral("%1 [%2]  %3  (%4)")
            .arg(name, uuid, endpoint, node.reachability);

        auto* item = new QListWidgetItem(label);
        item->setData(Qt::UserRole, endpoint);

        if (node.trustState == QStringLiteral("verified")) {
            trustedList_->addItem(item);
        } else {
            untrustedList_->addItem(item);
        }
    }
}

void HeadlessNodesPanel::updateNetworkOverview()
{
    QString text;
    text += QStringLiteral("Known nodes: %1\n").arg(nodes_.size());
    text += QStringLiteral("\nEdges (discovered-by):\n");

    QStringList endpoints = nodes_.keys();
    std::sort(endpoints.begin(), endpoints.end());

    for (const auto& endpoint : endpoints) {
        const auto& node = nodes_[endpoint];
        if (node.discoveredBy.isEmpty()) {
            text += QStringLiteral("- %1  (seed/manual)\n").arg(endpoint);
            continue;
        }
        for (const auto& src : node.discoveredBy) {
            text += QStringLiteral("- %1 -> %2\n").arg(src, endpoint);
        }
    }

    text += QStringLiteral("\nNodes:\n");
    for (const auto& endpoint : endpoints) {
        const auto& node = nodes_[endpoint];
        text += QStringLiteral("- %1  trust=%2  reachability=%3  os=%4/%5  deployment=%6\n")
            .arg(endpoint,
                 node.trustState,
                 node.reachability,
                 node.osFamily.isEmpty() ? QStringLiteral("?") : node.osFamily,
                 node.architecture.isEmpty() ? QStringLiteral("?") : node.architecture,
                 node.activeDeployment.isEmpty() ? QStringLiteral("none") : node.activeDeployment);
    }

    networkOverview_->setPlainText(text);
}

void HeadlessNodesPanel::openOrUpdateNodeTab(const NodeSnapshot& node)
{
    const auto tabId = QStringLiteral("node:%1").arg(node.endpoint);
    for (int i = 0; i < tabs_->count(); ++i) {
        if (tabs_->widget(i)->objectName() == tabId) {
            auto* details = qobject_cast<QPlainTextEdit*>(tabs_->widget(i));
            if (details) {
                details->setPlainText(node.detailText.isEmpty() ? QStringLiteral("No node details retrieved.") : node.detailText);
            }
            tabs_->setCurrentIndex(i);
            emit nodeActivated(node.endpoint, node.nodeUuid, node.nodeName);
            return;
        }
    }

    auto* details = new QPlainTextEdit(tabs_);
    details->setReadOnly(true);
    details->setObjectName(tabId);
    details->setPlainText(node.detailText.isEmpty() ? QStringLiteral("No node details retrieved.") : node.detailText);
    const auto title = node.nodeName.isEmpty() ? node.endpoint : node.nodeName;
    tabs_->addTab(details, title);
    tabs_->setCurrentWidget(details);
    emit nodeActivated(node.endpoint, node.nodeUuid, node.nodeName);
}

void HeadlessNodesPanel::onNodeItemDoubleClicked(QListWidgetItem* item)
{
    if (!item) {
        return;
    }
    const auto endpoint = item->data(Qt::UserRole).toString();
    if (endpoint.isEmpty() || !nodes_.contains(endpoint)) {
        return;
    }
    openOrUpdateNodeTab(nodes_[endpoint]);
}

auto HeadlessNodesPanel::requestJson(const QString& endpoint, const QString& requestJson, QString* error) -> QString
{
    try {
        s7::transport::BeastRuntimeNetEndpoint client;
        s7::transport::TlsConfig tls;
        const auto normalized = normalizeEndpoint(endpoint);
        if (normalized.startsWith(QStringLiteral("wss://"))) {
            tls.enabled = true;
            tls.ca_file = caFileEdit_->text().trimmed().toStdString();
            tls.cert_file = certFileEdit_->text().trimmed().toStdString();
            tls.key_file = keyFileEdit_->text().trimmed().toStdString();
            if (tls.ca_file.empty() || tls.cert_file.empty() || tls.key_file.empty()) {
                if (error) {
                    *error = QStringLiteral("Missing TLS files (CA/cert/key) for wss endpoint.");
                }
                return {};
            }
        }

        client.configure_client(normalized.toStdString(), tls);
        return QString::fromStdString(client.request_control_json(requestJson.toStdString()));
    } catch (const std::exception& ex) {
        if (error) {
            *error = QString::fromUtf8(ex.what());
        }
        return {};
    }
}

auto HeadlessNodesPanel::normalizeEndpoint(const QString& raw) const -> QString
{
    auto value = raw.trimmed();
    if (value.isEmpty()) {
        return {};
    }
    if (value.startsWith(QStringLiteral("ws://")) || value.startsWith(QStringLiteral("wss://"))) {
        return value;
    }
    return QStringLiteral("ws://") + value;
}

} // namespace s7studio::ui


