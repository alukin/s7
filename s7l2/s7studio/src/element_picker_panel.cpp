// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-S7-plugin-permission

#include "element_picker_panel.hpp"
#include "element_documentation.hpp"
#include "library_element.hpp"

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
#include <QMimeData>
#include <QApplication>
#include <QDrag>
#include <QMouseEvent>
#include <QFont>
#include <QContextMenuEvent>
#include <QKeyEvent>
#include <QMenu>
#include <QPointer>
#include <QSizePolicy>
#include <QRadioButton>
#include <QDockWidget>
#include <QToolButton>
#include <QStyle>
#include <QSettings>
#include <QSignalBlocker>
#include <QRegularExpression>
#include <QStringList>
#include <algorithm>
#include <map>
#include <vector>

namespace s7studio::ui {
namespace {
constexpr int descriptionRole = Qt::UserRole + 1;
constexpr int searchRole = Qt::UserRole + 2;
const auto viewSetting = QStringLiteral("elements/view");

QString categoryName(const QString& id)
{
    static const std::map<QString, QString> names{
        {"sources", "Sources"}, {"math", "Math & Functions"},
        {"memory_integration", "Memory & Integration"},
        {"signal_processing", "Signal Processing"}, {"control", "Control & Plants"},
        {"types_complex", "Types & Complex Signals"},
        {"io", "Files & Network"}, {"network", "Files & Network"}, {"sound_video", "Sound and Video"}, {"radio", "Radio"}, {"observers", "Observers"}};
    const auto it = names.find(id);
    return it != names.end() ? it->second : id.isEmpty() ? QStringLiteral("Uncategorized") : id;
}

QString libraryName(const QString& id)
{
    static const std::map<QString, QString> names{
        {"s7core", "S7 Core"}, {"s7_standard_schema_elements", "S7 Schema Library"},
        {"s7io", "S7 File I/O"}, {"s7net", "S7 Network"}, {"s7noise", "S7 Noise"},
        {"s7spectral", "S7 Spectral"}, {"s7wavelet", "S7 Wavelet"},
        {"s7audio", "S7 Audio"}, {"s7sdr", "S7 SDR"}, {"s7radio", "S7 Radio"}, {"s7studio", "S7 Studio"}};
    const auto it = names.find(id);
    return it != names.end() ? it->second : id.isEmpty() ? QStringLiteral("Unspecified library") : id;
}

struct PickerEntry {
    QString identifier;
    QString name;
    QString description;
    QString ports;
    QString category;
    QString library;
    QString kind;
    QStringList tags;
};

class LibraryTree final : public QTreeWidget {
public:
    LibraryTree(const s7::ElementRegistry& registry, QWidget* parent)
        : QTreeWidget(parent), registry_(registry) {}
protected:
    QStringList mimeTypes() const override { return {QString::fromLatin1(libraryElementMimeType)}; }
    QMimeData* mimeData(const QList<QTreeWidgetItem*>& items) const override
    {
        if (items.size() != 1 || items.front()->isHidden() || items.front()->childCount() != 0) return nullptr;
        return libraryElementMimeData(items.front()->data(0, Qt::UserRole).toString());
    }
    Qt::DropActions supportedDropActions() const override { return Qt::CopyAction; }
    void startDrag(Qt::DropActions) override
    {
        auto* data = mimeData(selectedItems());
        if (!data) return;
        QPointer<QDrag> drag = new QDrag(this);
        drag->setMimeData(data);
        drag->exec(Qt::CopyAction, Qt::CopyAction);
        if (drag) drag->deleteLater();
    }
    void keyPressEvent(QKeyEvent* event) override
    {
        if (event->key() == Qt::Key_Menu || (event->key() == Qt::Key_F10 && event->modifiers() == Qt::ShiftModifier)) {
            QContextMenuEvent context(QContextMenuEvent::Keyboard, {}, {});
            contextMenuEvent(&context);
            event->accept();
            return;
        }
        QTreeWidget::keyPressEvent(event);
    }
    void contextMenuEvent(QContextMenuEvent* event) override
    {
        const bool keyboard = event->reason() == QContextMenuEvent::Keyboard;
        const auto* item = keyboard ? currentItem() : itemAt(event->pos());
        if (!item) return;
        auto type = item->data(0, Qt::UserRole).toString();
        if (type.isEmpty()) return;
        if (type.startsWith("StudioObserver:")) type.remove(0, 15);
        QMenu menu(this);
        menu.setObjectName(QStringLiteral("elements.contextMenu"));
        auto* info = menu.addAction(QStringLiteral("Element Information…"));
        info->setObjectName(QStringLiteral("elements.information"));
        connect(info, &QAction::triggered, this, [this, type] { showElementInformation(registry_, type, this); });
        menu.exec(keyboard ? viewport()->mapToGlobal(visualItemRect(item).center()) : event->globalPos());
        event->accept();
    }
private:
    const s7::ElementRegistry& registry_;
};
}

ElementPickerPanel::ElementPickerPanel(const s7::ElementRegistry& registry, QWidget* parent)
    : QWidget(parent), registry_(registry)
{
    // Build UI
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(8);

    byLibrary_ = QSettings{}.value(viewSetting).toString() == "library";

    // Both views share the same search and insertion identifiers.
    auto* searchLayout = new QHBoxLayout();
    auto* searchLabel = new QLabel("&Search:", this);
    searchEdit_ = new QLineEdit(this);
    searchEdit_->setObjectName(QStringLiteral("elements.search"));
    searchLabel->setBuddy(searchEdit_);
    searchEdit_->setAccessibleName(QStringLiteral("Filter element library"));
    searchEdit_->setPlaceholderText("Search elements, categories, libraries…");
    searchEdit_->setClearButtonEnabled(true);
    searchLayout->addWidget(searchLabel);
    searchLayout->addWidget(searchEdit_);
    layout->addLayout(searchLayout);

    // Element tree
    elementTree_ = new LibraryTree(registry_, this);
    elementTree_->setObjectName(QStringLiteral("elements.tree"));
    elementTree_->setAccessibleName(QStringLiteral("Element library"));
    elementTree_->setColumnCount(2);
    elementTree_->setHeaderLabels({"Element", "I/O"});
    elementTree_->header()->setStretchLastSection(false);
    elementTree_->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    elementTree_->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    elementTree_->setUniformRowHeights(true);
    elementTree_->setSelectionMode(QAbstractItemView::SingleSelection);
    elementTree_->setSelectionBehavior(QAbstractItemView::SelectRows);
    elementTree_->setDragEnabled(true);
    elementTree_->setDragDropMode(QAbstractItemView::DragOnly);
    elementTree_->setDefaultDropAction(Qt::CopyAction);
    layout->addWidget(elementTree_);

    // Element info
    descriptionLabel_ = new QLabel(this);
    descriptionLabel_->setObjectName(QStringLiteral("elements.description"));
    descriptionLabel_->setWordWrap(true);
    // Reserve the details area so changing selection cannot move the tree rows.
    descriptionLabel_->setFixedHeight(104);
    // A long description must wrap in the existing dock width rather than
    // move the library/canvas boundary underneath the pointer during a drag.
    descriptionLabel_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    descriptionLabel_->setFrameStyle(QFrame::StyledPanel);
    descriptionLabel_->setMargin(4);
    descriptionLabel_->setTextFormat(Qt::PlainText);
    layout->addWidget(descriptionLabel_);

    // Count label
    elementCountLabel_ = new QLabel(this);
    elementCountLabel_->setObjectName(QStringLiteral("elements.count"));

    layout->addWidget(elementCountLabel_);

    // Set stretch
    layout->setStretchFactor(elementTree_, 1);

    // Build the element tree
    rebuildTree();

    // Connect signals
    connect(searchEdit_, &QLineEdit::textChanged, this, &ElementPickerPanel::applyFilter);
    connect(elementTree_, &QTreeWidget::itemDoubleClicked, this, &ElementPickerPanel::onItemDoubleClicked);
    connect(elementTree_, &QTreeWidget::itemSelectionChanged, this, &ElementPickerPanel::onItemSelectionChanged);

    setWindowTitle("Element Library");
}

ElementPickerPanel::~ElementPickerPanel() = default;

QWidget* ElementPickerPanel::createTitleBar(QDockWidget* dock)
{
    auto* titleBar = new QWidget(dock);
    titleBar->setObjectName(QStringLiteral("elements.titleBar"));
    auto* layout = new QHBoxLayout(titleBar);
    layout->setContentsMargins(4, 1, 2, 1);
    layout->setSpacing(4);
    auto* title = new QLabel(dock->windowTitle(), titleBar);
    title->setObjectName(QStringLiteral("elements.title"));
    title->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    title->setAttribute(Qt::WA_TransparentForMouseEvents);
    layout->addWidget(title, 1);
    auto* category = new QRadioButton(QStringLiteral("Cat."), titleBar);
    category->setObjectName(QStringLiteral("elements.byCategory"));
    category->setAccessibleName(QStringLiteral("Browse elements by category"));
    category->setToolTip(QStringLiteral("Group elements by category"));
    auto* library = new QRadioButton(QStringLiteral("Lib."), titleBar);
    library->setObjectName(QStringLiteral("elements.byLibrary"));
    library->setAccessibleName(QStringLiteral("Browse elements by library"));
    library->setToolTip(QStringLiteral("Group elements by providing library"));
    category->setChecked(!byLibrary_);
    library->setChecked(byLibrary_);
    layout->addWidget(category);
    layout->addWidget(library);
    connect(library, &QRadioButton::toggled, this, [this](bool checked) {
        byLibrary_ = checked;
        QSettings{}.setValue(viewSetting, checked ? "library" : "category");
        rebuildTree();
    });
    auto* close = new QToolButton(titleBar);
    close->setObjectName(QStringLiteral("elements.close"));
    close->setAutoRaise(true);
    close->setIcon(dock->style()->standardIcon(QStyle::SP_TitleBarCloseButton));
    close->setAccessibleName(QStringLiteral("Close Element Library"));
    close->setToolTip(QStringLiteral("Close Element Library"));
    layout->addWidget(close);
    connect(close, &QToolButton::clicked, dock, &QDockWidget::close);
    return titleBar;
}

void ElementPickerPanel::refreshCatalog()
{
    rebuildTree();
}

void ElementPickerPanel::rebuildTree()
{
    const auto selected = elementTree_->currentItem()
        ? elementTree_->currentItem()->data(0, Qt::UserRole).toString() : QString{};
    // Never instantiate runtime elements while browsing metadata.
    std::vector<PickerEntry> entries;
    for (const auto& descriptor : registry_.all_descriptors()) {
        const auto& m = descriptor.metadata();
        entries.push_back({QString::fromStdString(m.type_name), QString::fromStdString(m.display_name),
            QString::fromStdString(m.description), {}, QString::fromStdString(m.category),
            QString::fromStdString(m.library), m.schema_definition ? "Schema" : "Primitive", {}});
        for (const auto& tag : m.tags) entries.back().tags << QString::fromStdString(tag);
    }
    const s7::ElementCatalogService catalog(registry_);
    for (auto& entry : entries) {
        const auto* summary = catalog.find_element(entry.identifier.toStdString());
        entry.ports = QString("%1→%2").arg(summary->input_count).arg(summary->output_count);
    }
    for (const auto& viewer : viewers::ViewerRegistry::instance().types()) {
        const auto& m = viewer.documentation;
        entries.push_back({QStringLiteral("StudioObserver:") + viewer.typeId, viewer.displayName,
            viewer.description, viewer.portSummary, QStringLiteral("observers"),
            m ? QString::fromStdString(m->library) : QString{}, QStringLiteral("Observer"), {}});
        if (m) for (const auto& tag : m->tags) entries.back().tags << QString::fromStdString(tag);
    }
    const bool byLibrary = byLibrary_;
    const auto groupKey = [byLibrary](const PickerEntry& e) {
        // Library identity is independent of its display label and category.
        return byLibrary ? e.library : categoryName(e.category);
    };
    const auto groupName = [byLibrary](const QString& key) { return byLibrary ? libraryName(key) : key; };
    std::sort(entries.begin(), entries.end(), [&](const auto& a, const auto& b) {
        const auto aKey = groupKey(a), bKey = groupKey(b);
        const auto aName = groupName(aKey), bName = groupName(bKey);
        if (!byLibrary && aName != bName) {
            static const QStringList order{"Sources", "Math & Functions", "Memory & Integration",
                "Signal Processing", "Control & Plants", "Types & Complex Signals", "Files & Network", "Sound and Video", "Radio", "Observers"};
            const auto rank = [&](const QString& name) { const auto i = order.indexOf(name); return i < 0 ? order.size() : i; };
            if (rank(aName) != rank(bName)) return rank(aName) < rank(bName);
        }
        if (aName != bName) return aName < bName;
        if (aKey != bKey) return aKey < bKey;
        if (a.name != b.name) return a.name < b.name;
        return a.identifier < b.identifier;
    });
    {
        const QSignalBlocker block(elementTree_);
        elementTree_->clear();
        QTreeWidgetItem* group = nullptr;
        QString previousKey;
        QTreeWidgetItem* selection = nullptr;
        for (const auto& e : entries) {
            const auto key = groupKey(e);
            if (!group || key != previousKey) {
                group = new QTreeWidgetItem(elementTree_);
                group->setFlags(group->flags() & ~Qt::ItemIsDragEnabled);
                group->setText(0, groupName(key));
                group->setFirstColumnSpanned(true);
                group->setToolTip(0, key);
                auto font = group->font(0); font.setBold(true); group->setFont(0, font);
                previousKey = key;
            }
            auto* item = new QTreeWidgetItem(group);
            item->setFlags(item->flags() | Qt::ItemIsDragEnabled);
            item->setText(0, e.name);
            item->setText(1, e.ports);
            item->setData(0, Qt::UserRole, e.identifier);
            const auto details = QString("%1 · %2\n%3 · %4\n%5")
                .arg(e.name, e.kind, libraryName(e.library), categoryName(e.category), e.description);
            item->setData(0, descriptionRole, details);
            for (int column = 0; column < 2; ++column) item->setToolTip(column, details);
            QStringList search{e.name, e.identifier, e.description, e.category, categoryName(e.category),
                               e.library, libraryName(e.library), e.kind};
            search.append(e.tags);
            item->setData(0, searchRole, search.join(' '));
            if (e.identifier == selected) selection = item;
        }
        elementTree_->expandAll();
        applyFilter(searchEdit_->text());
        if (selection) {
            elementTree_->setCurrentItem(selection);
            if (!selection->isHidden()) elementTree_->scrollToItem(selection);
        }
    }
    onItemSelectionChanged();
}

void ElementPickerPanel::applyFilter(const QString& text)
{
    const auto words = text.trimmed().split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
    int total = 0, matches = 0;
    for (int i = 0; i < elementTree_->topLevelItemCount(); ++i) {
        auto* group = elementTree_->topLevelItem(i);
        bool visibleGroup = false;
        for (int j = 0; j < group->childCount(); ++j) {
            auto* item = group->child(j);
            const auto searchable = item->data(0, searchRole).toString();
            const bool visible = std::all_of(words.begin(), words.end(), [&](const auto& word) {
                return searchable.contains(word, Qt::CaseInsensitive);
            });
            item->setHidden(!visible);
            visibleGroup |= visible;
            ++total;
            if (visible) ++matches;
        }
        group->setHidden(!visibleGroup);
    }
    elementCountLabel_->setText(words.isEmpty() ? QString("Elements: %1").arg(total)
                                              : QString("Elements: %1 / %2").arg(matches).arg(total));
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
    QString description = item->data(0, descriptionRole).toString();

    if (!description.isEmpty()) {
        descriptionLabel_->setText(description);
    } else {
        descriptionLabel_->setText("(No description available)");
    }
}

} // namespace s7studio::ui
