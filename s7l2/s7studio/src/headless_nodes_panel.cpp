// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-S7-plugin-permission

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
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTabWidget>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <exception>
#include <utility>

namespace s7studio::ui {
namespace {

[[nodiscard]] auto shortFingerprint(const QString& fingerprint) -> QString
{
    if (fingerprint.isEmpty()) {
        return {};
    }
    if (fingerprint.size() <= 12) {
        return fingerprint;
    }
    return fingerprint.left(12) + QStringLiteral("…");
}

} // namespace

HeadlessNodesPanel::HeadlessNodesPanel(QWidget* parent)
    : QWidget(parent)
{
    setupUi();
}

HeadlessNodesPanel::~HeadlessNodesPanel()
{
    refreshCancelled_->store(true);
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
    removeSeedButton_->setEnabled(false);
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
    connect(seedList_, &QListWidget::itemSelectionChanged, this, [this]() {
        removeSeedButton_->setEnabled(seedList_->currentRow() >= 0);
    });
    connect(refreshButton_, &QPushButton::clicked, this, &HeadlessNodesPanel::refreshNow);
    connect(autoRefreshCheck_, &QCheckBox::toggled, this, &HeadlessNodesPanel::onAutoRefreshToggled);
    connect(refreshTimer_, &QTimer::timeout, this, &HeadlessNodesPanel::refreshNow);
    connect(trustedList_, &QListWidget::itemDoubleClicked, this, &HeadlessNodesPanel::onNodeItemDoubleClicked);
    connect(untrustedList_, &QListWidget::itemDoubleClicked, this, &HeadlessNodesPanel::onNodeItemDoubleClicked);
}

void HeadlessNodesPanel::addSeedFromInput()
{
    const auto endpoint = normalizeEndpoint(seedInput_->text());
    seedInput_->clear();
    if (endpoint.isEmpty()) {
        return;
    }
    for (int i = 0; i < seedList_->count(); ++i) {
        if (seedList_->item(i)->text() == endpoint) {
            return;
        }
    }
    addSeed(endpoint);
    refreshNow();
}

void HeadlessNodesPanel::addSeed(const QString& endpoint)
{
    const auto normalized = normalizeEndpoint(endpoint);
    for (int i = 0; i < seedList_->count(); ++i) {
        if (seedList_->item(i)->text() == normalized) {
            return;
        }
    }
    if (!normalized.isEmpty()) {
        seedList_->addItem(normalized);
    }
}

void HeadlessNodesPanel::setClientCertificate(const QString& caFile, const QString& certFile, const QString& keyFile)
{
    caFileEdit_->setText(caFile);
    certFileEdit_->setText(certFile);
    keyFileEdit_->setText(keyFile);
}

void HeadlessNodesPanel::removeSelectedSeed()
{
    const auto selectedRow = seedList_->currentRow();
    if (selectedRow < 0) {
        return;
    }
    delete seedList_->takeItem(selectedRow);
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
    if (refreshTask_.busy()) return;

    DiscoveryConfig config;
    for (int i = 0; i < seedList_->count(); ++i) config.seeds.push_back(seedList_->item(i)->text());
    config.caFile = caFileEdit_->text().trimmed();
    config.certFile = certFileEdit_->text().trimmed();
    config.keyFile = keyFileEdit_->text().trimmed();

    const auto cancelled = refreshCancelled_;
    auto result = std::make_shared<QMap<QString, NodeSnapshot>>();
    try {
        refreshTask_.start([cancelled, result, config = std::move(config)] {
            *result = NodeDiscovery::discover(config, *cancelled);
        }, [this, result] {
            nodes_ = std::move(*result);
            updateNodeLists();
            updateNetworkOverview();
            emit refreshed();
        }, [this](const QString& error) {
            networkOverview_->setPlainText(QStringLiteral("Discovery failed: %1").arg(error));
            emit refreshed();
        });
    } catch (const std::exception& ex) {
        networkOverview_->setPlainText(QStringLiteral("Discovery failed: %1").arg(QString::fromUtf8(ex.what())));
        emit refreshed();
    }
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
    if (!node.trustState.isEmpty() && (node.directlyChecked || !merged.directlyChecked)) {
        merged.trustState = node.trustState;
        merged.directlyChecked = merged.directlyChecked || node.directlyChecked;
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

    QStringList keys = nodes_.keys();
    std::sort(keys.begin(), keys.end());

    for (const auto& key : keys) {
        const auto& node = nodes_[key];
        const auto name = node.nodeName.isEmpty() ? QStringLiteral("(unknown)") : node.nodeName;
        const auto uuid = node.nodeUuid.isEmpty() ? QStringLiteral("no-uuid") : node.nodeUuid;
        const auto endpoint = node.endpoint.isEmpty() ? key : node.endpoint;
        const auto certName = node.certificateCommonName.isEmpty() ? QStringLiteral("no-cert") : node.certificateCommonName;
        const auto fingerprint = shortFingerprint(node.certificateFingerprintSha256);
        const auto label = fingerprint.isEmpty()
            ? QStringLiteral("%1 [%2]  %3  (%4) cert=%5")
                  .arg(name, uuid, endpoint, node.reachability, certName)
            : QStringLiteral("%1 [%2]  %3  (%4) cert=%5 fp=%6")
                  .arg(name, uuid, endpoint, node.reachability, certName, fingerprint);

        auto* item = new QListWidgetItem(label);
        item->setData(Qt::UserRole, key);

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

    QStringList keys = nodes_.keys();
    std::sort(keys.begin(), keys.end());

    for (const auto& key : keys) {
        const auto& node = nodes_[key];
        const auto endpoint = node.endpoint.isEmpty() ? key : node.endpoint;
        if (node.discoveredBy.isEmpty()) {
            text += QStringLiteral("- %1  (seed/manual)\n").arg(endpoint);
            continue;
        }
        for (const auto& src : node.discoveredBy) {
            text += QStringLiteral("- %1 -> %2\n").arg(src, endpoint);
        }
    }

    text += QStringLiteral("\nNodes:\n");
    for (const auto& key : keys) {
        const auto& node = nodes_[key];
        const auto endpoint = node.endpoint.isEmpty() ? key : node.endpoint;
        const auto certName = node.certificateCommonName.isEmpty() ? QStringLiteral("no-cert") : node.certificateCommonName;
        const auto fingerprint = shortFingerprint(node.certificateFingerprintSha256);
        text += QStringLiteral("- %1  uuid=%2  trust=%3  reachability=%4  cert=%5  fp=%6  os=%7/%8  deployment=%9\n")
            .arg(endpoint,
                 node.nodeUuid.isEmpty() ? QStringLiteral("no-uuid") : node.nodeUuid,
                 node.trustState,
                 node.reachability,
                 certName,
                 fingerprint.isEmpty() ? QStringLiteral("none") : fingerprint,
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
    const auto key = item->data(Qt::UserRole).toString();
    if (key.isEmpty() || !nodes_.contains(key)) {
        return;
    }
    openOrUpdateNodeTab(nodes_[key]);
}

auto HeadlessNodesPanel::normalizeEndpoint(const QString& raw) -> QString
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

auto HeadlessNodesPanel::knownNodes() const -> QVector<KnownNode>
{
    QVector<KnownNode> result;
    for (const auto& node : nodes_) {
        if (!node.nodeUuid.isEmpty() && !node.endpoint.isEmpty()) {
            const bool verified = node.directlyChecked && node.trustState == QStringLiteral("verified");
            result.push_back(KnownNode{node.nodeUuid, node.nodeName, node.endpoint, node.trustState,
                                      verified ? node.dataEndpoint : QString{}, verified ? node.dataSecurity : QString{}});
        }
    }
    std::sort(result.begin(), result.end(), [](const KnownNode& a, const KnownNode& b) {
        return a.nodeName == b.nodeName ? a.nodeUuid < b.nodeUuid : a.nodeName < b.nodeName;
    });
    return result;
}

auto HeadlessNodesPanel::clientTls() const -> s7::transport::TlsConfig
{
    const auto cert = certFileEdit_ ? certFileEdit_->text().trimmed() : QString();
    s7::transport::TlsConfig tls;
    tls.enabled = !cert.isEmpty();
    tls.ca_file = (caFileEdit_ ? caFileEdit_->text().trimmed() : QString()).toStdString();
    tls.cert_file = cert.toStdString();
    tls.key_file = (keyFileEdit_ ? keyFileEdit_->text().trimmed() : QString()).toStdString();
    return tls;
}

} // namespace s7studio::ui


