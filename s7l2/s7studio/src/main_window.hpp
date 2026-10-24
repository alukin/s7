// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-S7-plugin-permission

#pragma once

#include "local_schema_runner.hpp"
#include "diagram_document.hpp"
#include "element_requirements.hpp"
#include "model_run_controller.hpp"
#include "tool_panels.hpp"

#include <QHash>
#include <QList>
#include <QMainWindow>
#include <QPointer>
#include <QVector>

#include <boost/json.hpp>

#include <cstdint>
#include <memory>
#include <functional>
#include <thread>
#include <vector>

class QMenu;
class QAction;
class QActionGroup;
class QSpinBox;
class QCloseEvent;
class QToolButton;
class QWidget;
class QDockWidget;
class QFontComboBox;
class QToolBar;
class QFont;

namespace s7 {
class PluginLoader;
}

namespace s7studio {

namespace diagram {
class DiagramEditor;
}

namespace viewers {
class ViewerManager;
}

namespace ui {
class ToolWindows;
class ElementPickerPanel;
class HeadlessNodesPanel;
class ProbesPanel;
}

class SchemaTabs;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr, bool restoreStartupDiagram = true);
    ~MainWindow() override;

    // Opens a diagram, runtime schema or element library file.
    void openPath(const QString& filePath, bool preferDiagram = true, const QString& errorTitle = QStringLiteral("Open failed"));

private slots:
    void appendLog(const QString& message);
    void onStatusChanged(const QString& status);
    void onRunCompleted(bool success, const QString& error);
    void onTickProgress(long completed, long total);
    void onElementNodeDoubleClicked(const QString& elementId, const QString& elementType, int channelCount);
    void onScopeSamplesAvailable();
    void onProbesChanged();
    void onProbeOpenScopeRequested(const QString& probeName);
    void onProbeOpenPhasePortraitRequested(const QString& xProbeName, const QString& yProbeName);
    void onProbeRenameRequested(const QString& probeName, const QString& newName);
    void onProbeDeleteRequested(const QString& probeName);
    void refreshWireTypeLabels();
    void openDocumentFile();
    void closeCurrentDiagram();
    void saveDocument();
    void saveDocumentAs();
    void loadElementLibrary();
    void saveSchemaLibrary();
    void loadDemoDiagram();
    void chooseExampleDiagram();
    bool openExampleDiagram(const QString& path);
    void chooseTextFrameFont();
    void fitDiagramToView();
    void zoomDiagramIn();
    void zoomDiagramOut();
    void resetDiagramZoom();
    void setDiagramMode(int mode);
    void setPaperA5();
    void setPaperA4();
    void setPaperA3();
    void setPaperLetter();
    void setPagePortrait();
    void setPageLandscape();
    void chooseScopeTemporaryDirectory();
    void onElementRequested(const QString& elementType, const QString& displayName);
    void onHeadlessNodeActivated(const QString& endpoint, const QString& nodeUuid, const QString& nodeName);
    void addTextFrame();
    void setSelectedTextFrameTextColor();
    void setSelectedTextFrameLineColor();
    void setSelectedTextFrameFillColor();
    void setSelectedTextFrameFont(const QFont& font);
    void setSelectedTextFrameFontSize(int pointSize);
    void syncTextFrameStyleControls();
    void deployToNodes();
    void openDeployedDiagram();

private:
    void buildUi();
    bool confirmDocumentReplacement();
    void markDocumentSaved();
    [[nodiscard]] diagram::DiagramEditor* activeDiagramEditor() const;
    void buildActions();
    void refreshRecentDiagrams();
    void rememberRecentDiagram(const QString& path);
    bool openRecentDiagram(const QString& path, bool interactive);
    void restoreStartupDiagram();
    void connectRunnerSignals();
    void resetLocalRun();
    void retireRunForShutdown();
    bool observerMatchesActiveRun(const QString& observerId) const;
    // Live oscilloscope trigger settings persist as the observer's "trigger" param.
    void applyTriggerPreset(const QString& observerId, bool reportInvalid);
    void storeTriggerPreset(const QString& observerId);
    void refreshActions();
    // The viewer type recording a diagram observer (its element type), or
    // Scope for probes that belong to no diagram observer.
    [[nodiscard]] auto observerViewerType(const QString& observerId) const -> QString;
    void closeViewers();
    void clearViewerSamples();
    void refreshProbesPanel();
    // The RunProbe list the runner should sample for the current scene.
    [[nodiscard]] auto currentRunProbes() const -> std::vector<RunProbe>;
    void applyPaperSize(double widthMm, double heightMm);
    void syncPageActions();
    void openJsonDocument(const QString& dialogTitle,
                          const QString& filter,
                          bool preferDiagram,
                          const QString& errorTitle);
    void loadElementLibraryJson(const boost::json::value& libraryJson, const QString& sourceLabel);
    auto saveDocumentToPath(const QString& filePath) -> bool;
    bool loadSchemaJson(const boost::json::value& schemaJson, const QString& sourceLabel);
    bool loadDiagramJson(const boost::json::value& diagramJson, const QString& sourceLabel);
    void connectSceneSignals();
    void openRunParametersDialog();
    void buildToolWindows();
    // Collects the diagram's problems into the Problems tool window and the status bar.
    void refreshProblems();
    void showProblem(const ui::Problem& problem);
    void saveWindowLayout();
    void restoreWindowLayout();
    auto configureRun(bool launchAfterAccept) -> bool;
    void launchRunWithCurrentConfiguration();
    void applyDiscretizationFrequency(double frequencyHz);
    static auto updateSchemaDiscretizationParams(boost::json::value& schema,
                                                 double frequencyHz,
                                                 double dtSeconds) -> bool;

protected:
    void closeEvent(QCloseEvent* event) override;
    [[nodiscard]] bool documentModified() const;
    [[nodiscard]] auto currentDocument() const -> boost::json::value;
    // The scene serialized as a document; elements and connections come from
    // the scene (the source of truth), the rest of the schema from runtimeSchema_,
    // which is updated to match.
    [[nodiscard]] auto syncSchemaFromScene() -> diagram::DiagramDocument;
    [[nodiscard]] auto serializeScene() const -> diagram::DiagramDocument;
    void refreshPlacementLabels();
    // Copies element params of runtimeSchema_ (run configuration) onto the scene's elements.
    void applySchemaParamsToScene();
    // Loads the plugins a schema needs (searching the plugin paths) and
    // reports, after the diagram is shown, what is still missing.
    auto prepareElementTypes(const boost::json::value& schema) -> ElementCheck;
    void reportElementTypes(const ElementCheck& check, const QString& sourceLabel);
    // Why the diagram cannot run or deploy (a missing element type, a wire that connects nothing); empty if it can.
    [[nodiscard]] auto diagramProblem() const -> QString;
    // Runs work off the UI thread (one at a time); `done` runs on the UI thread.
    auto startNetworkTask(std::function<std::function<void()>()> work) -> bool;

private:
    boost::json::value runtimeSchema_;
    boost::json::value savedDocument_;
    bool hasSavedDocument_ = false;
    std::uint64_t runGeneration_ = 0;
    QHash<QString, QString> runObserverTypes_;
    bool observersFrozen_ = false;
    // Deployment data of the open diagram (`runtime_nodes`, `streams`).
    boost::json::array deploymentRuntimeNodes_;
    boost::json::array deploymentStreams_;
    std::thread networkTask_;
    bool networkTaskRunning_ = false;
    LocalSchemaRunner* runner_ = nullptr;
    std::unique_ptr<ui::ModelRunController> controller_;

    QMenu* recentDiagramsMenu_ = nullptr;
    QAction* openAction_ = nullptr;
    QAction* closeDiagramAction_ = nullptr;
    QAction* saveAction_ = nullptr;
    QAction* saveAsAction_ = nullptr;
    QAction* loadLibraryAction_ = nullptr;
    QAction* saveLibraryAction_ = nullptr;
    QAction* examplesAction_ = nullptr;
    QAction* fitDiagramAction_ = nullptr;
    QAction* exitAction_ = nullptr;
    QAction* runAction_ = nullptr;
    QAction* runParametersAction_ = nullptr;
    QAction* pauseAction_ = nullptr;
    QAction* resumeAction_ = nullptr;
    QAction* stopAction_ = nullptr;
    QAction* resetAction_ = nullptr;
    QAction* deployAction_ = nullptr;
    QAction* openDeployedAction_ = nullptr;
    QAction* zoomInAction_ = nullptr;
    QAction* zoomOutAction_ = nullptr;
    QAction* resetZoomAction_ = nullptr;
    QAction* fitPageAction_ = nullptr;
    QAction* overlayScopeTracesAction_ = nullptr;
    QAction* modeNormalAction_ = nullptr;
    QAction* modeWireAction_ = nullptr;
    QAction* paperA5Action_ = nullptr;
    QAction* paperA4Action_ = nullptr;
    QAction* paperA3Action_ = nullptr;
    QAction* paperLetterAction_ = nullptr;
    QAction* portraitPageAction_ = nullptr;
    QAction* landscapePageAction_ = nullptr;
    QAction* addTextFrameAction_ = nullptr;
    QList<QAction*> editorShortcuts_;
    QAction* textFrameFontAction_ = nullptr;
    QAction* textColorAction_ = nullptr;
    QAction* lineColorAction_ = nullptr;
    QAction* fillColorAction_ = nullptr;
    QAction* undoAction_ = nullptr;
    QAction* redoAction_ = nullptr;

    diagram::DiagramEditor* diagramEditor_ = nullptr;
    SchemaTabs* schemaTabs_ = nullptr;
    ui::ToolWindows* toolWindows_ = nullptr;
    ui::ProblemsPanel* problemsPanel_ = nullptr;
    ui::LogPanel* logPanel_ = nullptr;
    ui::RunPanel* runPanel_ = nullptr;
    ui::DeployPanel* deployPanel_ = nullptr;
    ui::ProbesPanel* probesPanel_ = nullptr;
    QDockWidget* problemsDock_ = nullptr;
    QDockWidget* logDock_ = nullptr;
    QDockWidget* runDock_ = nullptr;
    QDockWidget* deployDock_ = nullptr;
    QDockWidget* probesDock_ = nullptr;
    QToolButton* problemsStatus_ = nullptr;
    // Warnings from loading the diagram (e.g. another plugin version), shown under Problems.
    std::vector<QString> loadWarnings_;
    QDockWidget* elementPickerDock_ = nullptr;
    ui::ElementPickerPanel* elementPickerPanel_ = nullptr;
    QDockWidget* headlessNodesDock_ = nullptr;
    ui::HeadlessNodesPanel* headlessNodesPanel_ = nullptr;
    std::unique_ptr<s7::PluginLoader> pluginLoader_;
    QString currentDiagramPath_;
    // Open observation viewers (Scope, Phase Portrait, ...) of this window.
    viewers::ViewerManager* viewers_ = nullptr;
    class QTimer* wireTypeTimer_ = nullptr;
    QString lastWireTypeError_;
    qint64 lastScopeDropLogMs_ = 0;
    QActionGroup* paperSizeGroup_ = nullptr;
    QActionGroup* pageOrientationGroup_ = nullptr;
    QToolBar* diagramDrawToolBar_ = nullptr;
    QFontComboBox* textFrameFontCombo_ = nullptr;
    QSpinBox* textFrameFontSizeSpin_ = nullptr;
    double discretizationFrequencyHz_ = 1000.0;
    double runtimeSeconds_ = 60;
    bool clearScopesBeforeRun_ = true;
    bool shuttingDown_ = false;
    bool storingTriggerPreset_ = false;
};

} // namespace s7studio
