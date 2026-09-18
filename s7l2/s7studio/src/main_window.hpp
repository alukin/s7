#pragma once

#include "local_schema_runner.hpp"
#include "model_run_controller.hpp"

#include <QHash>
#include <QMainWindow>
#include <QPointer>
#include <QVector>

#include <memory>

class QAction;
class QActionGroup;
class QLabel;
class QPlainTextEdit;
class QDoubleSpinBox;
class QSpinBox;
class QCloseEvent;
class QSplitter;
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

namespace ui {
class ScopeViewWindow;
class ElementPickerPanel;
}

struct ScopeSamplePoint {
    int channel = 0;
    double timeSeconds = 0.0;
    double value = 0.0;
};

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

private slots:
    void appendLog(const QString& message);
    void onStatusChanged(const QString& status);
    void onRunCompleted(bool success, const QString& error);
    void onTickProgress(long completed, long total);
    void onScopeNodeDoubleClicked(const QString& scopeId, int channelCount);
    void onElementNodeDoubleClicked(const QString& elementId, const QString& elementType, int channelCount);
    void onScopeSample(const QString& scopeId, int channel, long step, double timeSeconds, double value);
    void openDocumentFile();
    void closeCurrentDiagram();
    void saveDocument();
    void saveDocumentAs();
    void loadDemoDiagram();
    void fitDiagramToView();
    void zoomDiagramIn();
    void zoomDiagramOut();
    void resetDiagramZoom();
    void setRuntimePanelVisible(bool visible);
    void setDiagramMode(int mode);
    void setPaperA5();
    void setPaperA4();
    void setPaperA3();
    void setPaperLetter();
    void setPagePortrait();
    void setPageLandscape();
    void onElementRequested(const QString& elementType, const QString& displayName);
    void addTextFrame();
    void setSelectedTextFrameTextColor();
    void setSelectedTextFrameLineColor();
    void setSelectedTextFrameFillColor();
    void setSelectedTextFrameFont(const QFont& font);
    void setSelectedTextFrameFontSize(int pointSize);
    void syncTextFrameStyleControls();

private:
    void buildUi();
    void buildActions();
    void connectRunnerSignals();
    void refreshActions();
    void openScopeWindow(const QString& scopeId, int channelCount);
    void closeScopeWindows();
    void clearScopeSamples();
    void replayScopeSamples(const QString& scopeId, ui::ScopeViewWindow& window);
    void applyPaperSize(double widthMm, double heightMm);
    void syncPageActions();
    void openJsonDocument(const QString& dialogTitle,
                          const QString& filter,
                          bool preferDiagram,
                          const QString& errorTitle);
    auto saveDocumentToPath(const QString& filePath) -> bool;
    [[nodiscard]] auto selectedRunDuration() const -> ui::RunDuration;
    void loadSchemaJson(const boost::json::value& schemaJson, const QString& sourceLabel);
    void loadDiagramJson(const boost::json::value& diagramJson, const QString& sourceLabel);
    void connectSceneSignals();
    void openRunParametersDialog();
    auto configureRun(bool launchAfterAccept) -> bool;
    void launchRunWithCurrentConfiguration();
    void applyDiscretizationFrequency(double frequencyHz);
    static auto updateSchemaDiscretizationParams(boost::json::value& schema,
                                                 double frequencyHz,
                                                 double dtSeconds) -> bool;

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    boost::json::value runtimeSchema_;
    LocalSchemaRunner* runner_ = nullptr;
    std::unique_ptr<ui::ModelRunController> controller_;

    QAction* openAction_ = nullptr;
    QAction* closeDiagramAction_ = nullptr;
    QAction* saveAction_ = nullptr;
    QAction* saveAsAction_ = nullptr;
    QAction* loadDemoAction_ = nullptr;
    QAction* fitDiagramAction_ = nullptr;
    QAction* exitAction_ = nullptr;
    QAction* runAction_ = nullptr;
    QAction* runParametersAction_ = nullptr;
    QAction* pauseAction_ = nullptr;
    QAction* resumeAction_ = nullptr;
    QAction* stopAction_ = nullptr;
    QAction* resetAction_ = nullptr;
    QAction* zoomInAction_ = nullptr;
    QAction* zoomOutAction_ = nullptr;
    QAction* resetZoomAction_ = nullptr;
    QAction* fitPageAction_ = nullptr;
    QAction* showRuntimePanelAction_ = nullptr;
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
    QAction* textColorAction_ = nullptr;
    QAction* lineColorAction_ = nullptr;
    QAction* fillColorAction_ = nullptr;

    diagram::DiagramEditor* diagramEditor_ = nullptr;
    QDoubleSpinBox* dtSpin_ = nullptr;
    QSpinBox* hoursSpin_ = nullptr;
    QSpinBox* minutesSpin_ = nullptr;
    QSpinBox* secondsSpin_ = nullptr;
    QLabel* statusLabel_ = nullptr;
    QLabel* progressLabel_ = nullptr;
    QPlainTextEdit* logView_ = nullptr;
    QSplitter* mainSplitter_ = nullptr;
    QWidget* runtimePanel_ = nullptr;
    QDockWidget* elementPickerDock_ = nullptr;
    ui::ElementPickerPanel* elementPickerPanel_ = nullptr;
    std::unique_ptr<s7::PluginLoader> pluginLoader_;
    QString currentDiagramPath_;
    QHash<QString, QPointer<ui::ScopeViewWindow>> scopeWindows_;
    QHash<QString, QVector<ScopeSamplePoint>> scopeSampleHistory_;
    QActionGroup* paperSizeGroup_ = nullptr;
    QActionGroup* pageOrientationGroup_ = nullptr;
    QToolBar* diagramDrawToolBar_ = nullptr;
    QFontComboBox* textFrameFontCombo_ = nullptr;
    QSpinBox* textFrameFontSizeSpin_ = nullptr;
    double discretizationFrequencyHz_ = 1000.0;
    long runtimeSeconds_ = 60;
    bool clearScopesBeforeRun_ = true;
    bool shuttingDown_ = false;
};

} // namespace s7studio
