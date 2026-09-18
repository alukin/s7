#pragma once

class QAction;
class QDoubleSpinBox;
class QLabel;
class QMainWindow;
class QPlainTextEdit;
class QSplitter;
class QSpinBox;
class QWidget;

namespace s7studio {

namespace diagram {
class DiagramEditor;
}

namespace ui {

struct MainWindowActions {
    QAction* openAction = nullptr;
    QAction* closeDiagramAction = nullptr;
    QAction* saveAction = nullptr;
    QAction* saveAsAction = nullptr;
    QAction* loadDemoAction = nullptr;
    QAction* fitDiagramAction = nullptr;
    QAction* exitAction = nullptr;
    QAction* runAction = nullptr;
    QAction* runParametersAction = nullptr;
    QAction* pauseAction = nullptr;
    QAction* resumeAction = nullptr;
    QAction* stopAction = nullptr;
    QAction* resetAction = nullptr;
};

struct MainWindowWidgets {
    diagram::DiagramEditor* diagramEditor = nullptr;
    QDoubleSpinBox* dtSpin = nullptr;
    QSpinBox* hoursSpin = nullptr;
    QSpinBox* minutesSpin = nullptr;
    QSpinBox* secondsSpin = nullptr;
    QLabel* statusLabel = nullptr;
    QLabel* progressLabel = nullptr;
    QPlainTextEdit* logView = nullptr;
    QSplitter* splitter = nullptr;
    QWidget* runtimePanel = nullptr;
};

class MainWindowBuilder
{
public:
    [[nodiscard]] static auto createActions(QMainWindow* window) -> MainWindowActions;
    static void buildMenuAndToolbar(QMainWindow* window, const MainWindowActions& actions);
    [[nodiscard]] static auto buildCentralWidgets(QMainWindow* window) -> MainWindowWidgets;
};

} // namespace ui
} // namespace s7studio

