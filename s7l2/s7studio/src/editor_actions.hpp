// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-S7-plugin-permission
#pragma once

#include <QList>
#include <functional>

class QAction;
class QMenu;
class QWidget;
class QUndoGroup;

namespace s7studio::diagram { class DiagramEditor; }
namespace s7studio::ui {

// Shared menu/toolbar actions, routed to focused text or the active diagram.
// Associate shortcuts with each diagram editor; text widgets keep native keys.
struct EditorActions {
    QAction* undo;
    QAction* redo;
    QList<QAction*> shortcuts;
};
EditorActions buildEditorActions(QWidget* owner, QMenu* menu, QUndoGroup* history,
    std::function<diagram::DiagramEditor*()> activeEditor);

}
