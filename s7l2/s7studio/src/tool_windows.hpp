#pragma once

#include <QKeySequence>
#include <QObject>
#include <QString>

#include <vector>

class QAction;
class QDockWidget;
class QMainWindow;
class QMenu;
class QToolBar;
class QWidget;

namespace s7studio::ui {

// Tool windows in the style of the JetBrains IDEs: each is a dock with a
// button on a stripe at the window edge (left stripe: left windows at the
// top, bottom windows at the bottom; right stripe: right windows). An area
// shows one tool window at a time; its button (or shortcut) shows it and,
// pressed again, hides it. Hide All Tool Windows gives the diagram the whole
// window and brings the same windows back.
class ToolWindows final : public QObject
{
    Q_OBJECT

public:
    explicit ToolWindows(QMainWindow* window);

    // A hidden tool window showing `content` in `area` (left, right or bottom).
    auto add(const QString& id, const QString& title, const QString& iconName, QWidget* content, Qt::DockWidgetArea area,
             const QKeySequence& shortcut = {}) -> QDockWidget*;

    // Shows the window (hiding the other one of its area) and raises it.
    void show(QDockWidget* dock);
    void toggle(QDockWidget* dock);
    // Hides every visible tool window; called again, shows them again.
    void hideAll();
    // At most one visible window per area, buttons in step (after setup or
    // QMainWindow::restoreState).
    void normalize();

    [[nodiscard]] QMenu* menu() const { return menu_; }                  // View > Tool Windows
    [[nodiscard]] QAction* hideAllAction() const { return hideAll_; }
    [[nodiscard]] QAction* stripesAction() const { return stripes_; }  // show the stripes

private:
    struct Entry {
        QDockWidget* dock;
        QAction* action;
        Qt::DockWidgetArea area;
    };

    QMainWindow* window_;
    QToolBar* leftStripe_;
    QToolBar* rightStripe_;
    QAction* leftSpacer_;
    QMenu* menu_;
    QAction* hideAll_;
    QAction* stripes_;
    std::vector<Entry> entries_;
    std::vector<QDockWidget*> hiddenByHideAll_;
};

} // namespace s7studio::ui
