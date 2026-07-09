#include "main_window.hpp"

#include "diagram_document.hpp"
#include "diagram_editor.hpp"

#include <QAction>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QLabel>
#include <QMenuBar>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QSizePolicy>
#include <QSplitter>
#include <QSpinBox>
#include <QStatusBar>
#include <QToolBar>
#include <QVBoxLayout>
#include <QWidget>

#include <boost/system/error_code.hpp>

#include <string>

namespace s7studio {

namespace {

auto parseJsonFile(const QString& filePath) -> boost::json::value
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        throw std::runtime_error(QString("Failed to open '%1': %2").arg(filePath, file.errorString()).toStdString());
    }

    const QByteArray bytes = file.readAll();
    boost::system::error_code ec;
    const auto parsed = boost::json::parse(std::string_view(bytes.constData(), static_cast<std::size_t>(bytes.size())), ec);
    if (ec) {
        throw std::runtime_error(QString("Failed to parse '%1': %2").arg(filePath, QString::fromUtf8(ec.message())).toStdString());
    }

    return parsed;
}

auto isDiagramDocument(const boost::json::value& json) -> bool
{
    if (!json.is_object()) {
        return false;
    }

    const auto& object = json.as_object();
    if (const auto it = object.find("document_kind"); it != object.end() && it->value().is_string()) {
        return it->value().as_string() == "s7.diagram";
    }

    return object.contains("diagram") || object.contains("schema");
}

auto schemaDisplayName(const boost::json::value& schemaJson, const QString& fallback) -> QString
{
    if (schemaJson.is_object()) {
        const auto& object = schemaJson.as_object();
        if (const auto it = object.find("name"); it != object.end() && it->value().is_string()) {
            return QString::fromUtf8(it->value().as_string().c_str());
        }
    }

    return fallback;
}

} // namespace

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
    loadSchemaJson(runtimeSchema_, QStringLiteral("embedded demo schema"));
    refreshActions();
    onStatusChanged("idle");
}

void MainWindow::buildActions()
{
    openSchemaAction_ = new QAction("Open Schema...", this);
    openDiagramAction_ = new QAction("Open Diagram...", this);
    loadDemoAction_ = new QAction("Load Demo Diagram", this);
    fitDiagramAction_ = new QAction("Fit Diagram to View", this);
    exitAction_ = new QAction("Exit", this);

    runAction_ = new QAction("Run", this);
    pauseAction_ = new QAction("Pause", this);
    resumeAction_ = new QAction("Resume", this);
    stopAction_ = new QAction("Stop", this);
    resetAction_ = new QAction("Reset", this);

    connect(openSchemaAction_, &QAction::triggered, this, &MainWindow::openSchemaFile);
    connect(openDiagramAction_, &QAction::triggered, this, &MainWindow::openDiagramFile);
    connect(loadDemoAction_, &QAction::triggered, this, &MainWindow::loadDemoDiagram);
    connect(fitDiagramAction_, &QAction::triggered, this, &MainWindow::fitDiagramToView);
    connect(exitAction_, &QAction::triggered, this, &QWidget::close);

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
    auto* fileMenu = menuBar()->addMenu("&File");
    fileMenu->addAction(openSchemaAction_);
    fileMenu->addAction(openDiagramAction_);
    fileMenu->addAction(loadDemoAction_);
    fileMenu->addSeparator();
    fileMenu->addAction(exitAction_);

    auto* diagramMenu = menuBar()->addMenu("&Diagram");
    diagramMenu->addAction(fitDiagramAction_);

    auto* toolBar = addToolBar("Runtime");
    toolBar->addAction(openSchemaAction_);
    toolBar->addAction(openDiagramAction_);
    toolBar->addSeparator();
    toolBar->addAction(runAction_);
    toolBar->addAction(pauseAction_);
    toolBar->addAction(resumeAction_);
    toolBar->addAction(stopAction_);
    toolBar->addAction(resetAction_);

    auto* central = new QWidget(this);
    auto* rootLayout = new QVBoxLayout(central);
    auto* splitter = new QSplitter(Qt::Horizontal, central);
    splitter->setChildrenCollapsible(false);

    diagramEditor_ = new diagram::DiagramEditor(splitter);
    diagramEditor_->setMinimumWidth(760);
    splitter->addWidget(diagramEditor_);

    auto* runtimePanel = new QWidget(splitter);
    runtimePanel->setMinimumWidth(360);
    runtimePanel->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
    auto* runtimeLayout = new QVBoxLayout(runtimePanel);

    auto* form = new QFormLayout();

    dtSpin_ = new QDoubleSpinBox(runtimePanel);
    dtSpin_->setDecimals(6);
    dtSpin_->setRange(1e-6, 10.0);
    dtSpin_->setSingleStep(0.001);
    dtSpin_->setValue(0.001);

    hoursSpin_ = new QSpinBox(runtimePanel);
    hoursSpin_->setRange(0, 23);

    minutesSpin_ = new QSpinBox(runtimePanel);
    minutesSpin_->setRange(0, 59);
    minutesSpin_->setValue(1);

    secondsSpin_ = new QSpinBox(runtimePanel);
    secondsSpin_->setRange(0, 59);

    form->addRow("dt (s)", dtSpin_);
    form->addRow("Run hours", hoursSpin_);
    form->addRow("Run minutes", minutesSpin_);
    form->addRow("Run seconds", secondsSpin_);
    runtimeLayout->addLayout(form);

    statusLabel_ = new QLabel("idle", runtimePanel);
    progressLabel_ = new QLabel("0 / 0", runtimePanel);
    runtimeLayout->addWidget(statusLabel_);
    runtimeLayout->addWidget(progressLabel_);

    logView_ = new QPlainTextEdit(runtimePanel);
    logView_->setReadOnly(true);
    runtimeLayout->addWidget(logView_, 1);

    splitter->addWidget(runtimePanel);
    splitter->setStretchFactor(0, 3);
    splitter->setStretchFactor(1, 2);
    splitter->setSizes({900, 380});
    rootLayout->addWidget(splitter, 1);

    setCentralWidget(central);
    statusBar()->showMessage("Local embedded runtime with diagram editor");
    resize(1440, 900);
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

void MainWindow::loadSchemaJson(const boost::json::value& schemaJson, const QString& sourceLabel)
{
    runtimeSchema_ = schemaJson;

    if (diagramEditor_) {
        diagramEditor_->loadRuntimeSchema(runtimeSchema_);
    }

    const auto name = schemaDisplayName(runtimeSchema_, sourceLabel);
    appendLog(QString("[studio] loaded schema: %1").arg(sourceLabel));
    statusBar()->showMessage(QString("Loaded schema: %1").arg(name), 5000);
    setWindowTitle(QString("s7studio - %1").arg(name));
}

void MainWindow::loadDiagramJson(const boost::json::value& diagramJson, const QString& sourceLabel)
{
    const auto document = diagram::DiagramDocument::fromJson(diagramJson);
    runtimeSchema_ = document.runtimeSchema;

    if (dtSpin_ && document.dt > 0.0) {
        dtSpin_->setValue(document.dt);
    }

    if (diagramEditor_) {
        diagramEditor_->loadDocument(document);
    }

    const auto name = !document.id.isEmpty() ? document.id : schemaDisplayName(runtimeSchema_, sourceLabel);
    appendLog(QString("[studio] loaded diagram: %1").arg(sourceLabel));
    statusBar()->showMessage(QString("Loaded diagram: %1").arg(name), 5000);
    setWindowTitle(QString("s7studio - %1").arg(name));
}

void MainWindow::openSchemaFile()
{
    const auto filePath = QFileDialog::getOpenFileName(
        this,
        QStringLiteral("Open schema or diagram"),
        QString(),
        QStringLiteral("JSON files (*.json);;All files (*)"));
    if (filePath.isEmpty()) {
        return;
    }

    try {
        const auto parsed = parseJsonFile(filePath);
        const auto sourceLabel = QFileInfo(filePath).fileName();
        if (isDiagramDocument(parsed)) {
            loadDiagramJson(parsed, sourceLabel);
        } else {
            loadSchemaJson(parsed, sourceLabel);
        }
    } catch (const std::exception& ex) {
        QMessageBox::critical(this, QStringLiteral("Open schema failed"), QString::fromUtf8(ex.what()));
        appendLog(QString("[studio] failed to open schema: %1").arg(QString::fromUtf8(ex.what())));
    }
}

void MainWindow::openDiagramFile()
{
    const auto filePath = QFileDialog::getOpenFileName(
        this,
        QStringLiteral("Open diagram"),
        QString(),
        QStringLiteral("Diagram or JSON files (*.json *.s7diagram);;All files (*)"));
    if (filePath.isEmpty()) {
        return;
    }

    try {
        const auto parsed = parseJsonFile(filePath);
        const auto sourceLabel = QFileInfo(filePath).fileName();
        if (isDiagramDocument(parsed)) {
            loadDiagramJson(parsed, sourceLabel);
        } else {
            loadSchemaJson(parsed, sourceLabel);
            appendLog(QString("[studio] file '%1' is a runtime schema; showing synthesized diagram view").arg(sourceLabel));
        }
    } catch (const std::exception& ex) {
        QMessageBox::critical(this, QStringLiteral("Open diagram failed"), QString::fromUtf8(ex.what()));
        appendLog(QString("[studio] failed to open diagram: %1").arg(QString::fromUtf8(ex.what())));
    }
}

void MainWindow::loadDemoDiagram()
{
    loadSchemaJson(makeDemoSchemaJson(), QStringLiteral("embedded demo schema"));
}

void MainWindow::fitDiagramToView()
{
    if (diagramEditor_) {
        diagramEditor_->fitToContents();
    }
}

} // namespace s7studio
