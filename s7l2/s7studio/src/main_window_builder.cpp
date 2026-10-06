#include "main_window_builder.hpp"

#include "diagram_editor.hpp"
#include "ui_icons.hpp"

#include <QAction>
#include <QMainWindow>
#include <QMenuBar>
#include <QToolBar>
#include <Qt>
#include <QWidget>

namespace s7studio::ui {

auto MainWindowBuilder::createActions(QMainWindow* window) -> MainWindowActions
{
    MainWindowActions actions;
    actions.openAction = new QAction("Open...", window);
    actions.closeDiagramAction = new QAction("Close Diagram", window);
    actions.saveAction = new QAction("Save", window);
    actions.saveAsAction = new QAction("Save As...", window);
    actions.loadLibraryAction = new QAction("Load Element Library...", window);
    actions.saveLibraryAction = new QAction("Save Schema Library...", window);
    actions.loadDemoAction = new QAction("Load Demo Diagram", window);
    actions.fitDiagramAction = new QAction("Fit Diagram to View", window);
    actions.exitAction = new QAction("Exit", window);

    actions.runAction = new QAction("Run", window);
    actions.runParametersAction = new QAction("Run Parameters...", window);
    actions.pauseAction = new QAction("Pause", window);
    actions.resumeAction = new QAction("Resume", window);
    actions.stopAction = new QAction("Stop", window);
    actions.resetAction = new QAction("Reset", window);
    actions.deployAction = new QAction("Deploy to Nodes...", window);
    actions.openDeployedAction = new QAction("Open Deployed Diagram...", window);

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
    actions.runParametersAction->setText(QStringLiteral("Run Configuration..."));

    return actions;
}

void MainWindowBuilder::buildMenuAndToolbar(QMainWindow* window, const MainWindowActions& actions)
{
    auto* fileMenu = window->menuBar()->addMenu("&File");
    fileMenu->addAction(actions.openAction);
    fileMenu->addAction(actions.closeDiagramAction);
    fileMenu->addSeparator();
    fileMenu->addAction(actions.saveAction);
    fileMenu->addAction(actions.saveAsAction);
    fileMenu->addAction(actions.loadLibraryAction);
    fileMenu->addAction(actions.saveLibraryAction);
    fileMenu->addSeparator();
    fileMenu->addAction(actions.loadDemoAction);
    fileMenu->addSeparator();
    fileMenu->addAction(actions.exitAction);

    auto* diagramMenu = window->menuBar()->addMenu("&Diagram");
    diagramMenu->addAction(actions.fitDiagramAction);

    auto* runMenu = window->menuBar()->addMenu("&Run");
    runMenu->addAction(actions.runAction);
    runMenu->addAction(actions.runParametersAction);
    runMenu->addAction(actions.pauseAction);
    runMenu->addAction(actions.resumeAction);
    runMenu->addAction(actions.stopAction);
    runMenu->addSeparator();
    runMenu->addAction(actions.resetAction);
    runMenu->addSeparator();
    runMenu->addAction(actions.deployAction);
    runMenu->addAction(actions.openDeployedAction);

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
}

auto MainWindowBuilder::buildCentralWidgets(QMainWindow* window) -> MainWindowWidgets
{
    MainWindowWidgets widgets;
    // The diagram is the whole central area; everything else is a tool window.
    widgets.diagramEditor = new diagram::DiagramEditor(window);
    widgets.diagramEditor->setMinimumWidth(320);
    window->setCentralWidget(widgets.diagramEditor);
    window->resize(1440, 900);
    window->setWindowTitle("s7studio");
    return widgets;
}

} // namespace s7studio::ui

