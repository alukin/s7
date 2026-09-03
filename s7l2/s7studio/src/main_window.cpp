#include "main_window.hpp"

#include "diagram_document.hpp"
#include "diagram_editor.hpp"
#include "json_utils.hpp"
#include "main_window_builder.hpp"
#include "scope_view_window.hpp"

#include <QAction>
#include <QActionGroup>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QCloseEvent>
#include <QSignalBlocker>
#include <QLabel>
#include <QKeySequence>
#include <QMenuBar>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QSpinBox>
#include <QStatusBar>
#include <QSplitter>
#include <QWidget>

#include <algorithm>
#include <cmath>
#include <string>

namespace s7studio {

namespace {

constexpr double kPaperA5WidthMm = 210.0;
constexpr double kPaperA5HeightMm = 148.0;
constexpr double kPaperA4WidthMm = 297.0;
constexpr double kPaperA4HeightMm = 210.0;
constexpr double kPaperA3WidthMm = 420.0;
constexpr double kPaperA3HeightMm = 297.0;
constexpr double kPaperLetterWidthMm = 279.4;
constexpr double kPaperLetterHeightMm = 215.9;

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
        if (const auto name = json_utils::stringValue(object, "name"); !name.empty()) {
            return QString::fromUtf8(name.c_str());
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

    connectRunnerSignals();
    if (diagramEditor_) {
        connect(diagramEditor_, &diagram::DiagramEditor::scopeNodeDoubleClicked,
                this,
                &MainWindow::onScopeNodeDoubleClicked);
    }

    appendLog("[studio] Local embedded runtime ready");
    loadSchemaJson(runtimeSchema_, QStringLiteral("embedded demo schema"));
    refreshActions();
    onStatusChanged("idle");
}

MainWindow::~MainWindow()
{
    closeScopeWindows();
}

void MainWindow::connectRunnerSignals()
{
    connect(runner_, &LocalSchemaRunner::logMessage, this, &MainWindow::appendLog);
    connect(runner_, &LocalSchemaRunner::statusChanged, this, &MainWindow::onStatusChanged);
    connect(runner_, &LocalSchemaRunner::runCompleted, this, &MainWindow::onRunCompleted);
    connect(runner_, &LocalSchemaRunner::ticksProgress, this, &MainWindow::onTickProgress);
    connect(runner_, &LocalSchemaRunner::scopeSample, this, &MainWindow::onScopeSample);
}

void MainWindow::buildActions()
{
    const auto actions = ui::MainWindowBuilder::createActions(this);
    openSchemaAction_ = actions.openSchemaAction;
    openDiagramAction_ = actions.openDiagramAction;
    loadDemoAction_ = actions.loadDemoAction;
    fitDiagramAction_ = actions.fitDiagramAction;
    exitAction_ = actions.exitAction;
    runAction_ = actions.runAction;
    pauseAction_ = actions.pauseAction;
    resumeAction_ = actions.resumeAction;
    stopAction_ = actions.stopAction;
    resetAction_ = actions.resetAction;

    zoomInAction_ = new QAction("Zoom In", this);
    zoomOutAction_ = new QAction("Zoom Out", this);
    resetZoomAction_ = new QAction("Actual Size", this);
    fitPageAction_ = new QAction("Fit Page", this);
    showRuntimePanelAction_ = new QAction("Show Runtime Panel", this);

    paperA5Action_ = new QAction("A5", this);
    paperA4Action_ = new QAction("A4", this);
    paperA3Action_ = new QAction("A3", this);
    paperLetterAction_ = new QAction("Letter", this);
    portraitPageAction_ = new QAction("Portrait", this);
    landscapePageAction_ = new QAction("Landscape", this);

    paperA5Action_->setCheckable(true);
    paperA4Action_->setCheckable(true);
    paperA3Action_->setCheckable(true);
    paperLetterAction_->setCheckable(true);
    portraitPageAction_->setCheckable(true);
    landscapePageAction_->setCheckable(true);
    showRuntimePanelAction_->setCheckable(true);
    showRuntimePanelAction_->setChecked(false);

    paperSizeGroup_ = new QActionGroup(this);
    paperSizeGroup_->setExclusive(true);
    paperSizeGroup_->addAction(paperA5Action_);
    paperSizeGroup_->addAction(paperA4Action_);
    paperSizeGroup_->addAction(paperA3Action_);
    paperSizeGroup_->addAction(paperLetterAction_);

    pageOrientationGroup_ = new QActionGroup(this);
    pageOrientationGroup_->setExclusive(true);
    pageOrientationGroup_->addAction(portraitPageAction_);
    pageOrientationGroup_->addAction(landscapePageAction_);

    zoomInAction_->setShortcut(QKeySequence::ZoomIn);
    zoomOutAction_->setShortcut(QKeySequence::ZoomOut);
    resetZoomAction_->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_0));
    fitPageAction_->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_9));

    connect(openSchemaAction_, &QAction::triggered, this, &MainWindow::openSchemaFile);
    connect(openDiagramAction_, &QAction::triggered, this, &MainWindow::openDiagramFile);
    connect(loadDemoAction_, &QAction::triggered, this, &MainWindow::loadDemoDiagram);
    connect(fitDiagramAction_, &QAction::triggered, this, &MainWindow::fitDiagramToView);
    connect(exitAction_, &QAction::triggered, this, &QWidget::close);

    connect(runAction_, &QAction::triggered, this, [this] {
        controller_->run(runtimeSchema_, dtSpin_->value(), selectedRunDuration());
        refreshActions();
    });
    connect(pauseAction_, &QAction::triggered, this, [this] { controller_->pause(); });
    connect(resumeAction_, &QAction::triggered, this, [this] { controller_->resume(); });
    connect(stopAction_, &QAction::triggered, this, [this] { controller_->stop(); });
    connect(resetAction_, &QAction::triggered, this, [this] {
        controller_->reset();
        refreshActions();
    });

    connect(zoomInAction_, &QAction::triggered, this, &MainWindow::zoomDiagramIn);
    connect(zoomOutAction_, &QAction::triggered, this, &MainWindow::zoomDiagramOut);
    connect(resetZoomAction_, &QAction::triggered, this, &MainWindow::resetDiagramZoom);
    connect(fitPageAction_, &QAction::triggered, this, &MainWindow::fitDiagramToView);
    connect(showRuntimePanelAction_, &QAction::toggled, this, &MainWindow::setRuntimePanelVisible);

    connect(paperA5Action_, &QAction::triggered, this, &MainWindow::setPaperA5);
    connect(paperA4Action_, &QAction::triggered, this, &MainWindow::setPaperA4);
    connect(paperA3Action_, &QAction::triggered, this, &MainWindow::setPaperA3);
    connect(paperLetterAction_, &QAction::triggered, this, &MainWindow::setPaperLetter);
    connect(portraitPageAction_, &QAction::triggered, this, &MainWindow::setPagePortrait);
    connect(landscapePageAction_, &QAction::triggered, this, &MainWindow::setPageLandscape);
}

void MainWindow::buildUi()
{
    ui::MainWindowActions actions;
    actions.openSchemaAction = openSchemaAction_;
    actions.openDiagramAction = openDiagramAction_;
    actions.loadDemoAction = loadDemoAction_;
    actions.fitDiagramAction = fitDiagramAction_;
    actions.exitAction = exitAction_;
    actions.runAction = runAction_;
    actions.pauseAction = pauseAction_;
    actions.resumeAction = resumeAction_;
    actions.stopAction = stopAction_;
    actions.resetAction = resetAction_;

    ui::MainWindowBuilder::buildMenuAndToolbar(this, actions);

    auto* viewMenu = menuBar()->addMenu("&View");
    viewMenu->addAction(zoomInAction_);
    viewMenu->addAction(zoomOutAction_);
    viewMenu->addAction(resetZoomAction_);
    viewMenu->addSeparator();
    viewMenu->addAction(fitPageAction_);
    viewMenu->addSeparator();
    viewMenu->addAction(showRuntimePanelAction_);

    auto* pageMenu = menuBar()->addMenu("&Page");
    pageMenu->addAction(paperA5Action_);
    pageMenu->addAction(paperA4Action_);
    pageMenu->addAction(paperA3Action_);
    pageMenu->addAction(paperLetterAction_);
    pageMenu->addSeparator();
    pageMenu->addAction(portraitPageAction_);
    pageMenu->addAction(landscapePageAction_);

    const auto widgets = ui::MainWindowBuilder::buildCentralWidgets(this);
    diagramEditor_ = widgets.diagramEditor;
    dtSpin_ = widgets.dtSpin;
    hoursSpin_ = widgets.hoursSpin;
    minutesSpin_ = widgets.minutesSpin;
    secondsSpin_ = widgets.secondsSpin;
    statusLabel_ = widgets.statusLabel;
    progressLabel_ = widgets.progressLabel;
    logView_ = widgets.logView;
    mainSplitter_ = widgets.splitter;
    runtimePanel_ = widgets.runtimePanel;
    setRuntimePanelVisible(false);
    syncPageActions();
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
    if (statusLabel_) {
        statusLabel_->setText(status);
    }
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

void MainWindow::onScopeNodeDoubleClicked(const QString& scopeId, const int channelCount)
{
    openScopeWindow(scopeId, channelCount);
}

void MainWindow::onScopeSample(const QString& scopeId,
                               const int channel,
                               const long /*step*/,
                               const double timeSeconds,
                               const double value)
{
    if (const auto window = scopeWindows_.value(scopeId)) {
        window->appendSample(channel, timeSeconds, value);
    }
}

void MainWindow::refreshActions()
{
    const auto state = controller_->actionState();
    if (runAction_) {
        runAction_->setEnabled(state.runEnabled);
    }
    if (pauseAction_) {
        pauseAction_->setEnabled(state.pauseEnabled);
    }
    if (resumeAction_) {
        resumeAction_->setEnabled(state.resumeEnabled);
    }
    if (stopAction_) {
        stopAction_->setEnabled(state.stopEnabled);
    }
    if (resetAction_) {
        resetAction_->setEnabled(state.resetEnabled);
    }
}

void MainWindow::zoomDiagramIn()
{
    if (diagramEditor_) {
        diagramEditor_->zoomIn();
    }
}

void MainWindow::zoomDiagramOut()
{
    if (diagramEditor_) {
        diagramEditor_->zoomOut();
    }
}

void MainWindow::resetDiagramZoom()
{
    if (diagramEditor_) {
        diagramEditor_->resetZoom();
    }
}

void MainWindow::setRuntimePanelVisible(const bool visible)
{
    if (showRuntimePanelAction_ && showRuntimePanelAction_->isChecked() != visible) {
        const QSignalBlocker blocker(showRuntimePanelAction_);
        showRuntimePanelAction_->setChecked(visible);
    }

    if (!runtimePanel_ || !mainSplitter_) {
        return;
    }

    runtimePanel_->setVisible(visible);
    if (!visible) {
        mainSplitter_->setSizes({std::max(mainSplitter_->width(), 1), 0});
        return;
    }

    const int totalWidth = std::max(mainSplitter_->width(), 900);
    const int panelWidth = std::clamp(totalWidth / 5, 220, 320);
    mainSplitter_->setSizes({std::max(totalWidth - panelWidth, 400), panelWidth});
}

void MainWindow::applyPaperSize(const double widthMm, const double heightMm)
{
    if (!diagramEditor_) {
        return;
    }

    auto pageLayout = diagramEditor_->pageLayout();
    pageLayout.widthMm = widthMm;
    pageLayout.heightMm = heightMm;
    diagramEditor_->setPageLayout(pageLayout);
    diagramEditor_->fitPageInView();
    syncPageActions();
}

void MainWindow::setPaperA5()
{
    applyPaperSize(kPaperA5WidthMm, kPaperA5HeightMm);
}

void MainWindow::setPaperA4()
{
    applyPaperSize(kPaperA4WidthMm, kPaperA4HeightMm);
}

void MainWindow::setPaperA3()
{
    applyPaperSize(kPaperA3WidthMm, kPaperA3HeightMm);
}

void MainWindow::setPaperLetter()
{
    applyPaperSize(kPaperLetterWidthMm, kPaperLetterHeightMm);
}

void MainWindow::setPagePortrait()
{
    if (!diagramEditor_) {
        return;
    }

    auto pageLayout = diagramEditor_->pageLayout();
    pageLayout.orientation = diagram::PageOrientation::Portrait;
    diagramEditor_->setPageLayout(pageLayout);
    diagramEditor_->fitPageInView();
    syncPageActions();
}

void MainWindow::setPageLandscape()
{
    if (!diagramEditor_) {
        return;
    }

    auto pageLayout = diagramEditor_->pageLayout();
    pageLayout.orientation = diagram::PageOrientation::Landscape;
    diagramEditor_->setPageLayout(pageLayout);
    diagramEditor_->fitPageInView();
    syncPageActions();
}

void MainWindow::syncPageActions()
{
    if (!diagramEditor_) {
        return;
    }

    const auto pageLayout = diagramEditor_->pageLayout();
    const double maxSide = std::max(pageLayout.widthMm, pageLayout.heightMm);
    const double minSide = std::min(pageLayout.widthMm, pageLayout.heightMm);

    auto nearlyEqual = [](const double lhs, const double rhs) {
        return std::abs(lhs - rhs) < 0.5;
    };

    paperA5Action_->setChecked(nearlyEqual(maxSide, kPaperA5WidthMm) && nearlyEqual(minSide, kPaperA5HeightMm));
    paperA4Action_->setChecked(nearlyEqual(maxSide, kPaperA4WidthMm) && nearlyEqual(minSide, kPaperA4HeightMm));
    paperA3Action_->setChecked(nearlyEqual(maxSide, kPaperA3WidthMm) && nearlyEqual(minSide, kPaperA3HeightMm));
    paperLetterAction_->setChecked(nearlyEqual(maxSide, kPaperLetterWidthMm) && nearlyEqual(minSide, kPaperLetterHeightMm));

    portraitPageAction_->setChecked(pageLayout.orientation == diagram::PageOrientation::Portrait);
    landscapePageAction_->setChecked(pageLayout.orientation == diagram::PageOrientation::Landscape);
}

void MainWindow::openScopeWindow(const QString& scopeId, const int channelCount)
{
    if (scopeId.isEmpty()) {
        return;
    }

    if (const auto existing = scopeWindows_.value(scopeId)) {
        existing->show();
        existing->raise();
        existing->activateWindow();
        return;
    }

    auto* window = new ui::ScopeViewWindow(
        scopeId,
        channelCount,
        nullptr,
        Qt::Window | Qt::WindowTitleHint | Qt::WindowSystemMenuHint | Qt::WindowMinMaxButtonsHint | Qt::WindowCloseButtonHint);
    window->setAttribute(Qt::WA_DeleteOnClose, true);
    scopeWindows_.insert(scopeId, window);
    connect(window, &QObject::destroyed, this, [this, scopeId] { scopeWindows_.remove(scopeId); });
    window->show();
    window->raise();
    window->activateWindow();
}

void MainWindow::closeScopeWindows()
{
    const auto existingWindows = scopeWindows_.values();
    scopeWindows_.clear();
    for (const auto& window : existingWindows) {
        if (window) {
            window->close();
        }
    }
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    closeScopeWindows();
    if (runner_) {
        runner_->reset();
    }
    QMainWindow::closeEvent(event);
}

auto MainWindow::selectedRunDuration() const -> ui::RunDuration
{
    ui::RunDuration duration;
    duration.hours = hoursSpin_ ? hoursSpin_->value() : 0;
    duration.minutes = minutesSpin_ ? minutesSpin_->value() : 1;
    duration.seconds = secondsSpin_ ? secondsSpin_->value() : 0;
    return duration;
}

void MainWindow::loadSchemaJson(const boost::json::value& schemaJson, const QString& sourceLabel)
{
    closeScopeWindows();

    runtimeSchema_ = schemaJson;

    if (diagramEditor_) {
        diagramEditor_->loadRuntimeSchema(runtimeSchema_);
        syncPageActions();
    }

    const auto name = schemaDisplayName(runtimeSchema_, sourceLabel);
    appendLog(QString("[studio] loaded schema: %1").arg(sourceLabel));
    statusBar()->showMessage(QString("Loaded schema: %1").arg(name), 5000);
    setWindowTitle(QString("s7studio - %1").arg(name));
}

void MainWindow::loadDiagramJson(const boost::json::value& diagramJson, const QString& sourceLabel)
{
    closeScopeWindows();

    const auto document = diagram::DiagramDocument::fromJson(diagramJson);
    runtimeSchema_ = document.runtimeSchema;

    if (dtSpin_ && document.dt > 0.0) {
        dtSpin_->setValue(document.dt);
    }

    if (diagramEditor_) {
        diagramEditor_->loadDocument(document);
        syncPageActions();
    }

    const auto name = !document.id.isEmpty() ? document.id : schemaDisplayName(runtimeSchema_, sourceLabel);
    appendLog(QString("[studio] loaded diagram: %1").arg(sourceLabel));
    statusBar()->showMessage(QString("Loaded diagram: %1").arg(name), 5000);
    setWindowTitle(QString("s7studio - %1").arg(name));
}

void MainWindow::openSchemaFile()
{
    openJsonDocument(
        QStringLiteral("Open schema or diagram"),
        QStringLiteral("JSON files (*.json);;All files (*)"),
        false,
        QStringLiteral("Open schema failed"));
}

void MainWindow::openDiagramFile()
{
    openJsonDocument(
        QStringLiteral("Open diagram"),
        QStringLiteral("Diagram or JSON files (*.json *.s7diagram);;All files (*)"),
        true,
        QStringLiteral("Open diagram failed"));
}

void MainWindow::openJsonDocument(const QString& dialogTitle,
                                  const QString& filter,
                                  const bool preferDiagram,
                                  const QString& errorTitle)
{
    const auto filePath = QFileDialog::getOpenFileName(
        this,
        dialogTitle,
        QString(),
        filter);
    if (filePath.isEmpty()) {
        return;
    }

    try {
        const auto parsed = json_utils::parseFile(filePath);
        const auto sourceLabel = QFileInfo(filePath).fileName();
        const bool diagramDocument = isDiagramDocument(parsed);
        if (diagramDocument) {
            loadDiagramJson(parsed, sourceLabel);
        } else {
            loadSchemaJson(parsed, sourceLabel);
            if (preferDiagram) {
                appendLog(QString("[studio] file '%1' is a runtime schema; showing synthesized diagram view").arg(sourceLabel));
            }
        }
    } catch (const std::exception& ex) {
        QMessageBox::critical(this, errorTitle, QString::fromUtf8(ex.what()));
        appendLog(QString("[studio] failed to open document: %1").arg(QString::fromUtf8(ex.what())));
    }
}

void MainWindow::loadDemoDiagram()
{
    loadSchemaJson(makeDemoSchemaJson(), QStringLiteral("embedded demo schema"));
}

void MainWindow::fitDiagramToView()
{
    if (diagramEditor_) {
        diagramEditor_->fitPageInView();
    }
}

} // namespace s7studio
