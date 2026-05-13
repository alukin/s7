#include "main_window.hpp"

#include "model_run_controller.hpp"
#include "node_factory.hpp"
#include "palette_service.hpp"
#include "scene_roles.hpp"
#include "schema_document_controller.hpp"
#include "schema_file_service.hpp"
#include "schema_workflow_controller.hpp"
#include "schema_runner.hpp"
#include "sheet_presenter.hpp"
#include "visual_catalog.hpp"

#include "elements/sink_node_item.hpp"
#include "remote_nodes_widget.hpp"

#include <boost/json.hpp>
#include <qschematic/view.hpp>

#include <QAction>
#include <QActionGroup>
#include <QDir>
#include <QDoubleSpinBox>
#include <QDockWidget>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHeaderView>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPainter>
#include <QPushButton>
#include <QShowEvent>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTextEdit>
#include <QTimer>
#include <QToolBar>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QWidgetAction>

#include <cmath>
#include <memory>

using namespace s7ui::ui;

// ---------------------------------------------------------------------------
// MainWindow
// ---------------------------------------------------------------------------

// Destructor defined here so unique_ptr<T> deleters see complete types.
MainWindow::~MainWindow() = default;

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
{
    setDockNestingEnabled(true);

    scene_ = new QSchematic::Scene(this);
    QSchematic::Settings cadSettings;
    cadSettings.gridSize               = 5;
    cadSettings.gridPointSize          = 1;
    cadSettings.showGrid               = true;
    cadSettings.routeStraightAngles    = true;
    cadSettings.preserveStraightAngles = true;
    scene_->setSettings(cadSettings);
    scene_->setWireFactory([] { return std::make_shared<QSchematic::Items::Wire>(); });
    connect(scene_, &QSchematic::Scene::modeChanged,
            this,   &MainWindow::onSceneModeChanged);

    sheetPresenter_  = std::make_unique<SheetPresenter>(scene_);
    schemaFileService_ = std::make_unique<SchemaFileService>();
    schemaWorkflowController_ = std::make_unique<SchemaWorkflowController>(
        scene_, schemaFileService_.get());

    // Bootstrap element libraries (idempotent guard inside PaletteService).
    PaletteService::ensureLibrariesRegistered();

    schemaRunner_       = new s7ui::SchemaRunner(this);
    modelRunController_ = std::make_unique<ModelRunController>(schemaRunner_);

    connect(schemaRunner_, &s7ui::SchemaRunner::logMessage,
            this, &MainWindow::appendLog);
    connect(schemaRunner_, &s7ui::SchemaRunner::statusChanged,
            this, &MainWindow::setModelStatus);
    connect(schemaRunner_, &s7ui::SchemaRunner::runCompleted,
            this, &MainWindow::onRunCompleted);
    connect(schemaRunner_,
            qOverload<long, long>(&s7ui::SchemaRunner::ticksProgress),
            this,
            qOverload<long, long>(&MainWindow::onRunStatusPoll));

    createActions();
    createMenus();
    createToolbars();
    createCentralEditor();
    applySheetPreset(SheetPresenter::Preset::A4Landscape);
    createDocks();
    populatePalette();
    updateModelActionStates();
    setModelStatus("idle");
    updateWindowTitle();
    resize(1600, 950);

    QTimer::singleShot(0, this, [this] { sheetPresenter_->fitInView(view_); });
    appendLog("[s7ui] Ready");
}

// ---------------------------------------------------------------------------
// Actions
// ---------------------------------------------------------------------------

void MainWindow::createActions()
{
    actionOpenSchema_ = new QAction("Open Schema...", this);
    actionOpenSchema_->setShortcut(QKeySequence::Open);
    connect(actionOpenSchema_, &QAction::triggered, this, &MainWindow::loadSchema);

    actionSaveSchema_ = new QAction("Save Schema", this);
    actionSaveSchema_->setShortcut(QKeySequence::Save);
    connect(actionSaveSchema_, &QAction::triggered, this, &MainWindow::saveSchema);

    actionSaveSchemaAs_ = new QAction("Save Schema As...", this);
    actionSaveSchemaAs_->setShortcut(QKeySequence::SaveAs);
    connect(actionSaveSchemaAs_, &QAction::triggered, this, &MainWindow::saveSchemaAs);

    actionExportPdf_ = new QAction("Export PDF...", this);
    connect(actionExportPdf_, &QAction::triggered, this, &MainWindow::exportPdf);

    actionPrint_ = new QAction("Print...", this);
    actionPrint_->setShortcut(QKeySequence::Print);
    connect(actionPrint_, &QAction::triggered, this, &MainWindow::printSheet);

    actionAddNode_ = new QAction("Add Node", this);
    connect(actionAddNode_, &QAction::triggered, this, &MainWindow::addSelectedPaletteNode);

    actionFitView_ = new QAction("Fit View", this);
    connect(actionFitView_, &QAction::triggered, this,
            [this] { sheetPresenter_->fitInView(view_); });

    actionFitSelection_ = new QAction("Fit Selection", this);
    connect(actionFitSelection_, &QAction::triggered, this,
            [this] { sheetPresenter_->fitSelectionInView(view_); });

    actionZoom25_  = new QAction("25%",  this);
    actionZoom50_  = new QAction("50%",  this);
    actionZoom100_ = new QAction("100%", this);
    connect(actionZoom25_,  &QAction::triggered, this, [this]{ setZoomPreset(0.25); });
    connect(actionZoom50_,  &QAction::triggered, this, [this]{ setZoomPreset(0.50); });
    connect(actionZoom100_, &QAction::triggered, this, [this]{ setZoomPreset(1.00); });

    actionToggleWireMode_ = new QAction("Wire Mode", this);
    actionToggleWireMode_->setShortcut(Qt::CTRL | Qt::Key_W);
    actionToggleWireMode_->setCheckable(true);
    actionToggleWireMode_->setToolTip("Toggle wire drawing mode (Ctrl+W)");
    connect(actionToggleWireMode_, &QAction::triggered, this, &MainWindow::toggleWireMode);

    actionRunModel_    = new QAction("Run",    this);
    actionPauseModel_  = new QAction("Pause",  this);
    actionResumeModel_ = new QAction("Resume", this);
    actionStopModel_   = new QAction("Stop",   this);
    actionResetModel_  = new QAction("Reset",  this);
    connect(actionRunModel_,    &QAction::triggered, this, &MainWindow::runSchemaModel);
    connect(actionPauseModel_,  &QAction::triggered, this, &MainWindow::pauseSchemaModel);
    connect(actionResumeModel_, &QAction::triggered, this, &MainWindow::resumeSchemaModel);
    connect(actionStopModel_,   &QAction::triggered, this, &MainWindow::stopSchemaModel);
    connect(actionResetModel_,  &QAction::triggered, this, &MainWindow::resetSchemaModel);

    createSheetActions();
}

void MainWindow::createSheetActions()
{
    using Preset = SheetPresenter::Preset;
    auto* grp = new QActionGroup(this);
    grp->setExclusive(true);

    auto mkSheet = [&](const QString& label, Preset p, bool checked) -> QAction* {
        auto* a = new QAction(label, this);
        a->setCheckable(true);
        a->setChecked(checked);
        grp->addAction(a);
        connect(a, &QAction::triggered, this, [this, p] { applySheetPreset(p); });
        return a;
    };

    actionSheetA4_ = mkSheet("A4 landscape (297 x 210 mm)", Preset::A4Landscape, true);
    actionSheetA3_ = mkSheet("A3 landscape (420 x 297 mm)", Preset::A3Landscape, false);
    actionSheetA2_ = mkSheet("A2 landscape (594 x 420 mm)", Preset::A2Landscape, false);
    actionSheetA1_ = mkSheet("A1 landscape (841 x 594 mm)", Preset::A1Landscape, false);
}

void MainWindow::createMenus()
{
    auto* fileMenu = menuBar()->addMenu("&File");
    fileMenu->addAction(actionOpenSchema_);
    fileMenu->addAction(actionSaveSchema_);
    fileMenu->addAction(actionSaveSchemaAs_);
    fileMenu->addSeparator();
    fileMenu->addAction(actionExportPdf_);
    fileMenu->addAction(actionPrint_);

    menuBar()->addMenu("&Edit")->addAction(actionToggleWireMode_);
    menuBar()->addMenu("&Insert")->addAction(actionAddNode_);

    auto* viewMenu  = menuBar()->addMenu("&View");
    viewMenu->addAction(actionFitView_);
    auto* zoomMenu  = viewMenu->addMenu("Zoom");
    zoomMenu->addAction(actionZoom25_);
    zoomMenu->addAction(actionZoom50_);
    zoomMenu->addAction(actionZoom100_);
    auto* sheetMenu = viewMenu->addMenu("Sheet Size (mm)");
    sheetMenu->addAction(actionSheetA4_);
    sheetMenu->addAction(actionSheetA3_);
    sheetMenu->addAction(actionSheetA2_);
    sheetMenu->addAction(actionSheetA1_);

    auto* modelMenu = menuBar()->addMenu("&Model");
    modelMenu->addAction(actionRunModel_);
    modelMenu->addAction(actionPauseModel_);
    modelMenu->addAction(actionResumeModel_);
    modelMenu->addAction(actionStopModel_);
    modelMenu->addAction(actionResetModel_);
}

void MainWindow::createToolbars()
{
    auto* tb = addToolBar("Main");
    tb->addAction(actionAddNode_);
    tb->addAction(actionFitView_);
    tb->addAction(actionFitSelection_);
    tb->addSeparator();
    tb->addAction(actionZoom25_);
    tb->addAction(actionZoom50_);
    tb->addAction(actionZoom100_);
    tb->addSeparator();
    tb->addAction(actionToggleWireMode_);

    auto* mTb = addToolBar("Model");
    mTb->addAction(actionRunModel_);
    mTb->addAction(actionPauseModel_);
    mTb->addAction(actionResumeModel_);
    mTb->addAction(actionStopModel_);
    mTb->addAction(actionResetModel_);

    auto addSpin = [&](int lo, int hi, int def, const QString& tip) -> QSpinBox* {
        auto* s = new QSpinBox(mTb);
        s->setRange(lo, hi);
        s->setValue(def);
        s->setToolTip(tip);
        auto* wa = new QWidgetAction(mTb);
        wa->setDefaultWidget(s);
        mTb->addAction(wa);
        return s;
    };

    runHoursSpin_   = addSpin(0, 23, 0, "Run hours");
    runMinutesSpin_ = addSpin(0, 59, 1, "Run minutes");
    runSecondsSpin_ = addSpin(0, 59, 0, "Run seconds");

    modelStatusLabel_ = new QLabel("model: idle", mTb);
    modelStatusLabel_->setMinimumWidth(220);
    auto* statusWA = new QWidgetAction(mTb);
    statusWA->setDefaultWidget(modelStatusLabel_);
    mTb->addAction(statusWA);
}

void MainWindow::createCentralEditor()
{
    view_ = new QSchematic::View(this);
    view_->setScene(scene_);
    view_->setAlignment(Qt::AlignCenter);
    view_->setRenderHint(QPainter::Antialiasing,          true);
    view_->setRenderHint(QPainter::TextAntialiasing,      true);
    view_->setRenderHint(QPainter::SmoothPixmapTransform, true);
    view_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setCentralWidget(view_);
}

void MainWindow::createDocks()
{
    // Palette
    auto* paletteDock   = new QDockWidget("S7 Node Palette", this);
    auto* paletteHost   = new QWidget(paletteDock);
    auto* paletteLayout = new QVBoxLayout(paletteHost);
    paletteTree_ = new QTreeWidget(paletteHost);
    paletteTree_->setHeaderLabel("Node Types");
    paletteTree_->header()->setSectionResizeMode(QHeaderView::Stretch);
    connect(paletteTree_, &QTreeWidget::itemDoubleClicked,
            this, [this](QTreeWidgetItem* item, int) {
                if (item && item->childCount() == 0) addSelectedPaletteNode();
            });
    auto* addBtn = new QPushButton("Add Selected Node", paletteHost);
    connect(addBtn, &QPushButton::clicked, this, &MainWindow::addSelectedPaletteNode);
    paletteLayout->addWidget(paletteTree_);
    paletteLayout->addWidget(addBtn);
    paletteDock->setWidget(paletteHost);
    addDockWidget(Qt::RightDockWidgetArea, paletteDock);

    // Remote
    auto* remoteDock = new QDockWidget("Remote Models", this);
    remoteNodes_ = new RemoteNodesWidget(remoteDock);
    connect(remoteNodes_, &RemoteNodesWidget::logMessage, this, &MainWindow::appendLog);
    remoteDock->setWidget(remoteNodes_);
    addDockWidget(Qt::LeftDockWidgetArea, remoteDock);

    // Log
    auto* logDock = new QDockWidget("Runtime Log", this);
    logView_ = new QTextEdit(logDock);
    logView_->setReadOnly(true);
    logDock->setWidget(logView_);
    addDockWidget(Qt::BottomDockWidgetArea, logDock);

    // Schema properties
    auto* schemaDock = new QDockWidget("Schema Properties", this);
    auto* schemaHost = new QWidget(schemaDock);
    auto* schemaForm = new QFormLayout(schemaHost);
    schemaNameEdit_ = new QLineEdit(schemaName_, schemaHost);
    schemaDtSpin_   = new QDoubleSpinBox(schemaHost);
    schemaDtSpin_->setDecimals(9);
    schemaDtSpin_->setRange(1e-9, 10.0);
    schemaDtSpin_->setSingleStep(0.0001);
    schemaDtSpin_->setValue(0.001);
    schemaForm->addRow("Name", schemaNameEdit_);
    schemaForm->addRow("Sampling Interval (dt)", schemaDtSpin_);
    connect(schemaNameEdit_, &QLineEdit::editingFinished,
            this, &MainWindow::schemaPropertiesEdited);
    connect(schemaDtSpin_,   &QDoubleSpinBox::editingFinished,
            this, &MainWindow::schemaPropertiesEdited);
    schemaDock->setWidget(schemaHost);
    addDockWidget(Qt::LeftDockWidgetArea, schemaDock);

    tabifyDockWidget(schemaDock, remoteDock);
    schemaDock->raise();
}

// ---------------------------------------------------------------------------
// Palette
// ---------------------------------------------------------------------------

void MainWindow::ensureVisualCatalogLoaded()
{
    if (visualCatalog_) return;

    visualCatalog_ = std::make_unique<s7ui::visual::Catalog>();
    const QString dir = QStringLiteral(S7UI_SOURCE_DIR "/s7ui/visuals");
    visualCatalog_->loadFromDir(dir, [this](const QString& msg) { appendLog(msg); });
    nodeFactory_ = std::make_unique<NodeFactory>(visualCatalog_.get());

    schemaDocumentController_ = std::make_unique<SchemaDocumentController>(
        scene_, nodeFactory_.get(), sheetPresenter_.get(), schemaRunner_, this);
    connect(schemaDocumentController_.get(),
            &SchemaDocumentController::logged,
            this, &MainWindow::appendLog);
}

void MainWindow::populatePalette()
{
    ensureVisualCatalogLoaded();
    PaletteService::populate(paletteTree_, visualCatalog_.get());
}

// ---------------------------------------------------------------------------
// Seed
// ---------------------------------------------------------------------------

void MainWindow::seedStarterScene()
{
    const QString path =
        QStringLiteral(S7UI_SOURCE_DIR "/s7ui/resources/oscillator_agreement.json");
    try {
        if (!schemaDocumentController_) ensureVisualCatalogLoaded();
        applyLoadedSchemaResult(schemaWorkflowController_->loadFromFile(*schemaDocumentController_, path));
    }
    catch (const std::exception& ex) {
        appendLog(QString("[schema] startup load failed: %1")
                      .arg(QString::fromUtf8(ex.what())));
    }
}

// ---------------------------------------------------------------------------
// Document I/O
// ---------------------------------------------------------------------------

void MainWindow::applyLoadedSchemaResult(const LoadedSchemaResult& r)
{
    schemaName_         = r.schemaName;
    createdNodeCounter_ = r.nextNodeCounter;
    if (schemaNameEdit_) { const QSignalBlocker b(schemaNameEdit_); schemaNameEdit_->setText(schemaName_); }
    if (schemaDtSpin_)   { const QSignalBlocker b(schemaDtSpin_);   schemaDtSpin_->setValue(r.dt); }
    sheetPresenter_->fitInView(view_);
    updateModelActionStates();
    updateWindowTitle();
}

void MainWindow::loadSchema()
{
    const QString startDir = currentSchemaPath_.isEmpty()
        ? QDir::homePath()
        : QFileInfo(currentSchemaPath_).absolutePath();
    const QString path = QFileDialog::getOpenFileName(
        this, "Open Schema", startDir, "Schema Files (*.json);;All Files (*)");
    if (path.isEmpty()) return;
    if (!schemaDocumentController_) ensureVisualCatalogLoaded();
    try {
        const auto r = schemaWorkflowController_->loadFromFile(*schemaDocumentController_, path);
        currentSchemaPath_ = path;
        applyLoadedSchemaResult(r);
    }
    catch (const std::exception& ex) {
        QMessageBox::warning(this, "Open Schema Failed", QString::fromUtf8(ex.what()));
    }
}

void MainWindow::saveSchema()
{
    if (currentSchemaPath_.isEmpty()) { saveSchemaAs(); return; }
    if (saveSchemaToPath(currentSchemaPath_, false)) {
        appendLog(QString("[schema] saved to %1").arg(currentSchemaPath_));
    }
}

void MainWindow::saveSchemaAs()
{
    const QString startDir = currentSchemaPath_.isEmpty()
        ? QDir::homePath()
        : QFileInfo(currentSchemaPath_).absolutePath();
    const QString path = QFileDialog::getSaveFileName(
        this, "Save Schema As", startDir, "Schema Files (*.json);;All Files (*)");
    if (path.isEmpty()) return;
    if (saveSchemaToPath(path, true)) {
        appendLog(QString("[schema] saved to %1").arg(path));
    }
}

double MainWindow::currentDt() const
{
    return schemaDtSpin_ ? schemaDtSpin_->value() : 0.001;
}

bool MainWindow::saveSchemaToPath(const QString& path, const bool setAsCurrentPath)
{
    try {
        const auto sheet = sheetPresenter_->currentSize();
        const double dt = currentDt();
        schemaWorkflowController_->saveToFile(
            path,
            schemaName_,
            dt,
            sheetPresenter_->currentStandardName(),
            sheet.width,
            sheet.height);
        if (setAsCurrentPath) {
            currentSchemaPath_ = path;
        }
        updateWindowTitle();
        return true;
    }
    catch (const std::exception& ex) {
        QMessageBox::warning(this, "Save Schema Failed", QString::fromUtf8(ex.what()));
        return false;
    }
}

// ---------------------------------------------------------------------------
// Export / print
// ---------------------------------------------------------------------------

void MainWindow::exportPdf()
{
    const QString stem = schemaName_.trimmed().isEmpty()
                             ? QStringLiteral("sheet")
                             : schemaName_.trimmed();
    const QString startDir = currentSchemaPath_.isEmpty()
                                 ? QDir::homePath()
                                 : QFileInfo(currentSchemaPath_).absolutePath();
    const QString path = QFileDialog::getSaveFileName(
        this, "Export PDF",
        QDir(startDir).filePath(stem + ".pdf"),
        "PDF Files (*.pdf)");
    if (path.isEmpty()) return;
    sheetPresenter_->exportPdf(path, this);
    appendLog(QString("[print] exported PDF: %1").arg(path));
}

void MainWindow::printSheet()
{
    sheetPresenter_->printSheet(this);
    appendLog("[print] print job submitted");
}

// ---------------------------------------------------------------------------
// Sheet preset
// ---------------------------------------------------------------------------

void MainWindow::applySheetPreset(SheetPresenter::Preset preset)
{
    using P = SheetPresenter::Preset;
    sheetPresenter_->applyPreset(preset, [this](const QString& msg){ appendLog(msg); });
    if (actionSheetA4_) actionSheetA4_->setChecked(preset == P::A4Landscape);
    if (actionSheetA3_) actionSheetA3_->setChecked(preset == P::A3Landscape);
    if (actionSheetA2_) actionSheetA2_->setChecked(preset == P::A2Landscape);
    if (actionSheetA1_) actionSheetA1_->setChecked(preset == P::A1Landscape);
    if (view_) {
        view_->setSceneRect(scene_->sceneRect());
        sheetPresenter_->fitInView(view_);
    }
}

// ---------------------------------------------------------------------------
// Node palette
// ---------------------------------------------------------------------------

void MainWindow::addSelectedPaletteNode()
{
    if (!nodeFactory_) return;
    const auto tmpl = nodeFactory_->selectedTemplate(paletteTree_);
    const auto item = nodeFactory_->createItemFromTemplate(tmpl, createdNodeCounter_++, {});
    if (view_ && item) {
        const QPoint vpCenter = view_->viewport()->rect().center();
        QPointF sc = view_->mapToScene(vpCenter);
        sc += QPointF(static_cast<double>((createdNodeCounter_ - 1) % 5) * 20.0,
                      static_cast<double>((createdNodeCounter_ - 1) % 5) * 20.0);
        item->setScenePos(sc);
    }
    if (scene_->addItem(item)) {
        appendLog(QString("[scene] Added node: %1").arg(tmpl.typeName));
        if (view_ && item) {
            const QPointF p = item->scenePos();
            const QRectF  b = item->boundingRect();
            view_->ensureVisible(QRectF(p.x(), p.y(), b.width(), b.height()), 20, 20);
        }
    }
}

// ---------------------------------------------------------------------------
// Model run
// ---------------------------------------------------------------------------

void MainWindow::runSchemaModel()
{
    if (schemaRunner_->isRunning()) {
        appendLog("[model] Run skipped: already running");
        return;
    }
    boost::json::value rj;
    try {
        rj = schemaWorkflowController_->buildRuntimeJson(schemaName_, currentDt());
    }
    catch (const std::exception& ex) {
        appendLog(QString("[compile] %1").arg(QString::fromUtf8(ex.what())));
        setModelStatus("error: invalid diagram");
        return;
    }
    const double dt = currentDt();
    modelRunController_->run(rj, dt, runHoursSpin_, runMinutesSpin_, runSecondsSpin_);
    if (schemaRunner_->isRunning()) linkSinkProbes();
    updateModelActionStates();
}

void MainWindow::pauseSchemaModel()  { modelRunController_->pause();  updateModelActionStates(); }
void MainWindow::resumeSchemaModel() { modelRunController_->resume(); updateModelActionStates(); }
void MainWindow::stopSchemaModel()   { modelRunController_->stop(); }

void MainWindow::resetSchemaModel()
{
    unlinkSinkProbes();
    modelRunController_->reset();
    updateModelActionStates();
}

void MainWindow::onRunStatusPoll(long completed, long total)
{
    if (total > 0)
        setModelStatus(QString("running %1/%2").arg(completed).arg(total));
}

void MainWindow::onRunCompleted(bool, const QString&)
{
    updateModelActionStates();
}

// ---------------------------------------------------------------------------
// Probe linkage
// ---------------------------------------------------------------------------

void MainWindow::linkSinkProbes()
{
    for (const auto& node : scene_->nodes()) {
        auto* sink = dynamic_cast<s7ui::elements::SinkNodeItem*>(node.get());
        if (!sink) continue;
        const QString id      = node->data(kRoleNodeId).toString();
        const bool    complex = (node->data(kRoleNodeType).toString() == "ComplexScopeSink");
        if (complex)
            sink->setComplexProbe(schemaRunner_->complexProbe(id.toStdString()));
        else
            sink->setProbe(schemaRunner_->probe(id.toStdString()));
    }
}

void MainWindow::unlinkSinkProbes()
{
    for (const auto& node : scene_->nodes()) {
        if (auto* sink = dynamic_cast<s7ui::elements::SinkNodeItem*>(node.get()))
            sink->clearProbe();
    }
}

// ---------------------------------------------------------------------------
// Schema properties
// ---------------------------------------------------------------------------

void MainWindow::schemaPropertiesEdited()
{
    schemaName_ = schemaNameEdit_ ? schemaNameEdit_->text().trimmed() : schemaName_;
    if (schemaName_.isEmpty()) {
        schemaName_ = "s7ui_schema";
        if (schemaNameEdit_) schemaNameEdit_->setText(schemaName_);
    }
    sheetPresenter_->setSchemaName(schemaName_);
    updateModelActionStates();
    appendLog(QString("[schema] properties updated: name=%1 dt=%2")
                  .arg(schemaName_)
                  .arg(currentDt(), 0, 'g', 9));
}

// ---------------------------------------------------------------------------
// Misc
// ---------------------------------------------------------------------------

void MainWindow::appendLog(const QString& msg)
{
    if (logView_) logView_->append(msg);
}

void MainWindow::setZoomPreset(const double zoom)
{
    if (view_) view_->setZoomValue(std::clamp(zoom, 0.05, 8.0));
}

void MainWindow::updateModelActionStates()
{
    modelRunController_->updateActionStates(
        actionRunModel_, actionPauseModel_, actionResumeModel_,
        actionStopModel_, actionResetModel_);
}

void MainWindow::setModelStatus(const QString& status)
{
    modelRunController_->setStatusLabel(modelStatusLabel_, status);
}

void MainWindow::updateWindowTitle()
{
    setWindowTitle(QString("s7ui \u2014 %1").arg(schemaName_));
}

void MainWindow::toggleWireMode()
{
    if (!scene_) return;
    scene_->setMode(scene_->mode() == QSchematic::Scene::WireMode
                        ? QSchematic::Scene::NormalMode
                        : QSchematic::Scene::WireMode);
}

void MainWindow::onSceneModeChanged(int mode)
{
    if (actionToggleWireMode_)
        actionToggleWireMode_->setChecked(mode == QSchematic::Scene::WireMode);
}

void MainWindow::keyPressEvent(QKeyEvent* evt)
{
    if (evt->key() == Qt::Key_W && (evt->modifiers() & Qt::ControlModifier)) {
        toggleWireMode();
        evt->accept();
        return;
    }
    QMainWindow::keyPressEvent(evt);
}

void MainWindow::showEvent(QShowEvent* event)
{
    if (!initialSheetFitDone_) {
        sheetPresenter_->fitInView(view_);
        initialSheetFitDone_ = true;
    }
    QMainWindow::showEvent(event);
}

