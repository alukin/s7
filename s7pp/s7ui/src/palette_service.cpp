#include "palette_service.hpp"
#include "scene_roles.hpp"
#include "visual_catalog.hpp"
#include <s7factory.hpp>
#include <QHeaderView>
#include <QMap>
#include <QString>
#include <QTreeWidget>
#include <QTreeWidgetItem>
namespace s7ui::ui {
namespace {
bool g_librariesRegistered = false;
} // namespace
void PaletteService::ensureLibrariesRegistered()
{
    if (g_librariesRegistered) return;
    s7::registerBuiltins();
    s7::registerFileIO();
    s7::registerMemIO();
    s7::registerNetIO();
    s7::registerProbe();
    g_librariesRegistered = true;
}
void PaletteService::populate(QTreeWidget* paletteTree,
                               const s7ui::visual::Catalog* visualCatalog)
{
    if (!paletteTree) return;
    paletteTree->clear();
    QMap<QString, QTreeWidgetItem*> roots;
    // Build from TypeDescriptors in the registry.
    const auto descriptors = s7::ElementRegistry::instance().allDescriptors();
    for (const auto& desc : descriptors) {
        if (desc.hidden) continue;
        if (desc.category == "Internal") continue;
        const QString cat = QString::fromStdString(desc.category);
        if (cat.isEmpty()) continue;
        if (!roots.contains(cat)) {
            auto* root = new QTreeWidgetItem();
            root->setText(0, cat);
            roots.insert(cat, root);
            paletteTree->addTopLevelItem(root);
        }
        const QString typeName    = QString::fromStdString(desc.type_name);
        const auto    visualSpec  = visualCatalog ? visualCatalog->specForType(typeName) : std::nullopt;
        const QString displayName = (visualSpec && !visualSpec->displayName.trimmed().isEmpty())
                                        ? visualSpec->displayName
                                        : typeName;
        QString prefix = typeName.toLower();
        prefix.remove(' ');
        auto* child = new QTreeWidgetItem();
        child->setText(0, displayName);
        const QString visualHint = visualSpec
                                       ? QString("\nshape: %1").arg(visualSpec->shape)
                                       : QString();
        child->setToolTip(0, QString::fromStdString(desc.description) + visualHint);
        child->setData(0, kRoleTypeName,    typeName);
        child->setData(0, kRoleCategory,    prefix);
        child->setData(0, kRoleInputPorts,  desc.default_input_count);
        child->setData(0, kRoleOutputPorts, desc.default_output_count);
        child->setData(0, kRoleVisualSink,  false);
        roots[cat]->addChild(child);
    }
    // Always add visual sinks under the "Visual" category.
    {
        const QString cat = "Visual";
        if (!roots.contains(cat)) {
            auto* root = new QTreeWidgetItem();
            root->setText(0, cat);
            roots.insert(cat, root);
            paletteTree->addTopLevelItem(root);
        }
        auto addSink = [&](const QString& typeName,
                           const QString& displayName,
                           const QString& tooltip,
                           const QString& prefix,
                           int inputs) {
            auto* child = new QTreeWidgetItem();
            child->setText(0, displayName);
            child->setToolTip(0, tooltip);
            child->setData(0, kRoleTypeName,    typeName);
            child->setData(0, kRoleCategory,    prefix);
            child->setData(0, kRoleInputPorts,  inputs);
            child->setData(0, kRoleOutputPorts, 0);
            child->setData(0, kRoleVisualSink,  true);
            roots[cat]->addChild(child);
        };
        addSink("ScopeSink",        "ScopeSink",
                "Oscilloscope-style visual sink",        "scope",         2);
        addSink("ComplexScopeSink", "ComplexScopeSink",
                "Complex oscilloscope sink (Re/Im)",     "complex_scope", 1);
    }
    paletteTree->expandAll();
    if (paletteTree->topLevelItemCount() > 0 &&
        paletteTree->topLevelItem(0)->childCount() > 0)
    {
        paletteTree->setCurrentItem(paletteTree->topLevelItem(0)->child(0));
    }
}
} // namespace s7ui::ui
