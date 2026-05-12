#pragma once

#include "visual_catalog.hpp"

#include <qschematic/scene.hpp>
#include <qschematic/items/item.hpp>

#include <s7runtime.hpp>

#include <boost/json.hpp>

#include <QMainWindow>

#include <memory>

class QTextEdit;
class QTreeWidget;
class QAction;
class QActionGroup;
class QLineEdit;
class QDoubleSpinBox;
class QSpinBox;
class QLabel;
class QTimer;

namespace QSchematic
{
class View;
}

class RemoteNodesWidget;

namespace s7ui {
class SchemaSession;
}

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    enum class SheetPreset
    {
        A4Landscape,
        A3Landscape,
        A2Landscape,
        A1Landscape,
    };

    explicit MainWindow(QWidget* parent = nullptr);

private slots:
    void addSelectedPaletteNode();
    void appendLog(const QString& message);
    void loadSchema();
    void saveSchema();
    void saveSchemaAs();
    void runSchemaModel();
    void pauseSchemaModel();
    void resumeSchemaModel();
    void stopSchemaModel();
    void resetSchemaModel();
    void schemaPropertiesEdited();
    void onRunStatusPoll();
    void toggleWireMode();
    void onSceneModeChanged(int mode);

private:
    struct NodeTemplate
    {
        QString category;
        QString typeName;
        QString defaultIdPrefix;
        int inputPorts = 1;
        int outputPorts = 1;
        bool visualSink = false;
    };

    void createActions();
    void createMenus();
    void createToolbars();
    void createCentralEditor();
    void createDocks();
    void createSheetActions();
    void populatePalette();
    void seedStarterScene();
    boost::json::value buildRuntimeJsonFromScene() const;
    boost::json::value buildSchemaJsonFromScene() const;
    void loadSchemaFromJson(const boost::json::value& root, const QString& sourcePath);
    bool saveSchemaToFile(const QString& path);
    void updateWindowTitle();
    void ensureNodeLibrariesRegistered();
    void ensureVisualCatalogLoaded();
    void refreshViewAfterLayoutChange();
    void applySheetPreset(SheetPreset preset);
    void rebuildPaperOverlay();
    void updateModelActionStates();
    void setModelStatus(const QString& status);
    std::shared_ptr<QSchematic::Items::Item> createItemFromTemplate(const NodeTemplate& nodeTemplate, int index) const;
    NodeTemplate selectedTemplate() const;
    NodeTemplate templateForType(const QString& typeName) const;

    // Hook all SinkNodeItems to their probes from the current session.
    void linkSinkProbes();
    // Detach probes from all SinkNodeItems.
    void unlinkSinkProbes();

    void keyPressEvent(QKeyEvent* evt) override;

    QSchematic::Scene* scene_ = nullptr;
    QSchematic::View* view_ = nullptr;

    QTreeWidget* paletteTree_ = nullptr;
    QTextEdit* logView_ = nullptr;
    RemoteNodesWidget* remoteNodes_ = nullptr;
    QLineEdit* schemaNameEdit_ = nullptr;
    QDoubleSpinBox* schemaDtSpin_ = nullptr;
    QSpinBox* runHoursSpin_ = nullptr;
    QSpinBox* runMinutesSpin_ = nullptr;
    QSpinBox* runSecondsSpin_ = nullptr;
    QLabel* modelStatusLabel_ = nullptr;

    QAction* actionAddNode_ = nullptr;
    QAction* actionOpenSchema_ = nullptr;
    QAction* actionSaveSchema_ = nullptr;
    QAction* actionSaveSchemaAs_ = nullptr;
    QAction* actionFitView_ = nullptr;
    QAction* actionSheetA4_ = nullptr;
    QAction* actionSheetA3_ = nullptr;
    QAction* actionSheetA2_ = nullptr;
    QAction* actionSheetA1_ = nullptr;
    QAction* actionToggleWireMode_ = nullptr;
    QAction* actionRunModel_ = nullptr;
    QAction* actionPauseModel_ = nullptr;
    QAction* actionResumeModel_ = nullptr;
    QAction* actionStopModel_ = nullptr;
    QAction* actionResetModel_ = nullptr;

    mutable int createdNodeCounter_ = 0;
    QString schemaName_ = "s7ui_schema";
    QString schemaHandle_;
    QString runId_;
    bool librariesRegistered_ = false;
    bool visualCatalogLoaded_ = false;
    bool modelLoaded_ = false;
    bool runActive_ = false;
    bool runPaused_ = false;
    QString currentSchemaPath_;
    SheetPreset sheetPreset_ = SheetPreset::A4Landscape;

    s7ui::visual::Catalog visualCatalog_;

    // Live session (replaces the NodeRuntimeService for local execution).
    std::shared_ptr<s7ui::SchemaSession> schemaSession_;
    QTimer* runStatusTimer_ = nullptr;  // polls session state at ~5 Hz
};
