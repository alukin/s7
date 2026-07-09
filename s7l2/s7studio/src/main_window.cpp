#include "main_window.hpp"

#include <QAction>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QSpinBox>
#include <QStatusBar>
#include <QToolBar>
#include <QVBoxLayout>
#include <QWidget>

namespace s7studio {

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
    , runtimeSchema_(makeDemoSchemaJson())
{
    runner_ = new LocalSchemaRunner(this);
    controller_ = std::make_unique<ui::ModelRunController>(runner_);

    buildActions();
    buildUi();

    connect(runner_, &LocalSchemaRunner::logMessage, this, &MainWindow::appendLog);
    connect(runner_, &LocalSchemaRunner::statusChanged, this, &MainWindow::onStatusChanged);
    connect(runner_, &LocalSchemaRunner::runCompleted, this, &MainWindow::onRunCompleted);
    connect(runner_, &LocalSchemaRunner::ticksProgress, this, &MainWindow::onTickProgress);

    appendLog("[studio] Local embedded runtime ready");
    refreshActions();
    onStatusChanged("idle");
}

void MainWindow::buildActions()
{
    runAction_ = new QAction("Run", this);
    pauseAction_ = new QAction("Pause", this);
    resumeAction_ = new QAction("Resume", this);
    stopAction_ = new QAction("Stop", this);
    resetAction_ = new QAction("Reset", this);

    connect(runAction_, &QAction::triggered, this, [this] {
        controller_->run(runtimeSchema_, dtSpin_->value(), hoursSpin_, minutesSpin_, secondsSpin_);
        refreshActions();
    });
    connect(pauseAction_, &QAction::triggered, this, [this] { controller_->pause(); });
    connect(resumeAction_, &QAction::triggered, this, [this] { controller_->resume(); });
    connect(stopAction_, &QAction::triggered, this, [this] { controller_->stop(); });
    connect(resetAction_, &QAction::triggered, this, [this] {
        controller_->reset();
        refreshActions();
    });
}

void MainWindow::buildUi()
{
    auto* toolBar = addToolBar("Runtime");
    toolBar->addAction(runAction_);
    toolBar->addAction(pauseAction_);
    toolBar->addAction(resumeAction_);
    toolBar->addAction(stopAction_);
    toolBar->addAction(resetAction_);

    auto* central = new QWidget(this);
    auto* rootLayout = new QVBoxLayout(central);

    auto* form = new QFormLayout();

    dtSpin_ = new QDoubleSpinBox(central);
    dtSpin_->setDecimals(6);
    dtSpin_->setRange(1e-6, 10.0);
    dtSpin_->setSingleStep(0.001);
    dtSpin_->setValue(0.001);

    hoursSpin_ = new QSpinBox(central);
    hoursSpin_->setRange(0, 23);

    minutesSpin_ = new QSpinBox(central);
    minutesSpin_->setRange(0, 59);
    minutesSpin_->setValue(1);

    secondsSpin_ = new QSpinBox(central);
    secondsSpin_->setRange(0, 59);

    form->addRow("dt (s)", dtSpin_);
    form->addRow("Run hours", hoursSpin_);
    form->addRow("Run minutes", minutesSpin_);
    form->addRow("Run seconds", secondsSpin_);
    rootLayout->addLayout(form);

    statusLabel_ = new QLabel("idle", central);
    progressLabel_ = new QLabel("0 / 0", central);
    rootLayout->addWidget(statusLabel_);
    rootLayout->addWidget(progressLabel_);

    logView_ = new QPlainTextEdit(central);
    logView_->setReadOnly(true);
    rootLayout->addWidget(logView_, 1);

    setCentralWidget(central);
    statusBar()->showMessage("Local embedded runtime only");
    resize(1000, 700);
    setWindowTitle("s7studio");
}

void MainWindow::appendLog(const QString& message)
{
    if (logView_) {
        logView_->appendPlainText(message);
    }
    statusBar()->showMessage(message, 5000);
}

void MainWindow::onStatusChanged(const QString& status)
{
    controller_->setStatusLabel(statusLabel_, status);
    refreshActions();
}

void MainWindow::onRunCompleted(bool success, const QString& error)
{
    if (success) {
        appendLog("[studio] run completed");
    } else if (!error.isEmpty()) {
        appendLog(QString("[studio] run failed: %1").arg(error));
    }
    refreshActions();
}

void MainWindow::onTickProgress(const long completed, const long total)
{
    if (progressLabel_) {
        progressLabel_->setText(QString("%1 / %2").arg(completed).arg(total));
    }
}

void MainWindow::refreshActions()
{
    controller_->updateActionStates(runAction_, pauseAction_, resumeAction_, stopAction_, resetAction_);
}

} // namespace s7studio
