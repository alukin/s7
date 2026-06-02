#pragma once

#include <QWidget>
#include <QVBoxLayout>
#include <QTabWidget>
#include <functional>
#include <map>

class QIconEngine;
class QPushButton;

namespace s7ui::ui {

enum class SidebarGroup {
    Primary,
    Logs
};

/**
 * SidebarItem represents one tab in the sidebar with an icon and associated panel.
 */
struct SidebarItem {
    QString id;
    QString title;
    QIcon icon;
    QString tooltip;
    SidebarGroup group = SidebarGroup::Primary;
    QWidget* panel = nullptr;  // ownership remains with caller
    std::function<void()> onClick;
};

/**
 * SidebarWidget implements a collapsible left sidebar with icon buttons.
 * Clicking an icon shows/hides the corresponding panel.
 * The sidebar starts hidden and expands when an icon is clicked.
 */
class SidebarWidget : public QWidget
{
    Q_OBJECT

public:
    explicit SidebarWidget(QWidget* parent = nullptr);
    ~SidebarWidget() override = default;

    /// Add a sidebar item (icon + panel)
    void addItem(const SidebarItem& item);

    /// Hide all panels
    void hideAll();

    /// Get current active panel ID, or empty string if none
    [[nodiscard]] QString currentActiveId() const;

private slots:
    void onIconButtonClicked(const QString& itemId);

private:
    void createUI();
    void updateButtonStates();
    void updateVisibility();
    [[nodiscard]] auto idForPanel(const QWidget* panel) const -> QString;
    void closeTabAt(int index);

    // Visual constants
    static constexpr int ICON_SIZE = 30;
    static constexpr int BUTTON_SPACING = 6;
    static constexpr int SIDEBAR_MIN_WIDTH = 40;
    static constexpr int PANEL_MIN_WIDTH = 250;

    // Layout: [icon buttons | separator line | stacked panel]
    QWidget*        iconBar_         = nullptr;
    QVBoxLayout*    primaryGroup_    = nullptr;
    QVBoxLayout*    logsGroup_       = nullptr;
    QTabWidget*     tabWidget_      = nullptr;
    QWidget*        panelContainer_ = nullptr;

    // Active item tracking
    QString activeItemId_;
    std::map<QString, QPushButton*> buttonMap_;
    std::map<QString, SidebarItem> itemMap_;
};

} // namespace s7ui::ui

