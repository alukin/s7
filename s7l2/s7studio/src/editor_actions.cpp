// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-S7-plugin-permission
#include "editor_actions.hpp"
#include "diagram_editor.hpp"
#include "ui_icons.hpp"

#include <QAction>
#include <QApplication>
#include <QLineEdit>
#include <QMenu>
#include <QPlainTextEdit>
#include <QPointer>
#include <QTextEdit>
#include <QTextDocument>
#include <QUndoGroup>

#include <type_traits>
#include <utility>

namespace s7studio::ui {
namespace {
enum class Command { Undo, Redo, Cut, Copy, Paste, Delete, SelectAll };

// QMenus preserve the previous focus widget. Read-only text accepts Copy and
// Select All, while mutation commands remain harmless.
template<class Function>
bool withTextEditor(QWidget* owner, Function function)
{
    auto* focus = QApplication::focusWidget();
    if (!focus || !owner->isAncestorOf(focus)) return false;
    if (auto* edit = qobject_cast<QLineEdit*>(focus)) { function(edit); return true; }
    if (auto* edit = qobject_cast<QPlainTextEdit*>(focus)) { function(edit); return true; }
    if (auto* edit = qobject_cast<QTextEdit*>(focus)) { function(edit); return true; }
    return false;
}

class ActionHost final : public QObject {
public:
    ActionHost(QWidget* owner, QMenu* menu, QUndoGroup* history,
               std::function<diagram::DiagramEditor*()> activeEditor)
        : QObject(owner), owner_(owner), history_(history), activeEditor_(std::move(activeEditor))
    {
        const auto add = [&](const char* label, const char* name, Command command, const QList<QKeySequence>& keys) {
            auto* action = menu->addAction(QString::fromUtf8(label));
            action->setObjectName(QString::fromLatin1(name));
            action->setShortcuts(keys);
            action->setShortcutContext(Qt::WidgetWithChildrenShortcut);
            connect(action, &QAction::triggered, this, [this, command] { execute(command); });
            result.shortcuts.append(action);
            return action;
        };
        result.undo = add("&Undo", "action.undo", Command::Undo, QKeySequence::keyBindings(QKeySequence::Undo));
        result.undo->setIcon(icon("arrow-back-up"));
        result.redo = add("&Redo", "action.redo", Command::Redo, QKeySequence::keyBindings(QKeySequence::Redo));
        result.redo->setIcon(icon("arrow-forward-up"));
        menu->addSeparator();
        add("Cu&t", "action.cut", Command::Cut, QKeySequence::keyBindings(QKeySequence::Cut));
        add("&Copy", "action.copy", Command::Copy, QKeySequence::keyBindings(QKeySequence::Copy));
        add("&Paste", "action.paste", Command::Paste, QKeySequence::keyBindings(QKeySequence::Paste));
        add("&Delete", "action.delete", Command::Delete, {QKeySequence(Qt::Key_Delete)});
        add("Select &All", "action.selectAll", Command::SelectAll, QKeySequence::keyBindings(QKeySequence::SelectAll));
        menu->addSeparator();
        auto* properties = menu->addAction(QStringLiteral("Element &Properties…"));
        properties->setObjectName(QStringLiteral("action.properties"));
        properties->setShortcut(QKeySequence(Qt::Key_F4));
        properties->setShortcutContext(Qt::WidgetWithChildrenShortcut);
        connect(properties, &QAction::triggered, this, [this] {
            if (auto* editor = activeEditor_()) editor->editSelectedElement();
        });
        result.shortcuts.append(properties);
        connect(history, &QUndoGroup::canUndoChanged, this, [this] { refresh(); });
        connect(history, &QUndoGroup::canRedoChanged, this, [this] { refresh(); });
        connect(history, &QUndoGroup::undoTextChanged, this, [this] { refresh(); });
        connect(history, &QUndoGroup::redoTextChanged, this, [this] { refresh(); });
        connect(menu, &QMenu::aboutToShow, this, [this] { refresh(); });
        connect(qApp, &QApplication::focusChanged, this, [this] {
            disconnect(textChanged_);
            withTextEditor(owner_, [this](auto* edit) {
                using Edit = std::remove_pointer_t<decltype(edit)>;
                textChanged_ = connect(edit, &Edit::textChanged, this, [this] { refresh(); });
            });
            refresh();
        });
        refresh();
    }
    EditorActions result;
private:
    void refresh()
    {
        if (!history_) return;
        bool undo = history_->canUndo(), redo = history_->canRedo();
        const bool text = withTextEditor(owner_, [&](auto* edit) {
            if constexpr (std::is_same_v<std::remove_pointer_t<decltype(edit)>, QLineEdit>) {
                undo = !edit->isReadOnly() && edit->isUndoAvailable();
                redo = !edit->isReadOnly() && edit->isRedoAvailable();
            } else {
                undo = !edit->isReadOnly() && edit->document()->isUndoAvailable();
                redo = !edit->isReadOnly() && edit->document()->isRedoAvailable();
            }
        });
        result.undo->setEnabled(undo); result.redo->setEnabled(redo);
        result.undo->setText(!text && !history_->undoText().isEmpty() ? QStringLiteral("&Undo %1").arg(history_->undoText()) : QStringLiteral("&Undo"));
        result.redo->setText(!text && !history_->redoText().isEmpty() ? QStringLiteral("&Redo %1").arg(history_->redoText()) : QStringLiteral("&Redo"));
    }
    void execute(Command command)
    {
        if (withTextEditor(owner_, [&](auto* edit) {
            if (edit->isReadOnly() && command != Command::Copy && command != Command::SelectAll) return;
            switch (command) {
            case Command::Undo: edit->undo(); break;
            case Command::Redo: edit->redo(); break;
            case Command::Cut: edit->cut(); break;
            case Command::Copy: edit->copy(); break;
            case Command::Paste: edit->paste(); break;
            case Command::SelectAll: edit->selectAll(); break;
            case Command::Delete:
                if constexpr (std::is_same_v<std::remove_pointer_t<decltype(edit)>, QLineEdit>) edit->del();
                else { auto cursor = edit->textCursor(); cursor.deleteChar(); edit->setTextCursor(cursor); }
                break;
            }
        })) { refresh(); return; }
        auto* editor = activeEditor_();
        if (!editor) return;
        switch (command) {
        case Command::Undo: history_->undo(); break;
        case Command::Redo: history_->redo(); break;
        case Command::Cut: editor->cutSelection(); break;
        case Command::Copy: editor->copySelection(); break;
        case Command::Paste: editor->pasteSelection(); break;
        case Command::Delete: editor->deleteSelection(); break;
        case Command::SelectAll: editor->selectAll(); break;
        }
    }
    QWidget* owner_;
    QPointer<QUndoGroup> history_;
    std::function<diagram::DiagramEditor*()> activeEditor_;
    QMetaObject::Connection textChanged_;
};
}

EditorActions buildEditorActions(QWidget* owner, QMenu* menu, QUndoGroup* history,
                                std::function<diagram::DiagramEditor*()> activeEditor)
{
    return (new ActionHost(owner, menu, history, std::move(activeEditor)))->result;
}
}
