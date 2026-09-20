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
#include <boost/asio/connect.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl/context.hpp>
#include <boost/beast/core/flat_buffer.hpp>
#include <boost/beast/core/tcp_stream.hpp>
#include <boost/beast/core/buffers_to_string.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/beast/ssl.hpp>

#include <algorithm>
#include <cstring>
#include <thread>
#include <utility>

namespace s7studio::ui {
namespace {

namespace websocket = boost::beast::websocket;
namespace ssl = boost::asio::ssl;
using Tcp = boost::asio::ip::tcp;

[[nodiscard]] auto tlsConfigForRequest(const QString& caFile,
                                       const QString& certFile,
                                       const QString& keyFile,
                                       bool enabled) -> s7::transport::TlsConfig
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

auto request_control_once(const QString& endpoint,
                         const QString& requestJson,
                         const QString& caFile,
                         const QString& certFile,
                         const QString& keyFile,
                         QString* error) -> QString
{
    try {
        const auto parsed = s7::transport::parse_ws_url(endpoint.toStdString());
        const auto host_port = parsed.host + ":" + parsed.port;
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
            return request_control_once(endpoint, requestJson, caFile, certFile, keyFile, error);
        };

        QMap<QString, NodeSnapshot> discovered;
        auto addOrUpdate = [&discovered](const NodeSnapshot& node) {
            const auto key = HeadlessNodesPanel::nodeKeyForSnapshot(node);
            if (key.isEmpty()) {
                return;
            }

            auto merged = discovered.value(key);
            if (merged.endpoint.isEmpty() || (!node.endpoint.isEmpty() && merged.endpoint == key)) {
                merged.endpoint = node.endpoint;
            }
            if (!node.nodeUuid.isEmpty()) merged.nodeUuid = node.nodeUuid;
            if (!node.nodeName.isEmpty()) merged.nodeName = node.nodeName;
            if (!node.certificateCommonName.isEmpty()) merged.certificateCommonName = node.certificateCommonName;
            if (!node.certificateFingerprintSha256.isEmpty()) merged.certificateFingerprintSha256 = node.certificateFingerprintSha256;
            if (!node.trustState.isEmpty()) merged.trustState = node.trustState;
            if (!node.reachability.isEmpty()) merged.reachability = node.reachability;
            if (!node.osFamily.isEmpty()) merged.osFamily = node.osFamily;
            if (!node.architecture.isEmpty()) merged.architecture = node.architecture;
            if (!node.activeDeployment.isEmpty()) merged.activeDeployment = node.activeDeployment;
            if (!node.detailText.isEmpty()) merged.detailText = node.detailText;
            merged.discoveredBy.unite(node.discoveredBy);
            discovered.insert(key, merged);
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
                    const auto [certificateCommonName, certificateFingerprint] = certificateInfoFromObject(obj);
                    node.certificateCommonName = certificateCommonName;
                    node.certificateFingerprintSha256 = certificateFingerprint;
                    if (node.nodeUuid.isEmpty() && !node.certificateCommonName.isEmpty()) {
                        node.nodeUuid = node.certificateCommonName;
                    }
                }
            }

            const auto nodeInfoText = requestJsonSync(endpoint, requestNodeInfo, &error);
            if (!nodeInfoText.isEmpty()) {
                boost::system::error_code ec;
                const auto parsed = boost::json::parse(nodeInfoText.toStdString(), ec);
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
                const auto [peerCertificateCommonName, peerCertificateFingerprint] = certificateInfoFromObject(peerObject);
                peerNode.certificateCommonName = peerCertificateCommonName;
                peerNode.certificateFingerprintSha256 = peerCertificateFingerprint;
                if (peerNode.nodeUuid.isEmpty() && !peerNode.certificateCommonName.isEmpty()) {
                    peerNode.nodeUuid = peerNode.certificateCommonName;
                }
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

auto HeadlessNodesPanel::requestJson(const QString& endpoint, const QString& requestJson, QString* error) -> QString
{
    return request_control_once(normalizeEndpoint(endpoint),
                                requestJson,
                                caFileEdit_->text().trimmed(),
                                certFileEdit_->text().trimmed(),
                                keyFileEdit_->text().trimmed(),
                                error);
}

auto HeadlessNodesPanel::nodeKeyForSnapshot(const NodeSnapshot& node) -> QString
{
    if (!node.nodeUuid.isEmpty()) {
        return node.nodeUuid;
    }
    if (!node.certificateFingerprintSha256.isEmpty()) {
        return node.certificateFingerprintSha256;
    }
    return node.endpoint;
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

} // namespace s7studio::ui


