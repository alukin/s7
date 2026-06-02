#include "sidebar_widget.hpp"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QIcon>

using namespace s7ui::ui;

SidebarWidget::SidebarWidget(QWidget* parent)
    : QWidget(parent)
{
    createUI();
}

void SidebarWidget::createUI()
{
    auto* mainLayout = new QHBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    // --- Icon bar (always visible, minimal width) ---
    iconBar_ = new QWidget(this);
    iconBar_->setMinimumWidth(SIDEBAR_MIN_WIDTH);
    iconBar_->setMaximumWidth(SIDEBAR_MIN_WIDTH);
    iconBar_->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);

    auto* iconBarLayout = new QVBoxLayout(iconBar_);
    iconBarLayout->setContentsMargins(0, 4, 0, 4);
    iconBarLayout->setSpacing(0);

    auto* primaryHost = new QWidget(iconBar_);
    primaryGroup_ = new QVBoxLayout(primaryHost);
    primaryGroup_->setContentsMargins(0, 0, 0, 0);
    primaryGroup_->setSpacing(BUTTON_SPACING);

    auto* logsHost = new QWidget(iconBar_);
    logsGroup_ = new QVBoxLayout(logsHost);
    logsGroup_->setContentsMargins(0, 0, 0, 0);
    logsGroup_->setSpacing(BUTTON_SPACING);

    iconBarLayout->addWidget(primaryHost, 0, Qt::AlignHCenter | Qt::AlignTop);
    iconBarLayout->addStretch(1);
    iconBarLayout->addWidget(logsHost, 0, Qt::AlignHCenter | Qt::AlignBottom);

    iconBar_->setLayout(iconBarLayout);
    mainLayout->addWidget(iconBar_);

    // --- Stacked panel container ---
    panelContainer_ = new QWidget(this);
    panelContainer_->setMinimumWidth(PANEL_MIN_WIDTH);
    panelContainer_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    auto* panelLayout = new QVBoxLayout(panelContainer_);
    panelLayout->setContentsMargins(0, 0, 0, 0);
    panelLayout->setSpacing(0);

    tabWidget_ = new QTabWidget(panelContainer_);
    tabWidget_->setTabsClosable(true);
    tabWidget_->setMovable(true);
    tabWidget_->setDocumentMode(true);
    panelLayout->addWidget(tabWidget_);

    connect(tabWidget_, &QTabWidget::currentChanged, this, [this](int idx) {
        if (idx < 0) {
            activeItemId_.clear();
        } else {
            activeItemId_ = idForPanel(tabWidget_->widget(idx));
        }
    });

    connect(tabWidget_, &QTabWidget::tabCloseRequested, this, [this](int idx) {
        closeTabAt(idx);
    });

    mainLayout->addWidget(panelContainer_, 1);

    // Start collapsed until at least one tab is toggled on.
    panelContainer_->hide();

    setLayout(mainLayout);
}

void SidebarWidget::addItem(const SidebarItem& item)
{
    // Store item metadata
    itemMap_[item.id] = item;

    // Create icon button
    auto* btn = new QPushButton(this);
    btn->setIcon(item.icon);
    btn->setIconSize(QSize(ICON_SIZE - 8, ICON_SIZE - 8));
    btn->setFixedSize(ICON_SIZE, ICON_SIZE);
    btn->setToolTip(item.tooltip);
    btn->setFlat(true);
    btn->setCheckable(item.panel != nullptr);
    btn->setAutoExclusive(false);

    buttonMap_[item.id] = btn;

    connect(btn, &QPushButton::clicked, this, [this, itemId = item.id]() {
        onIconButtonClicked(itemId);
    });

    auto* targetGroup = (item.group == SidebarGroup::Logs) ? logsGroup_ : primaryGroup_;
    if (targetGroup) {
        targetGroup->addWidget(btn, 0, Qt::AlignHCenter);
    }

}

void SidebarWidget::onIconButtonClicked(const QString& itemId)
{
    if (!itemMap_.count(itemId)) {
        return;
    }

    const auto& item = itemMap_.at(itemId);
    if (!item.panel) {
        if (item.onClick) {
            item.onClick();
        }
        return;
    }

    if (!tabWidget_) {
        return;
    }

    const int existingIdx = tabWidget_->indexOf(item.panel);

    // Toggle existing tab off.
    if (existingIdx >= 0) {
        closeTabAt(existingIdx);
        updateButtonStates();
        updateVisibility();
        return;
    }

    // Toggle tab on and focus it.
    const int newIdx = tabWidget_->addTab(item.panel, item.title);
    tabWidget_->setCurrentIndex(newIdx);
    activeItemId_ = itemId;
    updateButtonStates();
    updateVisibility();
}

void SidebarWidget::closeTabAt(const int index)
{
    if (!tabWidget_ || index < 0 || index >= tabWidget_->count()) {
        return;
    }

    tabWidget_->removeTab(index);
    if (tabWidget_->count() == 0) {
        activeItemId_.clear();
    } else {
        activeItemId_ = idForPanel(tabWidget_->currentWidget());
    }
    updateButtonStates();
    updateVisibility();
}

void SidebarWidget::updateButtonStates()
{
    if (!tabWidget_) {
        return;
    }

    for (auto& [id, btn] : buttonMap_) {
        const auto it = itemMap_.find(id);
        const bool isOpen = (it != itemMap_.end()) && it->second.panel &&
                            (tabWidget_->indexOf(it->second.panel) >= 0);
        btn->setChecked(isOpen);
    }
}

void SidebarWidget::updateVisibility()
{
    if (!panelContainer_ || !tabWidget_) {
        return;
    }

    panelContainer_->setVisible(tabWidget_->count() > 0);
}

auto SidebarWidget::idForPanel(const QWidget* panel) const -> QString
{
    for (const auto& [id, item] : itemMap_) {
        if (item.panel == panel) {
            return id;
        }
    }
    return QString();
}

void SidebarWidget::hideAll()
{
    activeItemId_.clear();

    if (tabWidget_) {
        while (tabWidget_->count() > 0) {
            tabWidget_->removeTab(0);
        }
    }

    updateButtonStates();
    updateVisibility();
}

QString SidebarWidget::currentActiveId() const
{
    return activeItemId_;
}



