// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-S7-plugin-permission
#include "element_picker_panel.hpp"
#include "library_element.hpp"
#include "viewers/viewer_registry.hpp"
#include <s7/builtin_element_registry.hpp>
#include <s7/schema_library.hpp>
#include <QApplication>
#include <QRadioButton>
#include <QDockWidget>
#include <QToolButton>
#include <QLabel>
#include <QLineEdit>
#include <QMimeData>
#include <QSettings>
#include <QTemporaryDir>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>
#include <iostream>
#include <set>
#include <stdexcept>

namespace {
void require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
QTreeWidgetItem* entry(QTreeWidget* tree, const QString& id) {
    QTreeWidgetItemIterator it(tree);
    while (*it) {
        if ((*it)->data(0, Qt::UserRole).toString() == id) return *it;
        ++it;
    }
    throw std::runtime_error("Missing picker entry: " + id.toStdString());
}
std::set<QString> identifiers(QTreeWidget* tree) {
    std::set<QString> result;
    QTreeWidgetItemIterator it(tree);
    while (*it) {
        const auto id = (*it)->data(0, Qt::UserRole).toString();
        if (!id.isEmpty()) require(result.insert(id).second, "An element appeared more than once");
        ++it;
    }
    return result;
}
void registerFixture(s7::ElementRegistry& registry, const std::string& type, const std::string& library) {
    s7::ElementMetadata m;
    m.type_name = type; m.display_name = "Fixture element"; m.category = "math"; m.library = library;
    m.description = "Plugin fixture with no executable factory"; m.tags = {"weighted-combination"};
    registry.register_element(s7::ElementDescriptor(type, [](const s7::ElementCreationContext&) -> std::unique_ptr<s7::Element> {
        throw std::runtime_error("Browsing must never instantiate an element");
    }, m));
}
}

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    try {
        QTemporaryDir settings;
        require(settings.isValid(), "Temporary settings directory");
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settings.path());
        QApplication::setOrganizationName("s7-test"); QApplication::setApplicationName("element-picker");
        auto& registry = s7::ElementRegistry::instance();
        s7::register_builtin_elements(registry);
        registerFixture(registry, "ExternalFixture", "third_party_library");
        registerFixture(registry, "s7corePretender", "");
        s7::SchemaLibraryLoader::register_library({"my_control_library", {{"MyPhaseDifference", "My phase difference",
            "A reusable user graph", "control", "1.0.0", *registry.find("PhaseDifference")->metadata().schema_definition}}}, registry);
        QDockWidget dock("Element Library");
        s7studio::ui::ElementPickerPanel panel(registry);
        dock.setWidget(&panel);
        dock.setTitleBarWidget(panel.createTitleBar(&dock));
        auto* tree = panel.findChild<QTreeWidget*>("elements.tree");
        auto* category = dock.findChild<QRadioButton*>("elements.byCategory");
        auto* library = dock.findChild<QRadioButton*>("elements.byLibrary");
        auto* search = panel.findChild<QLineEdit*>("elements.search");
        auto* count = panel.findChild<QLabel*>("elements.count");
        require(tree && category && library && search && count, "Picker controls");
        require(category->isChecked() && !library->isChecked(), "Category view is the initial default");
        dock.resize(280, 660); dock.show(); QApplication::processEvents();
        require(tree->columnViewportPosition(1) + tree->columnWidth(1) <= tree->viewport()->width(), "Element list fits the default dock width");
        require(tree->topLevelItem(0)->text(0) == "Sources", "Functional browsing starts with sources");
        require(category->parentWidget() == dock.titleBarWidget() && library->parentWidget() == dock.titleBarWidget(), "Grouping controls share the dock title bar");
        const auto original = identifiers(tree);
        require(original.size() == registry.element_count() + s7studio::viewers::ViewerRegistry::instance().types().size(), "All elements and observers appear once");
        require(entry(tree, "PIDController")->parent()->text(0) == "Control & Plants", "PID is classified by function");
        require(entry(tree, "MyPhaseDifference")->parent()->text(0) == "Control & Plants", "User schema category is honored independently of its library");
        require(entry(tree, "Trig")->parent()->text(0) == "Math & Functions", "Trigonometry is a math function");
        require(entry(tree, "SurfacePlane")->parent()->text(0) == "Math & Functions", "Schema surfaces are math functions");
        require(entry(tree, "Delay")->parent()->text(0) == "Memory & Integration", "Delay has a memory category");
        require(entry(tree, "RMSDetector")->parent()->text(0) == "Signal Processing", "RMS has a processing category");
        require(entry(tree, "Constant")->parent()->text(0) == "Sources", "Constants are sources");
        require(entry(tree, "PIDController")->toolTip(0).contains("· Schema\n"), "Schemas have a separate implementation label");
        require(entry(tree, "Gain")->toolTip(0).contains("· Primitive\n"), "Primitives have a separate implementation label");
        require(entry(tree, "StudioObserver:Scope")->toolTip(0).contains("· Observer\n"), "Observers remain identifiable");

        QStringList requests;
        QObject::connect(&panel, &s7studio::ui::ElementPickerPanel::elementRequested, &panel,
                         [&](const QString& id, const QString&) { requests << id; });
        auto* pid = entry(tree, "PIDController");
        tree->setCurrentItem(pid);
        require(panel.findChild<QLabel*>("elements.description")->text().contains("PIDController · Schema"), "Selection details identify the element and implementation");
        search->setText("  pid   schema  ");
        require(!pid->isHidden() && count->text().startsWith("Elements: 1 /"), "Search supports multiple terms and implementation kind");
        library->click();
        require(identifiers(tree) == original, "Both views contain the same insertion identities");
        require(search->text() == "  pid   schema  ", "Switching view preserves the search");
        pid = entry(tree, "PIDController");
        require(tree->currentItem() == pid && !pid->isHidden(), "Switching view preserves selection");
        require(pid->parent()->text(0) == "S7 Schema Library" && pid->parent()->toolTip(0) == "s7_standard_schema_elements", "Schema library label and stable identity");
        require(entry(tree, "Gain")->parent()->text(0) == "S7 Core", "Built-in library ownership");
        require(entry(tree, "ExternalFixture")->parent()->text(0) == "third_party_library", "Plugin declares its own library");
        require(entry(tree, "s7corePretender")->parent()->text(0) == "Unspecified library", "Missing ownership is never guessed from the type name");
        require(entry(tree, "MyPhaseDifference")->parent()->text(0) == "my_control_library", "User-loaded schema library ownership");
        require(entry(tree, "StudioObserver:Scope")->parent()->text(0) == "S7 Studio", "Observer library ownership");
        require(requests.isEmpty(), "Browsing never inserts elements");
        std::unique_ptr<QMimeData> drag(s7studio::ui::libraryElementMimeData(pid->data(0, Qt::UserRole).toString()));
        require(s7studio::ui::libraryElementFromMimeData(drag.get()) == "PIDController", "Library view retains the drag identifier");
        require(QMetaObject::invokeMethod(tree, "itemDoubleClicked", Qt::DirectConnection,
                Q_ARG(QTreeWidgetItem*, pid), Q_ARG(int, 0)), "Double-click dispatch");
        require(requests == QStringList{"PIDController"}, "Library view inserts the same element type");
        category->click();
        pid = entry(tree, "PIDController");
        require(tree->currentItem() == pid && pid->parent()->text(0) == "Control & Plants", "Return to category keeps selection");
        require(QMetaObject::invokeMethod(tree, "itemDoubleClicked", Qt::DirectConnection,
                Q_ARG(QTreeWidgetItem*, pid), Q_ARG(int, 0)), "Category double-click dispatch");
        require(requests == QStringList{"PIDController", "PIDController"}, "Category view inserts the same element type");

        search->setText("third_party_library weighted-combination");
        require(!entry(tree, "ExternalFixture")->isHidden(), "Search includes library identity and tags in either view");
        search->setText("my_control_library schema");
        require(!entry(tree, "MyPhaseDifference")->isHidden(), "Search includes loaded libraries");
        search->setText("missing-picker-element");
        require(count->text().startsWith("Elements: 0 /"), "Empty search results have an accurate count");
        for (int i = 0; i < tree->topLevelItemCount(); ++i) require(tree->topLevelItem(i)->isHidden(), "Groups with no matches are hidden");
        search->clear(); library->click();
        tree->setCurrentItem(entry(tree, "ExternalFixture"));
        search->setText("fixture");
        registerFixture(registry, "AddedLater", "another_library");
        panel.refreshCatalog();
        require(entry(tree, "AddedLater")->parent()->text(0) == "another_library", "Catalog refresh discovers new providers");
        require(tree->currentItem()->data(0, Qt::UserRole).toString() == "ExternalFixture" && search->text() == "fixture", "Refresh preserves search and selection");
        registry.unregister({"ExternalFixture"}); panel.refreshCatalog();
        require(!identifiers(tree).contains("ExternalFixture"), "Removed registrations disappear");
        require(panel.findChild<QLabel*>("elements.description")->text().isEmpty(), "Removed selections leave no stale details");
        QDockWidget reopenedDock("Element Library");
        s7studio::ui::ElementPickerPanel reopened(registry);
        reopenedDock.setWidget(&reopened);
        reopenedDock.setTitleBarWidget(reopened.createTitleBar(&reopenedDock));
        require(reopenedDock.findChild<QRadioButton*>("elements.byLibrary")->isChecked(), "View preference survives reopening");
        QSettings{}.setValue("elements/view", "unknown");
        QDockWidget fallbackDock("Element Library");
        s7studio::ui::ElementPickerPanel fallback(registry);
        fallbackDock.setWidget(&fallback);
        fallbackDock.setTitleBarWidget(fallback.createTitleBar(&fallbackDock));
        require(fallbackDock.findChild<QRadioButton*>("elements.byCategory")->isChecked(), "Unknown preferences fall back to category");
        dock.findChild<QToolButton*>("elements.close")->click();
        require(dock.isHidden(), "The compact title bar retains the close control");
        std::cout << "Element picker: category/library identity, functional grouping, search, selection, insertion, refresh and settings passed\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
