#pragma once

#include "local_schema_runner.hpp"
#include "model_run_controller.hpp"

#include <QMainWindow>
#include <QHash>
#include <QPointer>

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

namespace s7studio {

namespace diagram {
class DiagramEditor;
}

namespace ui {
class ScopeViewWindow;
}

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
    void onScopeSample(const QString& scopeId, int channel, long step, double timeSeconds, double value);
    void openSchemaFile();
    void openDiagramFile();
    void loadDemoDiagram();
    void fitDiagramToView();
    void zoomDiagramIn();
    void zoomDiagramOut();
    void resetDiagramZoom();
    void setRuntimePanelVisible(bool visible);
    void setPaperA5();
    void setPaperA4();
    void setPaperA3();
    void setPaperLetter();
    void setPagePortrait();
    void setPageLandscape();

private:
    void buildUi();
    void buildActions();
    void connectRunnerSignals();
    void refreshActions();
    void openScopeWindow(const QString& scopeId, int channelCount);
    void closeScopeWindows();
    void applyPaperSize(double widthMm, double heightMm);
    void syncPageActions();
    void openJsonDocument(const QString& dialogTitle,
                          const QString& filter,
                          bool preferDiagram,
                          const QString& errorTitle);
    [[nodiscard]] auto selectedRunDuration() const -> ui::RunDuration;
    void loadSchemaJson(const boost::json::value& schemaJson, const QString& sourceLabel);
    void loadDiagramJson(const boost::json::value& diagramJson, const QString& sourceLabel);

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    boost::json::value runtimeSchema_;
    LocalSchemaRunner* runner_ = nullptr;
    std::unique_ptr<ui::ModelRunController> controller_;

    QAction* openSchemaAction_ = nullptr;
    QAction* openDiagramAction_ = nullptr;
    QAction* loadDemoAction_ = nullptr;
    QAction* fitDiagramAction_ = nullptr;
    QAction* exitAction_ = nullptr;
    QAction* runAction_ = nullptr;
    QAction* pauseAction_ = nullptr;
    QAction* resumeAction_ = nullptr;
    QAction* stopAction_ = nullptr;
    QAction* resetAction_ = nullptr;
    QAction* zoomInAction_ = nullptr;
    QAction* zoomOutAction_ = nullptr;
    QAction* resetZoomAction_ = nullptr;
    QAction* fitPageAction_ = nullptr;
    QAction* showRuntimePanelAction_ = nullptr;
    QAction* paperA5Action_ = nullptr;
    QAction* paperA4Action_ = nullptr;
    QAction* paperA3Action_ = nullptr;
    QAction* paperLetterAction_ = nullptr;
    QAction* portraitPageAction_ = nullptr;
    QAction* landscapePageAction_ = nullptr;

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
    QHash<QString, QPointer<ui::ScopeViewWindow>> scopeWindows_;
    QActionGroup* paperSizeGroup_ = nullptr;
    QActionGroup* pageOrientationGroup_ = nullptr;
};

} // namespace s7studio
