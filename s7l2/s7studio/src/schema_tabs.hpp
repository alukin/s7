#pragma once
#include <QObject>
#include <QPointer>
#include <boost/json.hpp>
#include <vector>

class QTabWidget;
class QUndoGroup;
namespace s7studio {
namespace diagram {class DiagramEditor;}

// Instance-local schema editing. Child edits become undoable replacements of
// the embedded schema in the parent; registry/library templates are never edited.
class SchemaTabs final : public QObject {
    Q_OBJECT
public:
    SchemaTabs(QTabWidget* tabs,diagram::DiagramEditor* root,QObject* parent=nullptr);
    [[nodiscard]] diagram::DiagramEditor* activeEditor() const;
    [[nodiscard]] QUndoGroup* undoGroup() const {return undo_;}
    diagram::DiagramEditor* open(diagram::DiagramEditor* parent,const std::string& elementId);
    bool commitAll();
    void refresh();
    [[nodiscard]] boost::json::value activeSchema() const;
    void reset();
signals:
    void editorOpened(s7studio::diagram::DiagramEditor* editor);
    void activeEditorChanged(s7studio::diagram::DiagramEditor* editor);
    void message(const QString& text);
private:
    struct Entry {
        QPointer<diagram::DiagramEditor> editor;
        QPointer<diagram::DiagramEditor> parent;
        std::string elementId;
        boost::json::value original;
    };
    boost::json::value payload(const Entry& entry) const;
    void activate(int index);
    void close(int index);
    void reload(Entry& entry,const boost::json::value& schema);
    QTabWidget* tabs_;
    diagram::DiagramEditor* root_;
    QUndoGroup* undo_;
    std::vector<Entry> entries_;
    bool syncing_=false;
};
}
