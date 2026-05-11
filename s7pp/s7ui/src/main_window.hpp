#pragma once

#include <qschematic/scene.hpp>
#include <qschematic/items/item.hpp>

#include <s7runtime.hpp>

#include <nlohmann/json.hpp>

#include <QMainWindow>

class QTextEdit;
class QTreeWidget;
class QAction;
class QLineEdit;
class QDoubleSpinBox;
class QSpinBox;
class QLabel;

namespace QSchematic
{
class View;
}

class RemoteNodesWidget;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);

private slots:
    void addSelectedPaletteNode();
    void appendLog(const QString& message);
    void runSchemaModel();
    void pauseSchemaModel();
    void resumeSchemaModel();
    void stopSchemaModel();
    void resetSchemaModel();
    void schemaPropertiesEdited();

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
    void populatePalette();
    void seedStarterScene();
    nlohmann::json buildRuntimeJsonFromScene() const;
    void ensureNodeLibrariesRegistered();
    void refreshViewAfterLayoutChange();
    void updateModelActionStates();
    void setModelStatus(const QString& status);
    std::shared_ptr<QSchematic::Items::Item> createItemFromTemplate(const NodeTemplate& nodeTemplate, int index) const;
    NodeTemplate selectedTemplate() const;
    NodeTemplate templateForType(const QString& typeName) const;

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
    QAction* actionFitView_ = nullptr;
    QAction* actionRunModel_ = nullptr;
    QAction* actionPauseModel_ = nullptr;
    QAction* actionResumeModel_ = nullptr;
    QAction* actionStopModel_ = nullptr;
    QAction* actionResetModel_ = nullptr;

    mutable int createdNodeCounter_ = 0;
    s7::NodeRuntimeService runtimeService_;
    QString schemaName_ = "s7ui_schema";
    QString schemaHandle_;
    QString runId_;
    bool librariesRegistered_ = false;
    bool modelLoaded_ = false;
    bool runActive_ = false;
    bool runPaused_ = false;
    bool startupFitQueued_ = false;
};

