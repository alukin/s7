#pragma once

// Contents of the bottom tool windows: Problems, Log, Run, Deploy.

#include <QString>
#include <QWidget>

#include <vector>

class QAction;
class QLabel;
class QPlainTextEdit;
class QTreeWidget;
class QTreeWidgetItem;

namespace s7studio::ui {

struct Problem {
    enum class Severity { Error, Warning };

    Severity severity = Severity::Error;
    QString message;
    QString element;   // id of the element it is about, empty: none
    QString wireInput; // "element:port" of the input the wire it is about feeds, empty: none
};

// The diagram's problems; double-click (or Enter) on one emits `activated`.
class ProblemsPanel final : public QWidget
{
    Q_OBJECT

public:
    explicit ProblemsPanel(QWidget* parent = nullptr);

    void setProblems(std::vector<Problem> problems);
    [[nodiscard]] const std::vector<Problem>& problems() const { return problems_; }
    [[nodiscard]] int errorCount() const;
    [[nodiscard]] int warningCount() const;

Q_SIGNALS:
    void activated(const s7studio::ui::Problem& problem);

private:
    QLabel* summary_;
    QTreeWidget* list_;
    std::vector<Problem> problems_;
};

class LogPanel final : public QWidget
{
    Q_OBJECT

public:
    explicit LogPanel(QWidget* parent = nullptr);
    void append(const QString& line);

private:
    QPlainTextEdit* text_;
};

// Run status, progress and the messages of the current run, with the run actions.
class RunPanel final : public QWidget
{
    Q_OBJECT

public:
    RunPanel(const std::vector<QAction*>& actions, QWidget* parent = nullptr);
    void setStatus(const QString& status);
    void setProgress(const QString& progress);
    void append(const QString& line);
    void clearOutput();

private:
    QLabel* status_;
    QLabel* progress_;
    QPlainTextEdit* output_;
};

// The outcome of the last deployment, node by node, with the deploy actions.
class DeployPanel final : public QWidget
{
    Q_OBJECT

public:
    struct NodeRow {
        QString node;
        QString endpoint;
        QString status;
        QString message;
    };

    DeployPanel(const std::vector<QAction*>& actions, QWidget* parent = nullptr);
    void setRunning(const QString& summary);
    void setResult(const QString& summary, bool ok, const std::vector<NodeRow>& nodes, const std::vector<QString>& errors);

private:
    QLabel* summary_;
    QTreeWidget* nodes_;
};

} // namespace s7studio::ui
