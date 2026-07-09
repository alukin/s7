#include "diagram_editor.hpp"

#include <qschematic/scene.hpp>
#include <qschematic/view.hpp>

#include <QVBoxLayout>

namespace s7studio::diagram {

DiagramEditor::DiagramEditor(QWidget* parent)
    : QWidget(parent),
      _scene(std::make_unique<QSchematic::Scene>(this))
{
    auto layout = new QVBoxLayout(this);
    
    // Create editor view
    _view = new QSchematic::View(this);
    _view->setScene(_scene.get());
    _view->setRenderHint(QPainter::Antialiasing);
    
    layout->addWidget(_view);
    layout->setContentsMargins(0, 0, 0, 0);
    setLayout(layout);
}

DiagramEditor::~DiagramEditor() = default;

QSchematic::Scene* DiagramEditor::scene() {
    return _scene.get();
}

const QSchematic::Scene* DiagramEditor::scene() const {
    return _scene.get();
}

} // namespace s7studio::diagram
