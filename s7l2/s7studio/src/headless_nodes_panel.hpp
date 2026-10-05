#pragma once

#include <QMap>
#include <QMutex>
#include <QSet>
#include <QString>
#include <QVector>
#include <QWidget>

#include <s7/transport.hpp>

class QCheckBox;
class QLineEdit;
class QListWidget;
class QListWidgetItem;
class QPlainTextEdit;
class QPushButton;
class QTabWidget;
class QTimer;

namespace s7studio::ui {

class HeadlessNodesPanel : public QWidget
{
    Q_OBJECT

public:
    explicit HeadlessNodesPanel(QWidget* parent = nullptr);

    struct KnownNode {
        QString nodeUuid;
        QString nodeName;
        QString endpoint;
        QString trustState;
    };
    // Discovered nodes with a known node_uuid, sorted by name.
    [[nodiscard]] auto knownNodes() const -> QVector<KnownNode>;
    // The client certificate configured in the panel (enabled if a certificate is set).
    [[nodiscard]] auto clientTls() const -> s7::transport::TlsConfig;

signals:
    void nodeActivated(const QString& endpoint, const QString& nodeUuid, const QString& nodeName);

public slots:
    void refreshNow();

private slots:
    void addSeedFromInput();
    void removeSelectedSeed();
    void onNodeItemDoubleClicked(QListWidgetItem* item);
    void onAutoRefreshToggled(bool enabled);

private:
    struct NodeSnapshot {
        QString endpoint;
        QString nodeUuid;
        QString nodeName;
        QString certificateCommonName;
        QString certificateFingerprintSha256;
        QString trustState;
        QString reachability;
        QString osFamily;
        QString architecture;
        QString activeDeployment;
        QString detailText;
        QSet<QString> discoveredBy;
        bool directlyChecked = false; // trustState from contacting the node itself
    };

    void setupUi();
    void updateNodeLists();
    void updateNetworkOverview();
    void openOrUpdateNodeTab(const NodeSnapshot& node);
    void addOrUpdateNode(const NodeSnapshot& node);
    auto requestJson(const QString& endpoint, const QString& requestJson, QString* error) -> QString;
    static auto normalizeEndpoint(const QString& raw) -> QString;
    static auto nodeKeyForSnapshot(const NodeSnapshot& node) -> QString;

    QLineEdit* seedInput_ = nullptr;
    QPushButton* addSeedButton_ = nullptr;
    QPushButton* removeSeedButton_ = nullptr;
    QPushButton* refreshButton_ = nullptr;
    QCheckBox* autoRefreshCheck_ = nullptr;
    QLineEdit* caFileEdit_ = nullptr;
    QLineEdit* certFileEdit_ = nullptr;
    QLineEdit* keyFileEdit_ = nullptr;
    QListWidget* seedList_ = nullptr;
    QListWidget* trustedList_ = nullptr;
    QListWidget* untrustedList_ = nullptr;
    QTabWidget* tabs_ = nullptr;
    QPlainTextEdit* networkOverview_ = nullptr;
    QTimer* refreshTimer_ = nullptr;

    QMap<QString, NodeSnapshot> nodes_;
    bool refreshInProgress_ = false;
    mutable QMutex refreshMutex_;
};

} // namespace s7studio::ui

