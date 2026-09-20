#pragma once

#include <s7/element_catalog.hpp>

#include <QWidget>
#include <QTreeWidget>
#include <QLineEdit>
#include <QLabel>
#include <memory>
#include <vector>

namespace s7 {
class ElementRegistry;
}

namespace s7studio::ui {

// Element library panel for picking elements from the registry
class ElementPickerPanel : public QWidget {
    Q_OBJECT

public:
    explicit ElementPickerPanel(const s7::ElementRegistry& registry, QWidget* parent = nullptr);
    ~ElementPickerPanel() override;

    void refreshCatalog();

signals:
    // Emitted when user wants to add element to diagram
    void elementRequested(const QString& elementType, const QString& displayName);

protected:
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;

private slots:
    void onSearchTextChanged(const QString& text);
    void onItemDoubleClicked(QTreeWidgetItem* item, int column);
    void onItemSelectionChanged();

private:
    void buildCategoryTree();
    void applyFilter(const QString& filterText);

    const s7::ElementRegistry& registry_;

    // UI components
    QLineEdit* searchEdit_ = nullptr;
    QTreeWidget* elementTree_ = nullptr;
    QLabel* elementCountLabel_ = nullptr;
    QLabel* descriptionLabel_ = nullptr;

    // Cached element data
    struct CategoryNode {
        QString category;
        std::vector<s7::ElementCatalogEntry> elements;
    };
    std::vector<CategoryNode> categoryNodes_;
};

} // namespace s7studio::ui

