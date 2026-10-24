// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-S7-plugin-permission

#pragma once

class QAction;
class QMainWindow;
class QMenu;
class QTabWidget;

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
    QAction* loadLibraryAction = nullptr;
    QAction* saveLibraryAction = nullptr;
    QAction* examplesAction = nullptr;
    QAction* fitDiagramAction = nullptr;
    QAction* exitAction = nullptr;
    QAction* runAction = nullptr;
    QAction* runParametersAction = nullptr;
    QAction* pauseAction = nullptr;
    QAction* resumeAction = nullptr;
    QAction* stopAction = nullptr;
    QAction* resetAction = nullptr;
    QAction* deployAction = nullptr;
    QAction* openDeployedAction = nullptr;
};

struct MainWindowMenus {
    QMenu* file = nullptr;
    QMenu* edit = nullptr;
    QMenu* view = nullptr;
    QMenu* diagram = nullptr;
    QMenu* run = nullptr;
    QMenu* nodes = nullptr;
    QMenu* settings = nullptr;
    QMenu* help = nullptr;
};

struct MainWindowWidgets {
    QTabWidget* diagramTabs = nullptr;
    diagram::DiagramEditor* diagramEditor = nullptr;
};

class MainWindowBuilder
{
public:
    [[nodiscard]] static auto createActions(QMainWindow* window) -> MainWindowActions;
    [[nodiscard]] static auto buildMenuAndToolbar(QMainWindow* window, const MainWindowActions& actions) -> MainWindowMenus;
    [[nodiscard]] static auto buildCentralWidgets(QMainWindow* window) -> MainWindowWidgets;
};

} // namespace ui
} // namespace s7studio

