#include "main_window_builder.hpp"

#include "diagram_editor.hpp"

#include <QAction>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QLabel>
#include <QMainWindow>
#include <QMenuBar>
#include <QPlainTextEdit>
#include <QSizePolicy>
#include <QSplitter>
#include <QSpinBox>
#include <QStatusBar>
#include <QToolBar>
#include <QVBoxLayout>
#include <QWidget>

namespace s7studio::ui {

auto MainWindowBuilder::createActions(QMainWindow* window) -> MainWindowActions
{
    MainWindowActions actions;
    actions.openSchemaAction = new QAction("Open Schema...", window);
    actions.openDiagramAction = new QAction("Open Diagram...", window);
    actions.loadDemoAction = new QAction("Load Demo Diagram", window);
    actions.fitDiagramAction = new QAction("Fit Diagram to View", window);
    actions.exitAction = new QAction("Exit", window);

    actions.runAction = new QAction("Run", window);
    actions.pauseAction = new QAction("Pause", window);
    actions.resumeAction = new QAction("Resume", window);
    actions.stopAction = new QAction("Stop", window);
    actions.resetAction = new QAction("Reset", window);
    return actions;
}

void MainWindowBuilder::buildMenuAndToolbar(QMainWindow* window, const MainWindowActions& actions)
{
    auto* fileMenu = window->menuBar()->addMenu("&File");
    fileMenu->addAction(actions.openSchemaAction);
    fileMenu->addAction(actions.openDiagramAction);
    fileMenu->addAction(actions.loadDemoAction);
    fileMenu->addSeparator();
    fileMenu->addAction(actions.exitAction);

    auto* diagramMenu = window->menuBar()->addMenu("&Diagram");
    diagramMenu->addAction(actions.fitDiagramAction);

    auto* toolBar = window->addToolBar("Runtime");
    toolBar->addAction(actions.openSchemaAction);
    toolBar->addAction(actions.openDiagramAction);
    toolBar->addSeparator();
    toolBar->addAction(actions.runAction);
    toolBar->addAction(actions.pauseAction);
    toolBar->addAction(actions.resumeAction);
    toolBar->addAction(actions.stopAction);
    toolBar->addAction(actions.resetAction);
}

auto MainWindowBuilder::buildCentralWidgets(QMainWindow* window) -> MainWindowWidgets
{
    MainWindowWidgets widgets;

    auto* central = new QWidget(window);
    auto* rootLayout = new QVBoxLayout(central);
    auto* splitter = new QSplitter(Qt::Horizontal, central);
    splitter->setChildrenCollapsible(false);
    widgets.splitter = splitter;

    widgets.diagramEditor = new diagram::DiagramEditor(splitter);
    widgets.diagramEditor->setMinimumWidth(320);
    splitter->addWidget(widgets.diagramEditor);

    auto* runtimePanel = new QWidget(splitter);
    widgets.runtimePanel = runtimePanel;
    runtimePanel->setMinimumWidth(220);
    runtimePanel->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
    auto* runtimeLayout = new QVBoxLayout(runtimePanel);

    auto* form = new QFormLayout();

    widgets.dtSpin = new QDoubleSpinBox(runtimePanel);
    widgets.dtSpin->setDecimals(6);
    widgets.dtSpin->setRange(1e-6, 10.0);
    widgets.dtSpin->setSingleStep(0.001);
    widgets.dtSpin->setValue(0.001);

    widgets.hoursSpin = new QSpinBox(runtimePanel);
    widgets.hoursSpin->setRange(0, 23);

    widgets.minutesSpin = new QSpinBox(runtimePanel);
    widgets.minutesSpin->setRange(0, 59);
    widgets.minutesSpin->setValue(1);

    widgets.secondsSpin = new QSpinBox(runtimePanel);
    widgets.secondsSpin->setRange(0, 59);

    form->addRow("dt (s)", widgets.dtSpin);
    form->addRow("Run hours", widgets.hoursSpin);
    form->addRow("Run minutes", widgets.minutesSpin);
    form->addRow("Run seconds", widgets.secondsSpin);
    runtimeLayout->addLayout(form);

    widgets.statusLabel = new QLabel("idle", runtimePanel);
    widgets.progressLabel = new QLabel("0 / 0", runtimePanel);
    runtimeLayout->addWidget(widgets.statusLabel);
    runtimeLayout->addWidget(widgets.progressLabel);

    widgets.logView = new QPlainTextEdit(runtimePanel);
    widgets.logView->setReadOnly(true);
    runtimeLayout->addWidget(widgets.logView, 1);

    splitter->addWidget(runtimePanel);
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 0);
    splitter->setSizes({1200, 260});
    runtimePanel->hide();
    rootLayout->addWidget(splitter, 1);

    window->setCentralWidget(central);
    window->statusBar()->showMessage("Local embedded runtime with diagram editor");
    window->resize(1440, 900);
    window->setWindowTitle("s7studio");

    return widgets;
}

} // namespace s7studio::ui

