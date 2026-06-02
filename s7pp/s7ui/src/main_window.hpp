#pragma once

#include "sheet_presenter.hpp"
#include "sidebar_widget.hpp"

#include "s7_view.hpp"

#include <qschematic/scene.hpp>
#include <qschematic/settings.hpp>
#include <qschematic/items/item.hpp>

#include <QMainWindow>

#include <memory>

class QTextEdit;
class QTreeWidget;
class QAction;
class QLineEdit;
class QDoubleSpinBox;
class QSpinBox;
class QLabel;
class QKeyEvent;
class QShowEvent;
class QDockWidget;
class QPointF;


class RemoteNodesWidget;

namespace s7ui { class SchemaRunner; }

namespace s7ui::visual { class Catalog; }

namespace s7ui::ui {
class NodeFactory;
class ModelRunController;
class PaletteService;
class SchemaDocumentController;
class SchemaFileService;
class SchemaWorkflowController;
struct LoadedSchemaResult;
}

/// Application main window.
/// Responsibility: UI layout, action wiring, and delegation to services.
/// Does NOT contain business logic — that lives in the service layer.
class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    /// Destructor is declared (not defaulted) so that unique_ptr<T> members
    /// with forward-declared T are destroyed only where the full types are visible
    /// (i.e. in main_window.cpp).
    ~MainWindow() override;

private slots:
    void addSelectedPaletteNode();
    void addSelectedPaletteNodeAt(const QPointF& scenePos);
    void appendLog(const QString& message);
    void loadSchema();
    void saveSchema();
    void saveSchemaAs();
    void exportPdf();
    void printSheet();
    void runSchemaModel();
    void pauseSchemaModel();
    void resumeSchemaModel();
    void stopSchemaModel();
    void resetSchemaModel();
    void schemaPropertiesEdited();
    void onRunStatusPoll(long completed, long total);
    void toggleWireMode();
    void onSceneModeChanged(int mode);
    void onAdditiveRubberBandDragChanged(bool active);
    void onRunCompleted(bool success, const QString& error);

private:
    // --- UI construction (each method has one responsibility) ---
    void createActions();
    void createSheetActions();
    void createMenus();
    void createToolbars();
    void createCentralEditor();
    void createSidebar();

    // --- Helpers ---
    void populatePalette();
    void ensureVisualCatalogLoaded();
    void seedStarterScene();
    void applyLoadedSchemaResult(const s7ui::ui::LoadedSchemaResult& r);
    [[nodiscard]] auto currentDt() const -> double;
    auto saveSchemaToPath(const QString& path, bool setAsCurrentPath) -> bool;

    void applySheetPreset(s7ui::ui::SheetPresenter::Preset preset);
    void setZoomPreset(double zoom);
    void updateModelActionStates();
    void setModelStatus(const QString& status);
    void updateWindowTitle();
    void updateInteractionHint();
    void addPaletteNodeTypeAt(const QString& typeName, const QPointF& scenePos, bool cascadeOffset);
    void linkSinkProbes();
    void unlinkSinkProbes();

    void keyPressEvent(QKeyEvent* evt) override;
    void showEvent(QShowEvent* event) override;

    // --- Qt GUI widgets ---
    QSchematic::Scene*          scene_      = nullptr;
    s7ui::ui::S7View*           view_       = nullptr;
    QTreeWidget*                paletteTree_= nullptr;
    QTextEdit*                  logView_    = nullptr;
    QDockWidget*                logDock_    = nullptr;
    RemoteNodesWidget*          remoteNodes_= nullptr;
    s7ui::ui::SidebarWidget*    sidebar_    = nullptr;

    QAction* actionOpenSchema_     = nullptr;
    QAction* actionSaveSchema_     = nullptr;
    QAction* actionSaveSchemaAs_   = nullptr;
    QAction* actionFitView_        = nullptr;
    QAction* actionFitSelection_   = nullptr;
    QAction* actionZoom25_         = nullptr;
    QAction* actionZoom50_         = nullptr;
    QAction* actionZoom100_        = nullptr;
    QAction* actionExportPdf_      = nullptr;
    QAction* actionPrint_          = nullptr;
    QAction* actionSheetA4_        = nullptr;
    QAction* actionSheetA3_        = nullptr;
    QAction* actionSheetA2_        = nullptr;
    QAction* actionSheetA1_        = nullptr;
    QAction* actionToggleWireMode_ = nullptr;
    QAction* actionRunModel_       = nullptr;
    QAction* actionPauseModel_     = nullptr;
    QAction* actionResumeModel_    = nullptr;
    QAction* actionStopModel_      = nullptr;
    QAction* actionResetModel_     = nullptr;
    QAction* actionAddNode_        = nullptr;

    QSpinBox*       runHoursSpin_    = nullptr;
    QSpinBox*       runMinutesSpin_  = nullptr;
    QSpinBox*       runSecondsSpin_  = nullptr;
    QLabel*         modelStatusLabel_= nullptr;
    QLineEdit*      schemaNameEdit_  = nullptr;
    QDoubleSpinBox* schemaDtSpin_    = nullptr;

    // --- State ---
    QString currentSchemaPath_;
    QString schemaName_ = "s7ui_schema";
    int     createdNodeCounter_ = 0;
    bool    initialSheetFitDone_= false;
    bool    additiveRubberBandDragActive_ = false;
    QSchematic::Settings cadSettings_;

    // --- Services (owned) ---
    s7ui::SchemaRunner*                              schemaRunner_            = nullptr;
    std::unique_ptr<s7ui::visual::Catalog>           visualCatalog_;
    std::unique_ptr<s7ui::ui::NodeFactory>           nodeFactory_;
    std::unique_ptr<s7ui::ui::SchemaFileService>     schemaFileService_;
    std::unique_ptr<s7ui::ui::SchemaWorkflowController> schemaWorkflowController_;
    std::unique_ptr<s7ui::ui::ModelRunController>    modelRunController_;
    std::unique_ptr<s7ui::ui::SheetPresenter>        sheetPresenter_;
    std::unique_ptr<s7ui::ui::SchemaDocumentController> schemaDocumentController_;
};
