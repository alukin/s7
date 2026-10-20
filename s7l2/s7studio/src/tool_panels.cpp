#include "tool_panels.hpp"

#include "ui_icons.hpp"

#include <QAction>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPlainTextEdit>
#include <QToolBar>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <algorithm>

namespace s7studio::ui {

namespace {

const QColor kError(0xc6, 0x28, 0x28);
const QColor kWarning(0xc7, 0x7c, 0x02);
const QColor kOk(0x2e, 0x7d, 0x32);

// A tool window body: a vertical action bar on the left (as in the JetBrains
// IDEs), the content on the right.
auto withActionBar(QWidget* panel, const std::vector<QAction*>& actions, QWidget* content) -> void
{
    auto* layout = new QHBoxLayout(panel);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    if (!actions.empty()) {
        auto* bar = new QToolBar(panel);
        bar->setOrientation(Qt::Vertical);
        bar->setIconSize(QSize(16, 16));
        bar->setToolButtonStyle(Qt::ToolButtonIconOnly);
        for (auto* action : actions) {
            if (action) {
                bar->addAction(action);
            } else {
                bar->addSeparator();
            }
        }
        layout->addWidget(bar);
    }
    layout->addWidget(content, 1);
}

auto outputView(QWidget* parent) -> QPlainTextEdit*
{
    auto* text = new QPlainTextEdit(parent);
    text->setReadOnly(true);
    text->setMaximumBlockCount(5000);
    text->setLineWrapMode(QPlainTextEdit::NoWrap);
    text->setFont(QFont(QStringLiteral("Monospace")));
    return text;
}

} // namespace

// ProblemsPanel

ProblemsPanel::ProblemsPanel(QWidget* parent)
    : QWidget(parent)
    , summary_(new QLabel(this))
    , list_(new QTreeWidget(this))
{
    list_->setColumnCount(2);
    list_->setHeaderLabels({QStringLiteral("Problem"), QStringLiteral("Where")});
    list_->setRootIsDecorated(false);
    list_->setUniformRowHeights(true);
    list_->header()->setStretchLastSection(false);
    list_->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    list_->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    connect(list_, &QTreeWidget::itemActivated, this, [this](QTreeWidgetItem* item) {
        const auto index = list_->indexOfTopLevelItem(item);
        if (index >= 0 && index < static_cast<int>(problems_.size())) {
            Q_EMIT activated(problems_[static_cast<std::size_t>(index)]);
        }
    });

    auto* content = new QWidget(this);
    auto* layout = new QVBoxLayout(content);
    layout->setContentsMargins(6, 4, 0, 0);
    layout->addWidget(summary_);
    layout->addWidget(list_, 1);
    withActionBar(this, {}, content);
    setProblems({});
}

void ProblemsPanel::setProblems(std::vector<Problem> problems)
{
    problems_ = std::move(problems);
    std::stable_sort(problems_.begin(), problems_.end(), [](const Problem& a, const Problem& b) { return a.severity < b.severity; });
    list_->clear();
    const auto error = icon(QStringLiteral("circle-x"), kError);
    const auto warning = icon(QStringLiteral("alert-triangle"), kWarning);
    for (const auto& problem : problems_) {
        auto* item = new QTreeWidgetItem(list_);
        item->setIcon(0, problem.severity == Problem::Severity::Error ? error : warning);
        item->setText(0, problem.message);
        item->setToolTip(0, problem.message);
        item->setText(1, !problem.element.isEmpty() ? problem.element : problem.wireInput.isEmpty() ? QString() : QStringLiteral("wire → %1").arg(problem.wireInput));
    }
    const int errors = errorCount();
    const int warnings = warningCount();
    if (problems_.empty()) {
        summary_->setText(QStringLiteral("No problems: the diagram can run and be deployed."));
    } else {
        summary_->setText(QStringLiteral("%1 error(s), %2 warning(s)%3. Double-click a problem to show it.")
                              .arg(errors)
                              .arg(warnings)
                              .arg(errors > 0 ? QStringLiteral(": the diagram cannot run or be deployed") : QString()));
    }
}

int ProblemsPanel::errorCount() const
{
    return static_cast<int>(std::ranges::count(problems_, Problem::Severity::Error, &Problem::severity));
}

int ProblemsPanel::warningCount() const
{
    return static_cast<int>(std::ranges::count(problems_, Problem::Severity::Warning, &Problem::severity));
}

// LogPanel

LogPanel::LogPanel(QWidget* parent)
    : QWidget(parent)
    , text_(outputView(this))
{
    auto* clear = new QAction(icon(QStringLiteral("clear-all")), QStringLiteral("Clear Log"), this);
    connect(clear, &QAction::triggered, text_, &QPlainTextEdit::clear);
    withActionBar(this, {clear}, text_);
}

void LogPanel::append(const QString& line)
{
    text_->appendPlainText(line);
}

// RunPanel

RunPanel::RunPanel(const std::vector<QAction*>& actions, QWidget* parent)
    : QWidget(parent)
    , status_(new QLabel(QStringLiteral("idle"), this))
    , configuration_(new QLabel(this))
    , progress_(new QLabel(this))
    , output_(outputView(this))
{
    auto* content = new QWidget(this);
    auto* layout = new QVBoxLayout(content);
    layout->setContentsMargins(6, 4, 0, 0);
    auto* line = new QHBoxLayout();
    line->addWidget(new QLabel(QStringLiteral("Status:"), content));
    line->addWidget(status_);
    line->addSpacing(16);
    line->addWidget(new QLabel(QStringLiteral("Steps:"), content));
    line->addWidget(progress_);
    line->addStretch(1);
    layout->addLayout(line);
    configuration_->setObjectName(QStringLiteral("run.activeConfiguration"));
    status_->setObjectName(QStringLiteral("run.status"));
    configuration_->setWordWrap(true);
    layout->addWidget(configuration_);
    layout->addWidget(output_, 1);
    withActionBar(this, actions, content);
}

void RunPanel::setStatus(const QString& status)
{
    status_->setText(status);
}

void RunPanel::setConfigurationSummary(const QString& summary)
{
    configuration_->setText(summary);
    configuration_->setVisible(!summary.isEmpty());
}

void RunPanel::setProgress(const QString& progress)
{
    progress_->setText(progress);
}

void RunPanel::append(const QString& line)
{
    output_->appendPlainText(line);
}

void RunPanel::clearOutput()
{
    output_->clear();
}

// DeployPanel

DeployPanel::DeployPanel(const std::vector<QAction*>& actions, QWidget* parent)
    : QWidget(parent)
    , summary_(new QLabel(QStringLiteral("Not deployed in this session."), this))
    , nodes_(new QTreeWidget(this))
{
    nodes_->setColumnCount(4);
    nodes_->setHeaderLabels({QStringLiteral("Node"), QStringLiteral("Endpoint"), QStringLiteral("Status"), QStringLiteral("Message")});
    nodes_->setRootIsDecorated(false);
    nodes_->header()->setStretchLastSection(true);
    auto* content = new QWidget(this);
    auto* layout = new QVBoxLayout(content);
    layout->setContentsMargins(6, 4, 0, 0);
    summary_->setWordWrap(true);
    layout->addWidget(summary_);
    layout->addWidget(nodes_, 1);
    withActionBar(this, actions, content);
}

void DeployPanel::setRunning(const QString& summary)
{
    summary_->setText(summary);
    nodes_->clear();
}

void DeployPanel::setResult(const QString& summary, const bool ok, const std::vector<NodeRow>& nodes, const std::vector<QString>& errors)
{
    summary_->setText(summary);
    nodes_->clear();
    const auto good = icon(QStringLiteral("circle-check"), kOk);
    const auto bad = icon(QStringLiteral("circle-x"), kError);
    for (const auto& row : nodes) {
        auto* item = new QTreeWidgetItem(nodes_, QStringList{row.node, row.endpoint, row.status, row.message});
        item->setIcon(0, ok ? good : bad);
        item->setToolTip(3, row.message);
    }
    for (const auto& error : errors) {
        auto* item = new QTreeWidgetItem(nodes_, QStringList{QStringLiteral("error"), QString(), QString(), error});
        item->setIcon(0, bad);
        item->setToolTip(3, error);
    }
    for (int column = 0; column < 3; ++column) {
        nodes_->resizeColumnToContents(column);
    }
}

// ProbesPanel

ProbesPanel::ProbesPanel(QWidget* parent)
    : QWidget(parent)
    , list_(new QTreeWidget(this))
{
    list_->setColumnCount(3);
    list_->setHeaderLabels({QStringLiteral("Probe"), QStringLiteral("Taps"), QStringLiteral("Unit")});
    list_->setRootIsDecorated(false);
    list_->setUniformRowHeights(true);
    list_->setContextMenuPolicy(Qt::CustomContextMenu);
    list_->header()->setStretchLastSection(false);
    list_->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    list_->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    list_->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);

    connect(list_, &QTreeWidget::itemActivated, this, [this](QTreeWidgetItem* item) {
        const auto index = list_->indexOfTopLevelItem(item);
        if (index >= 0 && index < static_cast<int>(probes_.size())) {
            Q_EMIT openScopeRequested(probes_[static_cast<std::size_t>(index)].name);
        }
    });
    connect(list_, &QTreeWidget::customContextMenuRequested, this, [this](const QPoint& pos) {
        auto* item = list_->itemAt(pos);
        const auto index = item ? list_->indexOfTopLevelItem(item) : -1;
        if (index < 0 || index >= static_cast<int>(probes_.size())) {
            return;
        }
        const auto& probe = probes_[static_cast<std::size_t>(index)];

        QMenu menu(list_);
        auto* openScope = menu.addAction(QStringLiteral("Open Scope"));
        QMenu* phasePortraitMenu = nullptr;
        if (probes_.size() > 1) {
            phasePortraitMenu = menu.addMenu(QStringLiteral("Open Phase Portrait vs"));
            for (const auto& other : probes_) {
                if (other.name != probe.name) {
                    phasePortraitMenu->addAction(other.name);
                }
            }
        }
        menu.addSeparator();
        auto* rename = menu.addAction(QStringLiteral("Rename..."));
        auto* remove = menu.addAction(QStringLiteral("Delete"));

        const auto* chosen = menu.exec(list_->viewport()->mapToGlobal(pos));
        if (!chosen) {
            return;
        }
        if (chosen == openScope) {
            Q_EMIT openScopeRequested(probe.name);
        } else if (chosen == rename) {
            bool accepted = false;
            const auto name = QInputDialog::getText(this, QStringLiteral("Rename Probe"), QStringLiteral("Name"),
                                                     QLineEdit::Normal, probe.name, &accepted);
            if (accepted && !name.isEmpty()) {
                Q_EMIT renameRequested(probe.name, name);
            }
        } else if (chosen == remove) {
            Q_EMIT deleteRequested(probe.name);
        } else if (phasePortraitMenu && phasePortraitMenu->actions().contains(const_cast<QAction*>(chosen))) {
            Q_EMIT openPhasePortraitRequested(probe.name, chosen->text());
        }
    });

    auto* content = new QWidget(this);
    auto* layout = new QVBoxLayout(content);
    layout->setContentsMargins(6, 4, 0, 0);
    layout->addWidget(list_, 1);
    withActionBar(this, {}, content);
}

void ProbesPanel::setProbes(std::vector<ProbeRow> probes)
{
    probes_ = std::move(probes);
    list_->clear();
    for (const auto& probe : probes_) {
        auto* item = new QTreeWidgetItem(list_);
        item->setText(0, probe.name);
        item->setText(1, QStringLiteral("%1.%2").arg(QString::fromStdString(probe.element)).arg(probe.port));
        item->setText(2, probe.unit);
    }
}

} // namespace s7studio::ui
