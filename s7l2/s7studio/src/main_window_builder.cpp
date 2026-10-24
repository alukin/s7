// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-S7-plugin-permission

#include "main_window_builder.hpp"

#include "diagram_editor.hpp"
#include "ui_icons.hpp"

#include <QAction>
#include <QApplication>
#include <QEvent>
#include <QMainWindow>
#include <QMenu>
#include <QMenuBar>
#include <QToolBar>
#include <Qt>
#include <QWidget>
#include <QTabWidget>

namespace s7studio::ui {
namespace {
class ToolbarPalette final : public QObject {
public:
    explicit ToolbarPalette(QMainWindow* window) : QObject(window), window_(window)
    {
        window_->installEventFilter(this);
        apply();
    }
private:
    void apply()
    {
        // Stylesheet palette expressions can retain the palette from their
        // first polish. Resolve the same role again on desktop palette changes.
        window_->setStyleSheet(QStringLiteral("QToolBar { background: %1; } QToolBar QToolButton { color: palette(button-text); } QToolBar QToolButton:disabled { color: palette(mid); }")
            .arg(QApplication::palette().color(QPalette::Button).name()));
    }
    bool eventFilter(QObject* watched, QEvent* event) override
    {
        if (watched == window_ && event->type() == QEvent::ApplicationPaletteChange) apply();
        return false;
    }
    QMainWindow* window_;
};
}

auto MainWindowBuilder::createActions(QMainWindow* window) -> MainWindowActions
{
    MainWindowActions actions;
    actions.openAction = new QAction("Open Diagram…", window);
    actions.closeDiagramAction = new QAction("Close Diagram", window);
    actions.saveAction = new QAction("Save", window);
    actions.saveAsAction = new QAction("Save As…", window);
    actions.loadLibraryAction = new QAction("Load Element Library…", window);
    actions.saveLibraryAction = new QAction("Save Diagram as Library Element…", window);
    actions.examplesAction = new QAction("Example Diagrams…", window);
    actions.fitDiagramAction = new QAction("Fit Diagram to View", window);
    actions.exitAction = new QAction("Exit", window);

    actions.runAction = new QAction("Run", window);
    actions.runParametersAction = new QAction("Run Configuration…", window);
    actions.pauseAction = new QAction("Pause", window);
    actions.resumeAction = new QAction("Resume", window);
    actions.stopAction = new QAction("Stop", window);
    actions.resetAction = new QAction("Reset", window);
    actions.deployAction = new QAction("Deploy to Nodes…", window);
    actions.openDeployedAction = new QAction("Open Deployed Diagram…", window);

    actions.openAction->setObjectName(QStringLiteral("action.open"));
    actions.closeDiagramAction->setObjectName(QStringLiteral("action.closeDiagram"));
    actions.saveAction->setObjectName(QStringLiteral("action.save"));
    actions.saveAsAction->setObjectName(QStringLiteral("action.saveAs"));
    actions.loadLibraryAction->setObjectName(QStringLiteral("action.loadLibrary"));
    actions.saveLibraryAction->setObjectName(QStringLiteral("action.saveLibrary"));
    actions.examplesAction->setObjectName(QStringLiteral("action.examples"));
    actions.fitDiagramAction->setObjectName(QStringLiteral("action.fitDiagram"));
    actions.deployAction->setObjectName(QStringLiteral("action.deploy"));
    actions.openDeployedAction->setObjectName(QStringLiteral("action.openDeployed"));
    actions.runAction->setObjectName(QStringLiteral("action.run"));
    actions.runParametersAction->setObjectName(QStringLiteral("action.runConfiguration"));
    actions.pauseAction->setObjectName(QStringLiteral("action.pause"));
    actions.resumeAction->setObjectName(QStringLiteral("action.resume"));
    actions.stopAction->setObjectName(QStringLiteral("action.stop"));
    actions.resetAction->setObjectName(QStringLiteral("action.reset"));
    actions.exitAction->setObjectName(QStringLiteral("action.exit"));

    actions.openAction->setIcon(icon(QStringLiteral("folder-open")));
    actions.saveAction->setIcon(icon(QStringLiteral("device-floppy")));
    actions.runAction->setIcon(icon(QStringLiteral("player-play"), QColor(0x2e, 0x7d, 0x32)));
    actions.runParametersAction->setIcon(icon(QStringLiteral("adjustments-horizontal")));
    actions.pauseAction->setIcon(icon(QStringLiteral("player-pause")));
    actions.resumeAction->setIcon(icon(QStringLiteral("player-track-next")));
    actions.stopAction->setIcon(icon(QStringLiteral("player-stop"), QColor(0xc6, 0x28, 0x28)));
    actions.resetAction->setIcon(icon(QStringLiteral("refresh")));
    actions.deployAction->setIcon(icon(QStringLiteral("cloud-upload")));
    actions.openDeployedAction->setIcon(icon(QStringLiteral("cloud-download")));
    actions.runAction->setShortcut(QKeySequence(Qt::Key_F5));
    actions.stopAction->setShortcut(QKeySequence(Qt::SHIFT | Qt::Key_F5));
    actions.runParametersAction->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_R));
    actions.runParametersAction->setToolTip(QStringLiteral("Configure local simulation duration and sample rate (Ctrl+Shift+R)."));
    actions.runAction->setToolTip(QStringLiteral("Run local simulation (F5). Model time is independent of wall-clock speed."));
    actions.pauseAction->setToolTip(QStringLiteral("Pause simulation after the current causal step, retaining captured results."));
    actions.resumeAction->setToolTip(QStringLiteral("Resume the paused simulation from its current state."));
    actions.stopAction->setToolTip(QStringLiteral("Stop simulation (Shift+F5), retaining captured results."));
    actions.resetAction->setToolTip(QStringLiteral("Reset simulation and clear the current viewer presentation."));

    return actions;
}

auto MainWindowBuilder::buildMenuAndToolbar(QMainWindow* window, const MainWindowActions& actions) -> MainWindowMenus
{
    // Some platform styles draw a dark menu/toolbar while exposing light
    // Window/WindowText roles. Text/ButtonText follow the actual controls.
    window->menuBar()->setStyleSheet(QStringLiteral("QMenuBar { color: palette(text); } QMenuBar::item:disabled { color: palette(mid); }"));
    new ToolbarPalette(window);
    MainWindowMenus menus;
    const auto menu = [window](const char* title, const char* name) {
        auto* result = window->menuBar()->addMenu(QString::fromUtf8(title));
        result->setObjectName(QString::fromLatin1(name));
        return result;
    };
    menus.file = menu("&File", "menu.file");
    menus.edit = menu("&Edit", "menu.edit");
    menus.view = menu("&View", "menu.view");
    menus.diagram = menu("&Diagram", "menu.diagram");
    menus.run = menu("&Run", "menu.run");
    menus.nodes = menu("&Nodes", "menu.nodes");
    menus.settings = menu("&Settings", "menu.settings");
    menus.help = menu("&Help", "menu.help");
    auto* fileMenu = menus.file;
    fileMenu->addAction(actions.openAction);
    fileMenu->addAction(actions.closeDiagramAction);
    fileMenu->addSeparator();
    fileMenu->addAction(actions.saveAction);
    fileMenu->addAction(actions.saveAsAction);
    fileMenu->addSeparator();
    auto* libraries = fileMenu->addMenu(QStringLiteral("Element Libraries"));
    libraries->setObjectName(QStringLiteral("menu.libraries"));
    libraries->addAction(actions.loadLibraryAction);
    libraries->addAction(actions.saveLibraryAction);
    fileMenu->addSeparator();
    fileMenu->addAction(actions.examplesAction);
    fileMenu->addSeparator();
    fileMenu->addAction(actions.exitAction);

    auto* runMenu = menus.run;
    runMenu->addAction(actions.runParametersAction);
    runMenu->addSeparator();
    runMenu->addAction(actions.runAction);
    runMenu->addAction(actions.pauseAction);
    runMenu->addAction(actions.resumeAction);
    runMenu->addAction(actions.stopAction);
    runMenu->addSeparator();
    runMenu->addAction(actions.resetAction);
    menus.nodes->addAction(actions.deployAction);
    menus.nodes->addAction(actions.openDeployedAction);

    auto* fileToolBar = window->addToolBar("File");
    fileToolBar->setObjectName(QStringLiteral("toolbar.file"));
    fileToolBar->addAction(actions.openAction);
    fileToolBar->addAction(actions.saveAction);

    auto* runToolBar = window->addToolBar("Run");
    runToolBar->setObjectName(QStringLiteral("toolbar.run"));
    runToolBar->setToolButtonStyle(Qt::ToolButtonIconOnly);
    runToolBar->addAction(actions.runParametersAction);
    runToolBar->addAction(actions.runAction);
    runToolBar->addAction(actions.pauseAction);
    runToolBar->addAction(actions.resumeAction);
    runToolBar->addAction(actions.stopAction);
    runToolBar->addAction(actions.resetAction);
    return menus;
}

auto MainWindowBuilder::buildCentralWidgets(QMainWindow* window) -> MainWindowWidgets
{
    MainWindowWidgets widgets;
    // The diagram is the whole central area; everything else is a tool window.
    widgets.diagramEditor = new diagram::DiagramEditor(window);
    widgets.diagramEditor->setMinimumWidth(320);
    widgets.diagramTabs = new QTabWidget(window);
    widgets.diagramTabs->setObjectName(QStringLiteral("diagram.tabs"));
    widgets.diagramTabs->addTab(widgets.diagramEditor,QStringLiteral("Diagram"));
    window->setCentralWidget(widgets.diagramTabs);
    window->resize(1440, 900);
    window->setWindowTitle("s7studio");
    return widgets;
}

} // namespace s7studio::ui

