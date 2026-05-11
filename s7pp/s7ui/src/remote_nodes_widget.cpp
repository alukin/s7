#include "remote_nodes_widget.hpp"

#include <QHBoxLayout>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>

RemoteNodesWidget::RemoteNodesWidget(QWidget* parent)
    : QWidget(parent)
{
    auto* layout = new QVBoxLayout(this);

    nodeList_ = new QListWidget(this);
    layout->addWidget(nodeList_);

    auto* buttons = new QHBoxLayout();
    layout->addLayout(buttons);

    refreshButton_ = new QPushButton("Refresh", this);
    connectButton_ = new QPushButton("Connect", this);
    disconnectButton_ = new QPushButton("Disconnect", this);

    buttons->addWidget(refreshButton_);
    buttons->addWidget(connectButton_);
    buttons->addWidget(disconnectButton_);

    connect(refreshButton_, &QPushButton::clicked, this, &RemoteNodesWidget::refreshNodes);
    connect(connectButton_, &QPushButton::clicked, this, &RemoteNodesWidget::connectSelected);
    connect(disconnectButton_, &QPushButton::clicked, this, &RemoteNodesWidget::disconnectSelected);

    refreshNodes();
}

void RemoteNodesWidget::refreshNodes()
{
    nodeList_->clear();
    nodeList_->addItem("local://in-process (active)");
    nodeList_->addItem("ws://127.0.0.1:5577/s7 (stub)");
    emit logMessage("[remote] Node list refreshed (skeleton data)");
}

void RemoteNodesWidget::connectSelected()
{
    if (auto* item = nodeList_->currentItem()) {
        emit logMessage(QString("[remote] Connect requested: %1 (stub)").arg(item->text()));
    }
}

void RemoteNodesWidget::disconnectSelected()
{
    if (auto* item = nodeList_->currentItem()) {
        emit logMessage(QString("[remote] Disconnect requested: %1 (stub)").arg(item->text()));
    }
}

