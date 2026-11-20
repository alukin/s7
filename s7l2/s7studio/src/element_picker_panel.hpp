// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-S7-plugin-permission
#pragma once

#include <QWidget>

class QComboBox;
class QTreeWidget;
class QTreeWidgetItem;
class QLineEdit;
class QLabel;

namespace s7 { class ElementRegistry; }

namespace s7studio::ui {

// Two presentations of the same registered elements and Studio observers.
class ElementPickerPanel : public QWidget {
    Q_OBJECT
public:
    explicit ElementPickerPanel(const s7::ElementRegistry& registry, QWidget* parent = nullptr);
    ~ElementPickerPanel() override;
    void refreshCatalog();

signals:
    void elementRequested(const QString& elementType, const QString& displayName);

private slots:
    void onItemDoubleClicked(QTreeWidgetItem* item, int column);
    void onItemSelectionChanged();

private:
    void rebuildTree();
    void applyFilter(const QString& text);

    const s7::ElementRegistry& registry_;
    QComboBox* viewSelector_ = nullptr;
    QLineEdit* searchEdit_ = nullptr;
    QTreeWidget* elementTree_ = nullptr;
    QLabel* elementCountLabel_ = nullptr;
    QLabel* descriptionLabel_ = nullptr;
};

} // namespace s7studio::ui
