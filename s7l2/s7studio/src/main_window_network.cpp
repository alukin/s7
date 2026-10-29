// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-S7-plugin-permission

#include "main_window.hpp"

#include "deployment_support.hpp"
#include "diagram/diagram_scene.hpp"
#include "diagram_editor.hpp"
#include "headless_nodes_panel.hpp"
#include "json_utils.hpp"
#include "schema_tabs.hpp"
#include "tool_windows.hpp"

#include <s7/deployment_client.hpp>

#include <QAction>
#include <QInputDialog>
#include <QMessageBox>
#include <QStatusBar>

#include <memory>
#include <utility>

namespace s7studio {

auto MainWindow::startNetworkTask(std::function<void()> work, std::function<void()> completed) -> bool
{
    if (networkTask_.busy()) {
        QMessageBox::information(this, QStringLiteral("Busy"), QStringLiteral("A network request is still running."));
        return false;
    }
    deployAction_->setEnabled(false);
    openDeployedAction_->setEnabled(false);
    try {
        return networkTask_.start(
            std::move(work),
            [this, completed = std::move(completed)] {
                deployAction_->setEnabled(true);
                openDeployedAction_->setEnabled(true);
                if (!shuttingDown_ && completed)
                    completed();
            },
            [this](const QString& error) {
                deployAction_->setEnabled(true);
                openDeployedAction_->setEnabled(true);
                if (!shuttingDown_)
                    appendLog(QStringLiteral("[network] request failed: %1").arg(error));
            });
    } catch (const std::exception& ex) {
        deployAction_->setEnabled(true);
        openDeployedAction_->setEnabled(true);
        appendLog(QStringLiteral("[network] cannot start request: %1").arg(QString::fromUtf8(ex.what())));
        return false;
    }
}

void MainWindow::deployToNodes()
{
    if (schemaTabs_ && !schemaTabs_->commitAll()) {
        appendLog(QStringLiteral("[studio] Apply or undo the pending schema-tab edits before continuing."));
        return;
    }

    if (!diagramEditor_ || !diagramEditor_->scene() || !headlessNodesPanel_) {
        return;
    }
    refreshWireTypeLabels();
    if (const auto problem = diagramProblem(); !problem.isEmpty()) {
        statusBar()->showMessage(QStringLiteral("The diagram cannot be deployed: %1").arg(problem), 8000);
        toolWindows_->show(problemsDock_);
        return;
    }
    QMap<QString, diagram::NodeAddress> known;
    for (const auto& node : headlessNodesPanel_->knownNodes()) {
        known[node.nodeUuid] = diagram::NodeAddress{node.nodeName, node.endpoint, node.dataEndpoint, node.dataSecurity};
    }
    boost::json::object deployment;
    s7::deployment::OutputTypeLookup types;
    try {
        deployment = diagram::deploymentDiagram(diagram::DiagramDocument::fromJson(currentDocument()), known);
        if (s7::deployment::placed_nodes(deployment).empty()) {
            QMessageBox::information(this, QStringLiteral("Deploy"),
                                     QStringLiteral("No element is placed on a node. Choose a node in each element's properties "
                                                    "(nodes come from the Headless Nodes tool window)."));
            return;
        }
        types = diagram::studioOutputTypes(deployment);
    } catch (const std::exception& ex) {
        QMessageBox::warning(this, QStringLiteral("Deploy"), QStringLiteral("The diagram cannot be deployed: %1").arg(ex.what()));
        return;
    }
    const auto tls = headlessNodesPanel_->clientTls();
    if (!tls.enabled) {
        QMessageBox::warning(this, QStringLiteral("Deploy"),
                             QStringLiteral("Deployments need the Studio certificate: set CA, certificate and key in "
                                            "the Headless Nodes tool window."));
        return;
    }
    appendLog(QStringLiteral("[deploy] deploying to %1 node(s)...").arg(s7::deployment::placed_nodes(deployment).size()));
    deployPanel_->setRunning(QStringLiteral("Deploying to %1 node(s)...").arg(s7::deployment::placed_nodes(deployment).size()));
    toolWindows_->show(deployDock_);
    auto result = std::make_shared<s7::deployment::DeployResult>();
    startNetworkTask(
        [result, deployment, types, tls] {
            try {
                s7::deployment::BeastControlRequester requester(tls);
                *result = s7::deployment::deploy_diagram(deployment, types,
                                                         [&](const auto& node, const auto& message) { return requester(node, message); });
            } catch (const std::exception& ex) {
                result->errors.push_back(ex.what());
            }
        },
        [this, result] {
            const auto& resultValue = *result;
            QStringList lines;
            for (const auto& node : resultValue.nodes) {
                lines << QStringLiteral("%1 %2: %3%4")
                             .arg(QString::fromStdString(node.node_uuid).left(8), QString::fromStdString(node.endpoint),
                                  QString::fromStdString(node.status),
                                  node.message.empty() ? QString() : QStringLiteral(" — ") + QString::fromStdString(node.message));
            }
            for (const auto& error : resultValue.errors) {
                appendLog(QStringLiteral("[deploy] error: %1").arg(QString::fromStdString(error)));
            }
            for (const auto& line : lines) {
                appendLog(QStringLiteral("[deploy] ") + line);
            }
            if (!resultValue.diagram.empty()) {
                // Keep the streams so the next deployment continues them.
                document_.setDeployment(resultValue.diagram.at("runtime_nodes").as_array(), resultValue.diagram.at("streams").as_array());
                refreshPlacementLabels();
            }
            std::vector<ui::DeployPanel::NodeRow> rows;
            QMap<QString, QString> names;
            if (headlessNodesPanel_) {
                for (const auto& known : headlessNodesPanel_->knownNodes()) {
                    names[known.nodeUuid] = known.nodeName;
                }
            }
            for (const auto& node : resultValue.nodes) {
                const auto uuid = QString::fromStdString(node.node_uuid);
                rows.push_back({names.value(uuid, uuid.left(8)), QString::fromStdString(node.endpoint), QString::fromStdString(node.status),
                                QString::fromStdString(node.message)});
            }
            std::vector<QString> errors;
            for (const auto& error : resultValue.errors) {
                errors.push_back(QString::fromStdString(error));
            }
            if (resultValue.committed) {
                appendLog(QStringLiteral("[deploy] committed %1 revision %2")
                              .arg(QString::fromStdString(resultValue.deployment_id))
                              .arg(resultValue.revision));
                statusBar()->showMessage(
                    QStringLiteral("Deployed revision %1 to %2 node(s)").arg(resultValue.revision).arg(resultValue.nodes.size()), 8000);
                deployPanel_->setResult(QStringLiteral("Deployed revision %1 of %2 to %3 node(s).")
                                            .arg(resultValue.revision)
                                            .arg(QString::fromStdString(resultValue.deployment_id))
                                            .arg(resultValue.nodes.size()),
                                        true, rows, errors);
            } else {
                deployPanel_->setResult(QStringLiteral("Deployment failed: nothing was committed."), false, rows, errors);
                toolWindows_->show(deployDock_);
            }
        });
}

void MainWindow::openDeployedDiagram()
{
    if (!headlessNodesPanel_) {
        return;
    }
    const auto nodes = headlessNodesPanel_->knownNodes();
    if (nodes.isEmpty()) {
        QMessageBox::information(this, QStringLiteral("Open Deployed Diagram"),
                                 QStringLiteral("No node is known yet. Add a seed in the Headless Nodes tool window and refresh."));
        return;
    }
    QStringList labels;
    for (const auto& node : nodes) {
        labels << QStringLiteral("%1  %2  (%3)").arg(node.nodeName, node.endpoint, node.nodeUuid.left(8));
    }
    bool accepted = false;
    const auto chosen =
        QInputDialog::getItem(this, QStringLiteral("Open Deployed Diagram"), QStringLiteral("Node"), labels, 0, false, &accepted);
    if (!accepted) {
        return;
    }
    const auto node = nodes.at(static_cast<int>(labels.indexOf(chosen)));
    const auto tls = headlessNodesPanel_->clientTls();
    struct ConfigurationReply
    {
        boost::json::object response;
        QString error;
    };
    auto reply = std::make_shared<ConfigurationReply>();
    startNetworkTask(
        [reply, node, tls] {
            try {
                s7::deployment::BeastControlRequester requester(tls);
                reply->response = requester(
                    s7::deployment::RuntimeNode{node.nodeUuid.toStdString(), {}, node.endpoint.toStdString()},
                    boost::json::object{{"type", "GET_NETWORK_CONFIGURATION"}, {"request_id", "studio-config"}, {"protocol_version", 1}});
            } catch (const std::exception& ex) {
                reply->error = QString::fromUtf8(ex.what());
            }
        },
        [this, node, reply] {
            const auto& response = reply->response;
            const auto& error = reply->error;
            if (error.isEmpty() && json_utils::stringValue(response, "type", "") != "NETWORK_CONFIGURATION") {
                QMessageBox::information(this, QStringLiteral("Open Deployed Diagram"),
                                         QStringLiteral("%1 has no deployed diagram: %2")
                                             .arg(node.nodeName, QString::fromStdString(json_utils::stringValue(response, "message", ""))));
                return;
            }
            if (!error.isEmpty() || !response.contains("diagram")) {
                QMessageBox::warning(this, QStringLiteral("Open Deployed Diagram"), QStringLiteral("Request failed: %1").arg(error));
                return;
            }
            if (!confirmDocumentReplacement())
                return;
            try {
                const auto revision = response.at("revision").to_number<std::uint64_t>();
                if (!loadDiagramJson(response.at("diagram"),
                                     QStringLiteral("deployed on %1, revision %2").arg(node.nodeName).arg(revision)))
                    return;
                document_.clearPath();
            } catch (const std::exception& ex) {
                QMessageBox::warning(this, QStringLiteral("Open Deployed Diagram"), QString::fromUtf8(ex.what()));
            }
        });
}

} // namespace s7studio
