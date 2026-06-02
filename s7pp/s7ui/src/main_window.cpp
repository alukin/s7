#include "main_window.hpp"

#include "model_run_controller.hpp"
#include "node_factory.hpp"
#include "palette_service.hpp"
#include "palette_tree_widget.hpp"
#include "documents/diagram_document.hpp"
#include "scene_roles.hpp"
#include "schema_document_controller.hpp"
#include "schema_file_service.hpp"
#include "schema_workflow_controller.hpp"
#include "schema_runner.hpp"
#include "sheet_presenter.hpp"
#include "visual_catalog.hpp"

#include "elements/sink_node_item.hpp"
#include "elements/s7_wire_item.hpp"
#include "remote_nodes_widget.hpp"

#include <boost/json.hpp>
#include <QApplication>
#include <QScreen>
#include <QWindow>

#include <QAction>
#include <QActionGroup>
#include <QDoubleSpinBox>
#include <QDockWidget>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHeaderView>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMenuBar>
#include <QMessageBox>
#include <QPainter>
#include <QPushButton>
#include <QShowEvent>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStatusBar>
#include <QStyle>
#include <QTextEdit>
#include <QTimer>
#include <QToolBar>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
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
    cadSettings_.gridSize               = 5;
    cadSettings_.gridPointSize          = 1;
    cadSettings_.showGrid               = true;
    cadSettings_.routeStraightAngles    = true;
    cadSettings_.preserveStraightAngles = true;
    scene_->setSettings(cadSettings_);
    scene_->setWireFactory([]() -> std::shared_ptr<QSchematic::Items::Wire> {
        return std::make_shared<s7ui::elements::S7WireItem>();
    });
    connect(scene_, &QSchematic::Scene::modeChanged,
            this,   &MainWindow::onSceneModeChanged);

    // -----------------------------------------------------------------------
    // View zoom limits — expressed in pixels-per-scene-unit (pixels per mm).
    // At 96 DPI: 1mm ≈ 3.78 px.  We allow 4% … 400% physical zoom.
    // The actual limit values are applied in createCentralEditor() once the
    // view widget exists.  Screen DPI is computed on demand in setZoomPreset().
    // -----------------------------------------------------------------------

    sheetPresenter_ = std::make_unique<SheetPresenter>(scene_);
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
    createSidebar();
    populatePalette();
    updateModelActionStates();
    setModelStatus("idle");
    updateWindowTitle();
    updateInteractionHint();
    resize(1600, 950);

    QTimer::singleShot(0, this, [this]() -> void { sheetPresenter_->fitInView(view_); });
    appendLog("[s7ui] Ready");
}

// ---------------------------------------------------------------------------
// Actions
// ---------------------------------------------------------------------------

void MainWindow::createActions()
{
    actionOpenSchema_ = new QAction("Load Diagram...", this);
    actionOpenSchema_->setShortcut(QKeySequence::Open);
    connect(actionOpenSchema_, &QAction::triggered, this, &MainWindow::loadSchema);

    actionSaveSchema_ = new QAction("Save Diagram", this);
    actionSaveSchema_->setShortcut(QKeySequence::Save);
    connect(actionSaveSchema_, &QAction::triggered, this, &MainWindow::saveSchema);

    actionSaveSchemaAs_ = new QAction("Save Diagram As...", this);
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
            [this]() -> void { sheetPresenter_->fitInView(view_); });

    actionFitSelection_ = new QAction("Fit Selection", this);
    connect(actionFitSelection_, &QAction::triggered, this,
            [this]() -> void { sheetPresenter_->fitSelectionInView(view_); });

    actionZoom25_ = new QAction("25%", this);
    actionZoom50_ = new QAction("50%", this);
    actionZoom100_ = new QAction("100%", this);
    connect(actionZoom25_, &QAction::triggered, this, [this]() -> void { setZoomPreset(0.25); });
    connect(actionZoom50_, &QAction::triggered, this, [this]() -> void { setZoomPreset(0.50); });
    connect(actionZoom100_, &QAction::triggered, this, [this]() -> void { setZoomPreset(1.00); });

    actionToggleWireMode_ = new QAction("Wire Mode", this);
    actionToggleWireMode_->setShortcut(Qt::CTRL | Qt::Key_W);
    actionToggleWireMode_->setCheckable(true);
    actionToggleWireMode_->setToolTip("Toggle wire drawing mode (Ctrl+W)");
    connect(actionToggleWireMode_, &QAction::triggered, this, &MainWindow::toggleWireMode);

    actionRunModel_ = new QAction("Run", this);
    actionPauseModel_ = new QAction("Pause", this);
    actionResumeModel_ = new QAction("Resume", this);
    actionStopModel_ = new QAction("Stop", this);
    actionResetModel_ = new QAction("Reset", this);
    connect(actionRunModel_, &QAction::triggered, this, &MainWindow::runSchemaModel);
    connect(actionPauseModel_, &QAction::triggered, this, &MainWindow::pauseSchemaModel);
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
        connect(a, &QAction::triggered, this, [this, p]() -> void { applySheetPreset(p); });
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

    auto* viewMenu = menuBar()->addMenu("&View");
    viewMenu->addAction(actionFitView_);
    auto* zoomMenu = viewMenu->addMenu("Zoom");
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

    struct SpinSpec {
        int minimum;
        int maximum;
        int initial;
        QString toolTip;
    };

    auto addSpin = [&](const SpinSpec& spec) -> QSpinBox* {
        auto* s = new QSpinBox(mTb);
        s->setRange(spec.minimum, spec.maximum);
        s->setValue(spec.initial);
        s->setToolTip(spec.toolTip);
        auto* wa = new QWidgetAction(mTb);
        wa->setDefaultWidget(s);
        mTb->addAction(wa);
        return s;
    };

    runHoursSpin_   = addSpin({.minimum = 0, .maximum = 23, .initial = 0, .toolTip = "Run hours"});
    runMinutesSpin_ = addSpin({.minimum = 0, .maximum = 59, .initial = 1, .toolTip = "Run minutes"});
    runSecondsSpin_ = addSpin({.minimum = 0, .maximum = 59, .initial = 0, .toolTip = "Run seconds"});

    modelStatusLabel_ = new QLabel(tr("model: idle"), mTb);
    modelStatusLabel_->setMinimumWidth(220);
    auto* statusWA = new QWidgetAction(mTb);
    statusWA->setDefaultWidget(modelStatusLabel_);
    mTb->addAction(statusWA);
}

void MainWindow::createCentralEditor()
{
    view_ = new s7ui::ui::S7View(this);
    view_->setScene(scene_);

    // Set zoom limits to match physical-size expectations.
    // zoom_factor_min/max in QSchematic View are view transform values (pixels/scene-unit).
    // Since 1 scene unit = 1 mm, we compute limits from screen DPI.
    // Allow 4% … 400% physical zoom.
    {
        const QScreen* screen = QApplication::primaryScreen();
        const double dpi     = screen ? screen->logicalDotsPerInchX() : 96.0;
        const double mmToPx  = dpi / 25.4;
        view_->zoom_factor_min  = 0.04 * mmToPx;   //   4% physical
        view_->zoom_factor_max  = 4.00 * mmToPx;   // 400% physical
        view_->zoom_factor_step = 0.10 * mmToPx;   //  10% physical per step
    }
    connect(view_, &s7ui::ui::S7View::additiveRubberBandDragChanged,
            this, &MainWindow::onAdditiveRubberBandDragChanged);
    view_->setAlignment(Qt::AlignCenter);
    view_->setRenderHint(QPainter::Antialiasing, true);
    view_->setRenderHint(QPainter::TextAntialiasing, true);
    view_->setRenderHint(QPainter::SmoothPixmapTransform, true);
    view_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    view_->setAcceptDrops(true);
    view_->viewport()->setAcceptDrops(true);

    // Wrap view with sidebar on the left
    auto* container = new QWidget(this);
    auto* layout = new QHBoxLayout(container);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    sidebar_ = new s7ui::ui::SidebarWidget(this);
    layout->addWidget(sidebar_);
    layout->addWidget(view_, 1);

    container->setLayout(layout);
    setCentralWidget(container);

}

void MainWindow::createSidebar()
{
    // --- Palette Panel ---
    auto* paletteHost = new QWidget();
    auto* paletteLayout = new QVBoxLayout(paletteHost);
    paletteTree_ = new PaletteTreeWidget(paletteHost);
    paletteTree_->setHeaderLabel("Node Types");
    paletteTree_->header()->setSectionResizeMode(QHeaderView::Stretch);
    paletteTree_->setDragEnabled(true);
    paletteTree_->setSelectionMode(QAbstractItemView::SingleSelection);
    paletteTree_->setDragDropMode(QAbstractItemView::DragOnly);
    connect(paletteTree_, &QTreeWidget::itemDoubleClicked,
            this, [this](QTreeWidgetItem* item, int) -> void {
                if (item && item->childCount() == 0) {
                    addSelectedPaletteNode();
                }
            });
    paletteLayout->addWidget(paletteTree_);
    paletteHost->setLayout(paletteLayout);

    // --- Remote Models Panel ---
    auto* remoteHost = new QWidget();
    auto* remoteLayout = new QVBoxLayout(remoteHost);
    remoteNodes_ = new RemoteNodesWidget(remoteHost);
    connect(remoteNodes_, &RemoteNodesWidget::logMessage, this, &MainWindow::appendLog);
    remoteLayout->addWidget(remoteNodes_);
    remoteHost->setLayout(remoteLayout);

    // --- Diagram Properties Panel ---
    auto* schemaHost = new QWidget();
    auto* schemaForm = new QFormLayout(schemaHost);
    schemaNameEdit_ = new QLineEdit(schemaName_, schemaHost);
    schemaDtSpin_ = new QDoubleSpinBox(schemaHost);
    schemaDtSpin_->setDecimals(9);
    schemaDtSpin_->setRange(1e-9, 10.0);
    schemaDtSpin_->setSingleStep(0.0001);
    schemaDtSpin_->setValue(0.001);
    schemaForm->addRow("Name", schemaNameEdit_);
    schemaForm->addRow("Sampling Interval (dt)", schemaDtSpin_);
    connect(schemaNameEdit_, &QLineEdit::editingFinished,
            this, &MainWindow::schemaPropertiesEdited);
    connect(schemaDtSpin_, &QDoubleSpinBox::editingFinished,
            this, &MainWindow::schemaPropertiesEdited);
    schemaHost->setLayout(schemaForm);

    // --- Runtime Log (bottom dock, like previous layout) ---
    logDock_ = new QDockWidget("Runtime Log", this);
    logView_ = new QTextEdit(logDock_);
    logView_->setReadOnly(true);
    logDock_->setWidget(logView_);
    addDockWidget(Qt::BottomDockWidgetArea, logDock_);

    // Use style-provided icons so sidebar colors follow the active UI palette/theme.
    const auto paletteIcon = style()->standardIcon(QStyle::SP_FileDialogDetailedView);
    const auto remoteIcon = style()->standardIcon(QStyle::SP_ComputerIcon);
    const auto propertiesIcon = style()->standardIcon(QStyle::SP_FileDialogInfoView);
    const auto logIcon = style()->standardIcon(QStyle::SP_FileDialogContentsView);

    // Add items to sidebar with icons
    sidebar_->addItem({
        .id = "palette",
        .title = "Node Types",
        .icon = paletteIcon,
        .tooltip = "Node Palette (Add nodes to diagram)",
        .panel = paletteHost
    });

    sidebar_->addItem({
        .id = "remote",
        .title = "Remote Nodes",
        .icon = remoteIcon,
        .tooltip = "Remote Models (Manage remote nodes)",
        .panel = remoteHost
    });

    sidebar_->addItem({
        .id = "properties",
        .title = "Diagram Properties",
        .icon = propertiesIcon,
        .tooltip = "Diagram Properties (Name, dt, etc.)",
        .panel = schemaHost
    });

    sidebar_->addItem({
        .id = "log",
        .title = "",
        .icon = logIcon,
        .tooltip = "Runtime Log (Show/hide bottom log window)",
        .group = s7ui::ui::SidebarGroup::Logs,
        .panel = nullptr,
        .onClick = [this]() -> void {
            if (!logDock_) {
                return;
            }
            if (logDock_->isVisible()) {
                logDock_->hide();
            } else {
                logDock_->show();
                logDock_->raise();
            }
        }
    });


}

// ---------------------------------------------------------------------------
// Palette
// ---------------------------------------------------------------------------

void MainWindow::ensureVisualCatalogLoaded()
{
    if (visualCatalog_) {
        return;
    }

    visualCatalog_ = std::make_unique<s7ui::visual::Catalog>();
    const QString dir = QStringLiteral(S7UI_SOURCE_DIR "/s7ui/visuals");
    visualCatalog_->loadFromDir(dir, [this](const QString& msg) -> void { appendLog(msg); });
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
    if (auto* paletteWidget = qobject_cast<PaletteTreeWidget*>(paletteTree_)) {
        paletteWidget->setNodeFactory(nodeFactory_.get());
        // Give the palette widget the scene settings so prototype connector
        // symbol rects are sized correctly in the drag preview.
        paletteWidget->setSceneSettings(cadSettings_);
        // Give the palette widget a live view-scale provider so the drag
        // preview pixmap always matches the node's apparent size on canvas.
        if (view_) {
            paletteWidget->setViewScaleProvider([this]() -> qreal {
                return view_->transform().m11();
            });
        }
    }
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
        appendLog(QString("[diagram] startup load failed: %1")
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
    if (schemaNameEdit_) {
        const QSignalBlocker block(schemaNameEdit_);
        schemaNameEdit_->setText(schemaName_);
    }
    if (schemaDtSpin_) {
        const QSignalBlocker block(schemaDtSpin_);
        schemaDtSpin_->setValue(r.dt);
    }
    sheetPresenter_->fitInView(view_);
    updateModelActionStates();
    updateWindowTitle();
}

void MainWindow::loadSchema()
{
    const QString startDir = currentSchemaPath_.isEmpty()
        ? QDir::currentPath()
        : QFileInfo(currentSchemaPath_).absolutePath();
    const QString path = QFileDialog::getOpenFileName(
        this, "Load Diagram", startDir, "Diagram Files (*.json);;All Files (*)");
    if (path.isEmpty()) {
        return;
    }
    if (!schemaDocumentController_) {
        ensureVisualCatalogLoaded();
    }
    try {
        const auto r = schemaWorkflowController_->loadFromFile(*schemaDocumentController_, path);
        currentSchemaPath_ = path;
        applyLoadedSchemaResult(r);
    }
    catch (const std::exception& ex) {
        QMessageBox::warning(this, "Load Diagram Failed", QString::fromUtf8(ex.what()));
    }
}

void MainWindow::saveSchema()
{
    if (currentSchemaPath_.isEmpty()) {
        saveSchemaAs();
        return;
    }
    if (saveSchemaToPath(currentSchemaPath_, false)) {
        appendLog(QString("[diagram] saved to %1").arg(currentSchemaPath_));
    }
}

void MainWindow::saveSchemaAs()
{
    const QString startDir = currentSchemaPath_.isEmpty()
        ? QDir::currentPath()
        : QFileInfo(currentSchemaPath_).absolutePath();
    const QString path = QFileDialog::getSaveFileName(
        this, "Save Diagram As", startDir, "Diagram Files (*.json);;All Files (*)");
    if (path.isEmpty()) {
        return;
    }
    if (saveSchemaToPath(path, true)) {
        appendLog(QString("[diagram] saved to %1").arg(path));
    }
}

auto MainWindow::currentDt() const -> double
{
    return schemaDtSpin_ ? schemaDtSpin_->value() : 0.001;
}

auto MainWindow::saveSchemaToPath(const QString& path, const bool setAsCurrentPath) -> bool
{
    try {
        const auto sheet = sheetPresenter_->currentSize();
        const double dt = currentDt();

        s7ui::documents::DiagramDocument document;
        document.id = schemaName_;
        document.dt = dt;
        document.page.standard = s7ui::documents::DiagramPageLayout::standardFromName(
            sheetPresenter_->currentStandardName());
        document.page.orientation = s7ui::documents::PageOrientation::Landscape;
        document.page.widthMm = sheet.width;
        document.page.heightMm = sheet.height;

        schemaWorkflowController_->saveToFile(
            path,
            document);
        if (setAsCurrentPath) {
            currentSchemaPath_ = path;
        }
        updateWindowTitle();
        return true;
    }
    catch (const std::exception& ex) {
        QMessageBox::warning(this, "Save Diagram Failed", QString::fromUtf8(ex.what()));
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
    if (path.isEmpty()) {
        return;
    }
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

    appendLog(QString("[sheet] Applying preset %1").arg(static_cast<int>(preset)));

    sheetPresenter_->applyPreset(preset, [this](const QString& msg) -> void { appendLog(msg); });

    if (actionSheetA4_) actionSheetA4_->setChecked(preset == P::A4Landscape);
    if (actionSheetA3_) actionSheetA3_->setChecked(preset == P::A3Landscape);
    if (actionSheetA2_) actionSheetA2_->setChecked(preset == P::A2Landscape);
    if (actionSheetA1_) actionSheetA1_->setChecked(preset == P::A1Landscape);

    if (view_ && scene_) {
        const QRectF sceneRect = scene_->sceneRect();
        appendLog(QString("[sheet] Scene rect after preset: %.1f x %.1f mm @ (%.1f, %.1f)")
                  .arg(sceneRect.width(), 0, 'f', 1)
                  .arg(sceneRect.height(), 0, 'f', 1)
                  .arg(sceneRect.left(), 0, 'f', 1)
                  .arg(sceneRect.top(), 0, 'f', 1));

        view_->setSceneRect(sceneRect);

        appendLog(QString("[sheet] View sceneRect set. Calling fitInView..."));
        sheetPresenter_->fitInView(view_);

        const QTransform viewTransform = view_->transform();
        appendLog(QString("[sheet] After fitInView: view transform m11=%1 m22=%2")
                  .arg(viewTransform.m11(), 0, 'f', 4)
                  .arg(viewTransform.m22(), 0, 'f', 4));
    }
}

// ---------------------------------------------------------------------------
// Node palette
// ---------------------------------------------------------------------------

void MainWindow::addSelectedPaletteNode()
{
    if (!view_) {
        return;
    }
    const QPoint vpCenter = view_->viewport()->rect().center();
    addPaletteNodeTypeAt(
        nodeFactory_ ? nodeFactory_->selectedTemplate(paletteTree_).typeName : QString(),
        view_->mapToScene(vpCenter),
        true);
}

void MainWindow::addSelectedPaletteNodeAt(const QPointF& scenePos)
{
    addPaletteNodeTypeAt(
        nodeFactory_ ? nodeFactory_->selectedTemplate(paletteTree_).typeName : QString(),
        scenePos,
        false);
}

void MainWindow::addPaletteNodeTypeAt(const QString& typeName,
                                      const QPointF& scenePos,
                                      const bool cascadeOffset)
{
    if (!nodeFactory_ || typeName.trimmed().isEmpty()) {
        return;
    }

    const auto tmpl = nodeFactory_->templateForType(typeName);
    const int itemIndex = createdNodeCounter_++;
    QPointF itemOrigin = scenePos;
    if (const auto prototype = nodeFactory_->createPrototypeItem(tmpl)) {
        itemOrigin -= nodeFactory_->preferredAnchorInItem(*prototype);
    }
    if (cascadeOffset) {
        const qreal cascade = static_cast<qreal>(itemIndex % 5) * 10.0;
        itemOrigin += QPointF(cascade, cascade);
    }

    const auto item = nodeFactory_->createItemFromTemplate(tmpl, itemIndex, itemOrigin);
    if (scene_->addItem(item)) {
        appendLog(QString("[scene] Added node: %1").arg(tmpl.typeName));
        if (view_ && item) {
            view_->ensureVisible(item->sceneBoundingRect().adjusted(-20.0, -20.0, 20.0, 20.0));
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
    if (schemaRunner_->isRunning()) {
        linkSinkProbes();
    }
    updateModelActionStates();
}

void MainWindow::pauseSchemaModel()
{
    modelRunController_->pause();
    updateModelActionStates();
}

void MainWindow::resumeSchemaModel()
{
    modelRunController_->resume();
    updateModelActionStates();
}

void MainWindow::stopSchemaModel()
{
    modelRunController_->stop();
}


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
    appendLog(QString("[diagram] properties updated: name=%1 dt=%2")
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
    if (!view_) return;

    // -----------------------------------------------------------------------
    // Physical-size zoom mapping
    // -----------------------------------------------------------------------
    // Our scene uses 1 scene unit = 1 mm.  QSchematic's setZoomValue(f) sets
    // the view transform to f, meaning "f pixels per scene unit (mm)".
    // At f=1 the A4 sheet (297 mm) appears only 297 pixels wide — a matchbox.
    //
    // Physical 100% (1 mm on screen = 1 real mm) requires:
    //   f = screen_logical_dpi / 25.4   (pixels per mm at 100% physical size)
    //
    // We call setZoomValue(zoom * mmToPixels) so that:
    //   zoom=1.00  →  physical 100%  (true A4 size on screen)
    //   zoom=0.50  →  physical  50%
    //   zoom=0.25  →  physical  25%
    // -----------------------------------------------------------------------
    const QScreen* screen = windowHandle() ? windowHandle()->screen()
                                           : QApplication::primaryScreen();
    const double dpi        = screen ? screen->logicalDotsPerInchX() : 96.0;
    const double mmToPixels = dpi / 25.4;                  // e.g. ~3.78 at 96 DPI

    const double physicalScale = zoom * mmToPixels;

    // Debug logging
    const QRectF sceneRect = scene_ ? scene_->sceneRect() : QRectF();
    const QSizeF viewSize = view_->viewport()->size();

    appendLog(QString("[zoom] %1% physical (scale=%.4f, DPI=%.1f, mm→px=%.3f)")
              .arg(qRound(zoom * 100.0))
              .arg(physicalScale, 0, 'f', 4)
              .arg(dpi, 0, 'f', 1)
              .arg(mmToPixels, 0, 'f', 3));

    appendLog(QString("[zoom] Scene rect: %.1f x %.1f mm | View size: %1 x %2 px")
              .arg(sceneRect.width(), 0, 'f', 1)
              .arg(sceneRect.height(), 0, 'f', 1)
              .arg(viewSize.width())
              .arg(viewSize.height()));


    view_->setZoomValue(physicalScale);

    const QTransform newTransform = view_->transform();
    appendLog(QString("[zoom] After zoom: transform m11=%1 m22=%2")
              .arg(newTransform.m11(), 0, 'f', 4)
              .arg(newTransform.m22(), 0, 'f', 4));
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
    if (actionToggleWireMode_) {
        actionToggleWireMode_->setChecked(mode == QSchematic::Scene::WireMode);
    }
    updateInteractionHint();
}

void MainWindow::onAdditiveRubberBandDragChanged(const bool active)
{
    additiveRubberBandDragActive_ = active;
    updateInteractionHint();
}

void MainWindow::updateInteractionHint()
{
    if (!statusBar() || !scene_) {
        return;
    }

    if (scene_->mode() == QSchematic::Scene::WireMode) {
        statusBar()->showMessage(
            tr("Wire mode: click to place points, click a pin to finish, Esc to return to select mode."));
        return;
    }

    if (additiveRubberBandDragActive_) {
        statusBar()->showMessage(
            tr("Additive selection: drag a box to add items to the current selection."));
        return;
    }

    statusBar()->showMessage(
        tr("Select mode: drag to box-select. Hold Shift/Ctrl while dragging to add to selection."));
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
    QMainWindow::showEvent(event);
    if (!initialSheetFitDone_) {
        sheetPresenter_->fitInView(view_);
        initialSheetFitDone_ = true;
    }
}

