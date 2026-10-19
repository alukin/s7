#include "schema_tabs.hpp"
#include "diagram_editor.hpp"
#include "diagram/diagram_scene.hpp"
#include "scene_serializer.hpp"
#include <s7/element_registry.hpp>
#include <QSignalBlocker>
#include <QTabWidget>
#include <QTabBar>
#include <QUndoGroup>
#include <QUndoStack>
#include <algorithm>
#include <stdexcept>

namespace s7studio {
SchemaTabs::SchemaTabs(QTabWidget* tabs,diagram::DiagramEditor* root,QObject* parent)
    : QObject(parent),tabs_(tabs),root_(root),undo_(new QUndoGroup(this)) {
    undo_->addStack(root_->scene()->undoStack());
    undo_->setActiveStack(root_->scene()->undoStack());
    tabs_->setTabsClosable(true);
    tabs_->tabBar()->setTabButton(0,QTabBar::RightSide,nullptr);
    tabs_->tabBar()->setTabButton(0,QTabBar::LeftSide,nullptr);
    connect(tabs_,&QTabWidget::currentChanged,this,&SchemaTabs::activate);
    connect(tabs_,&QTabWidget::tabCloseRequested,this,&SchemaTabs::close);
}
diagram::DiagramEditor* SchemaTabs::activeEditor() const {
    if (auto* editor=qobject_cast<diagram::DiagramEditor*>(tabs_->currentWidget())) return editor;
    return root_;
}
boost::json::value SchemaTabs::payload(const Entry& entry) const {
    if (!entry.parent) return {};
    const auto node=entry.parent->scene()->elementNode(entry.elementId);
    if (!node) return {};
    if (node->element().schema.is_object()) return node->element().schema;
    const auto* descriptor=s7::ElementRegistry::instance().find(node->element().type);
    return descriptor && descriptor->metadata().schema_definition ? *descriptor->metadata().schema_definition : boost::json::value{};
}
void SchemaTabs::reload(Entry& entry,const boost::json::value& schema) {
    const QSignalBlocker block(entry.editor->scene()->undoStack());
    if (schema.as_object().contains("diagram")) {
        auto document=diagram::DiagramDocument::fromJson(schema);
        if (!schema.as_object().contains("diagram_version")) document.diagramVersion=diagram::kDiagramVersion;
        entry.editor->loadDocument(document);
    } else entry.editor->loadRuntimeSchema(schema);
    entry.editor->scene()->undoStack()->clear();
    entry.editor->scene()->undoStack()->setClean();
    entry.original=schema;
}
diagram::DiagramEditor* SchemaTabs::open(diagram::DiagramEditor* parent,const std::string& id) {
    for (const auto& entry : entries_) {
        if (entry.parent==parent && entry.elementId==id) {
            tabs_->setCurrentWidget(entry.editor);
            return entry.editor;
        }
    }
    Entry entry;
    entry.parent=parent;entry.elementId=id;
    const auto schema=payload(entry);
    if (!schema.is_object()) throw std::invalid_argument("Element has no embedded schema.");
    auto* editor=new diagram::DiagramEditor(tabs_);
    entry.editor=editor;
    try {reload(entry,schema);} catch (...) {delete editor;throw;}
    undo_->addStack(editor->scene()->undoStack());
    connect(editor->scene()->undoStack(),&QUndoStack::indexChanged,this,[this] {commitAll();});
    entries_.push_back(std::move(entry));
    const auto title=parent==root_ ? QString::fromStdString(id) :
        tabs_->tabText(tabs_->indexOf(parent))+QStringLiteral("/")+QString::fromStdString(id);
    const int index=tabs_->addTab(editor,title);
    tabs_->setTabToolTip(index,QStringLiteral("Edit this instance's embedded schema. Changes are saved with the root diagram."));
    emit editorOpened(editor);
    tabs_->setCurrentIndex(index);
    return editor;
}
bool SchemaTabs::commitAll() {
    if (syncing_) return true;
    bool success=true;
    syncing_=true;
    try {
        // Descendants were opened after ancestors. Commit inside out so nested
        // edits propagate all the way to the root in one UI operation.
        for (auto it=entries_.rbegin();it!=entries_.rend();++it) {
            if (!it->editor || it->editor->scene()->undoStack()->isClean()) continue;
            const auto current=payload(*it);
            if (!current.is_object()) {emit message(QStringLiteral("Parent element was deleted; its schema tab cannot apply edits."));success=false;continue;}
            if (current!=it->original) {emit message(QStringLiteral("Parent schema changed; reopen its tab before editing."));success=false;continue;}
            diagram::SerializationOptions options;
            if (const auto* name=it->original.as_object().if_contains("name"); name && name->is_string()) options.schemaName=QString::fromStdString(std::string(name->as_string()));
            if (const auto* dt=it->original.as_object().if_contains("dt"); dt && dt->is_number()) options.dt=dt->to_number<double>();
            const auto page=it->editor->pageLayout();
            options.sheetWidthMm=page.widthMm;options.sheetHeightMm=page.heightMm;options.sheetOrientation=page.orientation;
            const auto document=diagram::SceneSerializer{}.serialize(*it->editor->scene(),options);
            auto schema=it->original.as_object();
            schema["elements"]=document.runtimeSchema.as_object().at("elements");
            schema["connections"]=document.runtimeSchema.as_object().at("connections");
            schema["diagram"]=document.diagram;
            schema["diagram_version"]=diagram::kDiagramVersion;
            schema["sheet"]=page.toJson();
            // Exposed bindings and all other schema fields survive unchanged.
            auto node=it->parent->scene()->elementNode(it->elementId);
            auto element=node->element();element.schema=schema;
            it->parent->scene()->changeElement(node,element);
            it->original=std::move(schema);
            it->editor->scene()->undoStack()->setClean();
        }
    } catch(const std::exception& e) {success=false;emit message(QString::fromUtf8(e.what()));}
    syncing_=false;
    return success;
}
boost::json::value SchemaTabs::activeSchema() const {
    for (const auto& entry : entries_) if (entry.editor==activeEditor()) return entry.original;
    return {};
}
void SchemaTabs::refresh() {activate(tabs_->currentIndex());}
void SchemaTabs::activate(int index) {
    if (syncing_ || index<0) return;
    syncing_=true;
    auto* editor=activeEditor();
    // Refresh ancestors before descendants after a root undo or rate update.
    for (auto& entry : entries_) {
        const auto current=payload(entry);
        entry.editor->setEnabled(current.is_object());
        if (!current.is_object()) {
            if (entry.editor==editor) emit message(QStringLiteral("Parent element was deleted. Undo its deletion to edit this schema."));
        } else if (current!=entry.original && entry.editor->scene()->undoStack()->isClean()) {
            try {reload(entry,current);} catch(const std::exception& e) {emit message(QString::fromUtf8(e.what()));}
        }
    }
    undo_->setActiveStack(editor->scene()->undoStack());
    syncing_=false;
    emit activeEditorChanged(editor);
}
void SchemaTabs::close(int index) {
    auto* closing=qobject_cast<diagram::DiagramEditor*>(tabs_->widget(index));
    if (!closing || closing==root_) return;
    if (!commitAll()) return;
    std::vector<diagram::DiagramEditor*> removed{closing};
    for (const auto& entry : entries_)
        if (std::find(removed.begin(),removed.end(),entry.parent.data())!=removed.end()) removed.push_back(entry.editor);
    syncing_=true;
    for (auto* editor : removed) {
        tabs_->removeTab(tabs_->indexOf(editor));
        editor->deleteLater();
    }
    std::erase_if(entries_,[&](const Entry& entry) {return std::find(removed.begin(),removed.end(),entry.editor.data())!=removed.end();});
    syncing_=false;activate(tabs_->currentIndex());
}
void SchemaTabs::reset() {
    syncing_=true;
    tabs_->setCurrentWidget(root_);
    for (const auto& entry : entries_) if (entry.editor) {
        tabs_->removeTab(tabs_->indexOf(entry.editor));entry.editor->deleteLater();
    }
    entries_.clear();syncing_=false;activate(0);
}
}
