#include "diagram_editor.hpp"

#include "scene_serializer.hpp"

#include <qschematic/scene.hpp>
#include <qschematic/view.hpp>

#include <QGraphicsScene>
#include <QResizeEvent>
#include <QShowEvent>
#include <QSizePolicy>
#include <QTimer>
#include <QVBoxLayout>

namespace s7studio::diagram {

DiagramEditor::DiagramEditor(QWidget* parent)
    : QWidget(parent),
      _scene(std::make_unique<QSchematic::Scene>(this))
{
    auto layout = new QVBoxLayout(this);
    setMinimumSize(720, 520);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    // Create editor view
    _view = new QSchematic::View(this);
    _view->setScene(_scene.get());
    _view->setRenderHint(QPainter::Antialiasing);
    _view->setDragMode(QGraphicsView::RubberBandDrag);
    _view->setMinimumSize(640, 480);
    _view->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    layout->addWidget(_view);
    layout->setContentsMargins(0, 0, 0, 0);
    setLayout(layout);
}

DiagramEditor::~DiagramEditor() = default;

void DiagramEditor::clearDiagram()
{
    _scene->clear();
}

void DiagramEditor::loadDocument(const DiagramDocument& document)
{
    SceneSerializer serializer;
    serializer.deserialize(document, *_scene);
    scheduleFitToContents();
}

void DiagramEditor::loadRuntimeSchema(const boost::json::value& schemaJson)
{
    DiagramDocument document;
    document.runtimeSchema = schemaJson;

    if (schemaJson.is_object()) {
        const auto& object = schemaJson.as_object();
        if (const auto it = object.find("name"); it != object.end() && it->value().is_string()) {
            document.id = QString::fromUtf8(it->value().as_string().c_str());
        }
    }

    loadDocument(document);
}

void DiagramEditor::fitToContents()
{
    if (!_view || !_scene) {
        return;
    }

    if (_view->viewport()->width() < 32 || _view->viewport()->height() < 32) {
        _pendingFitToContents = true;
        return;
    }

    const auto bounds = _scene->itemsBoundingRect();
    if (!bounds.isValid() || bounds.isEmpty()) {
        _scene->setSceneRect(QRectF(-200.0, -150.0, 400.0, 300.0));
        _view->centerOn(0.0, 0.0);
        _pendingFitToContents = false;
        return;
    }

    const auto paddedBounds = bounds.adjusted(-60.0, -60.0, 60.0, 60.0);
    _scene->setSceneRect(paddedBounds);
    _view->fitInView();
    _pendingFitToContents = false;
}

void DiagramEditor::scheduleFitToContents()
{
    _pendingFitToContents = true;
    QTimer::singleShot(0, this, [this] {
        if (_pendingFitToContents) {
            fitToContents();
        }
    });
}

void DiagramEditor::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);

    if (_pendingFitToContents) {
        scheduleFitToContents();
    }
}

void DiagramEditor::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);

    if (_pendingFitToContents && event->size().width() > 32 && event->size().height() > 32) {
        scheduleFitToContents();
    }
}

QSchematic::Scene* DiagramEditor::scene() {
    return _scene.get();
}

const QSchematic::Scene* DiagramEditor::scene() const {
    return _scene.get();
}

} // namespace s7studio::diagram
