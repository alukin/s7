#include "element_picker_panel.hpp"

#include "viewers/viewer_registry.hpp"

#include "s7/element_registry.hpp"
#include "s7/element_catalog.hpp"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QLabel>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QHeaderView>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QApplication>
#include <QDrag>
#include <QMouseEvent>
#include <QFont>
#include <algorithm>
#include <map>

namespace s7studio::ui {

ElementPickerPanel::ElementPickerPanel(const s7::ElementRegistry& registry, QWidget* parent)
    : QWidget(parent), registry_(registry)
{
    // Build UI
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(8);

    // Search box
    auto* searchLayout = new QHBoxLayout();
    auto* searchLabel = new QLabel("&Search:", this);
    searchEdit_ = new QLineEdit(this);
    searchEdit_->setObjectName(QStringLiteral("elements.search"));
    searchLabel->setBuddy(searchEdit_);
    searchEdit_->setAccessibleName(QStringLiteral("Filter element library"));
    searchEdit_->setPlaceholderText("Filter elements...");
    searchLayout->addWidget(searchLabel);
    searchLayout->addWidget(searchEdit_);
    layout->addLayout(searchLayout);

    // Element tree
    elementTree_ = new QTreeWidget(this);
    elementTree_->setObjectName(QStringLiteral("elements.tree"));
    elementTree_->setAccessibleName(QStringLiteral("Element library"));
    elementTree_->setColumnCount(2);
    elementTree_->setHeaderLabels({"Element", "I/O"});
    elementTree_->setColumnWidth(0, 180);
    elementTree_->setColumnWidth(1, 50);
    elementTree_->setUniformRowHeights(true);
    elementTree_->setSelectionMode(QAbstractItemView::SingleSelection);
    elementTree_->setSelectionBehavior(QAbstractItemView::SelectRows);
    layout->addWidget(elementTree_);

    // Element info
    descriptionLabel_ = new QLabel(this);
    descriptionLabel_->setWordWrap(true);
    descriptionLabel_->setMaximumHeight(80);
    descriptionLabel_->setFrameStyle(QFrame::StyledPanel);
    descriptionLabel_->setMargin(4);
    descriptionLabel_->setTextFormat(Qt::PlainText);
    layout->addWidget(descriptionLabel_);

    // Count label
    elementCountLabel_ = new QLabel(this);

    layout->addWidget(elementCountLabel_);

    // Set stretch
    layout->setStretchFactor(elementTree_, 1);

    // Build the element tree
    buildCategoryTree();

    // Connect signals
    connect(searchEdit_, &QLineEdit::textChanged, this, &ElementPickerPanel::onSearchTextChanged);
    connect(elementTree_, &QTreeWidget::itemDoubleClicked, this, &ElementPickerPanel::onItemDoubleClicked);
    connect(elementTree_, &QTreeWidget::itemSelectionChanged, this, &ElementPickerPanel::onItemSelectionChanged);

    setWindowTitle("Element Library");
}

ElementPickerPanel::~ElementPickerPanel() = default;

void ElementPickerPanel::refreshCatalog()
{
    buildCategoryTree();
}

void ElementPickerPanel::buildCategoryTree() {
    s7::ElementCatalogService catalog(registry_);

    elementTree_->clear();
    categoryNodes_.clear();

    // Schema is an element: keep all reusable graph elements together,
    // including user-loaded libraries with their own metadata categories.
    std::map<std::string,std::vector<s7::ElementCatalogEntry>> groups;
    for (const auto& entry : catalog.all_elements()) {
        const auto* descriptor=registry_.find(entry.type_name);
        const bool schema=descriptor && descriptor->metadata().schema_definition.has_value();
        groups[schema ? "Schema elements" : entry.category].push_back(entry);
    }
    for (const auto& [category,elements] : groups) {
        if (elements.empty()) {
            continue;
        }

        // Create category item
        auto* categoryItem = new QTreeWidgetItem();
        categoryItem->setText(0, QString::fromStdString(category));
        categoryItem->setChildIndicatorPolicy(QTreeWidgetItem::ShowIndicator);

        // Set category font (bold)
        QFont categoryFont = categoryItem->font(0);
        categoryFont.setBold(true);
        categoryItem->setFont(0, categoryFont);

        // Add elements
        for (const auto& element : elements) {
            auto* elementItem = new QTreeWidgetItem(categoryItem);
            elementItem->setText(0, QString::fromStdString(element.display_name));
            elementItem->setText(1, QString("%1→%2")
                .arg(element.input_count)
                .arg(element.output_count));

            // Store type name as data for later retrieval
            elementItem->setData(0, Qt::UserRole, QString::fromStdString(element.type_name));
            elementItem->setData(0, Qt::UserRole + 1, QString::fromStdString(element.description));
        }

        elementTree_->addTopLevelItem(categoryItem);

        // Store category node
        CategoryNode node{
            QString::fromStdString(category),
            elements
        };
        categoryNodes_.push_back(node);
    }

    // Observers are Studio diagram items, not runtime element registry entries.
    auto* observerCategory = new QTreeWidgetItem();
    observerCategory->setText(0, QStringLiteral("Observers"));
    observerCategory->setChildIndicatorPolicy(QTreeWidgetItem::ShowIndicator);
    QFont observerFont = observerCategory->font(0);
    observerFont.setBold(true);
    observerCategory->setFont(0, observerFont);
    const auto& viewerTypes = viewers::ViewerRegistry::instance().types();
    for (const auto& type : viewerTypes) {
        auto* item = new QTreeWidgetItem(observerCategory);
        item->setText(0, type.displayName);
        item->setText(1, type.portSummary);
        item->setData(0, Qt::UserRole, QStringLiteral("StudioObserver:") + type.typeId);
        item->setData(0, Qt::UserRole + 1, type.description);
    }
    elementTree_->addTopLevelItem(observerCategory);

    elementTree_->expandAll();
    applyFilter(searchEdit_->text());
    elementCountLabel_->setText(QString("Elements: %1").arg(registry_.element_count() + viewerTypes.size()));
}

void ElementPickerPanel::onSearchTextChanged(const QString& text) {
    applyFilter(text);
}

void ElementPickerPanel::applyFilter(const QString& filterText) {

    for (int i = 0; i < elementTree_->topLevelItemCount(); ++i) {
        auto* categoryItem = elementTree_->topLevelItem(i);
        bool categoryVisible = false;

        for (int j = 0; j < categoryItem->childCount(); ++j) {
            auto* elementItem = categoryItem->child(j);
            QString elementName = elementItem->text(0);
            QString elementType = elementItem->data(0, Qt::UserRole).toString();

            bool visible = elementName.contains(filterText, Qt::CaseInsensitive) ||
                          elementType.contains(filterText, Qt::CaseInsensitive);

            elementItem->setHidden(!visible);

            if (visible) {
                categoryVisible = true;
            }
        }

        categoryItem->setHidden(!categoryVisible);
    }
}

void ElementPickerPanel::onItemDoubleClicked(QTreeWidgetItem* item, int column) {
    (void)column;  // unused

    // Only respond to element items (not category items)
    QString typeName = item->data(0, Qt::UserRole).toString();
    if (typeName.isEmpty()) {
        return;
    }

    QString displayName = item->text(0);
    emit elementRequested(typeName, displayName);
}

void ElementPickerPanel::onItemSelectionChanged() {
    auto selected = elementTree_->selectedItems();
    if (selected.isEmpty()) {
        descriptionLabel_->clear();
        return;
    }

    QTreeWidgetItem* item = selected.first();
    QString description = item->data(0, Qt::UserRole + 1).toString();

    if (!description.isEmpty()) {
        descriptionLabel_->setText(description);
    } else {
        descriptionLabel_->setText("(No description available)");
    }
}

void ElementPickerPanel::dragEnterEvent(QDragEnterEvent* event) {
    if (event->mimeData()->hasFormat("text/plain")) {
        event->acceptProposedAction();
    }
}

void ElementPickerPanel::dropEvent(QDropEvent* event) {
    (void)event;  // unused for now
}

} // namespace s7studio::ui
