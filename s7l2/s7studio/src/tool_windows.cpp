// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-S7-plugin-permission

#include "tool_windows.hpp"

#include "ui_icons.hpp"

#include <QAction>
#include <QDockWidget>
#include <QMainWindow>
#include <QMenu>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QToolBar>
#include <QWidget>

#include <algorithm>

namespace s7studio::ui {

namespace {

auto makeStripe(QMainWindow* window, const QString& name) -> QToolBar*
{
    auto* stripe = new QToolBar(QStringLiteral("Tool Window Bar"), window);
    stripe->setObjectName(name);
    stripe->setMovable(false);
    stripe->setFloatable(false);
    stripe->setToolButtonStyle(Qt::ToolButtonIconOnly);
    stripe->setIconSize(QSize(20, 20));
    stripe->setContextMenuPolicy(Qt::PreventContextMenu);
    return stripe;
}

} // namespace

ToolWindows::ToolWindows(QMainWindow* window)
    : QObject(window)
    , window_(window)
    , leftStripe_(makeStripe(window, QStringLiteral("toolWindowBar.left")))
    , rightStripe_(makeStripe(window, QStringLiteral("toolWindowBar.right")))
    , menu_(new QMenu(QStringLiteral("&Tool Windows"), window))
    , hideAll_(new QAction(icon(QStringLiteral("arrows-maximize")), QStringLiteral("&Hide All Tool Windows"), window))
    , stripes_(new QAction(QStringLiteral("Tool Window &Bars"), window))
{
    window->addToolBar(Qt::LeftToolBarArea, leftStripe_);
    window->addToolBar(Qt::RightToolBarArea, rightStripe_);
    // Side windows run the full height; the bottom window sits between them.
    window->setCorner(Qt::BottomLeftCorner, Qt::LeftDockWidgetArea);
    window->setCorner(Qt::BottomRightCorner, Qt::RightDockWidgetArea);

    // Left stripe: left windows above the spacer, bottom windows below it.
    auto* spacer = new QWidget(leftStripe_);
    spacer->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
    leftSpacer_ = leftStripe_->addWidget(spacer);

    hideAll_->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_F12));
    hideAll_->setToolTip(QStringLiteral("Hide all tool windows, or show them again (Ctrl+Shift+F12)"));
    window->addAction(hideAll_);
    connect(hideAll_, &QAction::triggered, this, &ToolWindows::hideAll);

    stripes_->setCheckable(true);
    stripes_->setChecked(true);
    connect(stripes_, &QAction::toggled, this, [this](const bool visible) {
        leftStripe_->setVisible(visible);
        rightStripe_->setVisible(visible);
    });
}

auto ToolWindows::add(const QString& id, const QString& title, const QString& iconName, QWidget* content,
                      const Qt::DockWidgetArea area, const QKeySequence& shortcut) -> QDockWidget*
{
    auto* dock = new QDockWidget(title, window_);
    dock->setObjectName(QStringLiteral("toolWindow.") + id);
    dock->setWidget(content);
    dock->setAllowedAreas(area);
    dock->setFeatures(QDockWidget::DockWidgetClosable | QDockWidget::DockWidgetMovable);
    window_->addDockWidget(area, dock);
    dock->hide();

    auto* action = new QAction(icon(iconName), title, window_);
    action->setCheckable(true);
    action->setShortcut(shortcut);
    action->setToolTip(shortcut.isEmpty() ? title : QStringLiteral("%1 (%2)").arg(title, shortcut.toString(QKeySequence::NativeText)));
    window_->addAction(action); // the shortcut works with the stripes hidden too
    connect(action, &QAction::triggered, this, [this, dock](const bool checked) {
        if (checked) {
            show(dock);
        } else {
            dock->hide();
        }
    });
    connect(dock, &QDockWidget::visibilityChanged, action, [action, dock] {
        const QSignalBlocker blocker(action);
        action->setChecked(!dock->isHidden());
    });

    if (area == Qt::RightDockWidgetArea) {
        rightStripe_->addAction(action);
    } else if (area == Qt::LeftDockWidgetArea) {
        leftStripe_->insertAction(leftSpacer_, action);
    } else {
        leftStripe_->addAction(action);
    }
    menu_->addAction(action);
    entries_.push_back({dock, action, area});
    return dock;
}

void ToolWindows::show(QDockWidget* dock)
{
    const auto it = std::ranges::find(entries_, dock, &Entry::dock);
    if (it == entries_.end()) {
        return;
    }
    for (const auto& entry : entries_) {
        if (entry.dock != dock && entry.area == it->area && !entry.dock->isHidden()) {
            entry.dock->hide();
        }
    }
    dock->show();
    dock->raise();
    hiddenByHideAll_.clear();
}

void ToolWindows::toggle(QDockWidget* dock)
{
    if (!dock->isHidden()) {
        dock->hide();
    } else {
        show(dock);
    }
}

void ToolWindows::hideAll()
{
    std::vector<QDockWidget*> visible;
    for (const auto& entry : entries_) {
        if (!entry.dock->isHidden()) {
            visible.push_back(entry.dock);
        }
    }
    if (!visible.empty()) {
        for (auto* dock : visible) {
            dock->hide();
        }
        hiddenByHideAll_ = std::move(visible);
        return;
    }
    const auto restore = hiddenByHideAll_;
    for (auto* dock : restore) {
        show(dock);
    }
}

void ToolWindows::normalize()
{
    for (const auto area : {Qt::LeftDockWidgetArea, Qt::RightDockWidgetArea, Qt::BottomDockWidgetArea}) {
        bool shown = false;
        for (const auto& entry : entries_) {
            if (entry.area == area && !entry.dock->isHidden()) {
                if (shown) {
                    entry.dock->hide();
                }
                shown = true;
            }
        }
    }
    for (const auto& entry : entries_) {
        const QSignalBlocker blocker(entry.action);
        entry.action->setChecked(!entry.dock->isHidden());
    }
    const QSignalBlocker blocker(stripes_);
    stripes_->setChecked(!leftStripe_->isHidden() || !rightStripe_->isHidden());
}

} // namespace s7studio::ui
