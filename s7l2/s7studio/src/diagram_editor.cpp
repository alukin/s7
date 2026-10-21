#include "diagram_editor.hpp"

#include "diagram_ruler.hpp"
#include "diagram/diagram_scene.hpp"
#include "scene_serializer.hpp"

#include <qschematic/view.hpp>

#include <QEvent>
#include <QGraphicsRectItem>
#include <QGraphicsItem>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QPen>
#include <QBrush>
#include <QColor>
#include <QResizeEvent>
#include <QScrollBar>
#include <QShowEvent>
#include <QSizePolicy>
#include <QTimer>
#include <QShortcut>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace s7studio::diagram {

namespace {

class DiagramView final : public QSchematic::View
{
public:
    using QSchematic::View::View;
    using QAbstractScrollArea::setViewportMargins;

    void setDiagramScene(DiagramScene* scene)
    {
        diagramScene_ = scene;
        setScene(scene);
    }

protected:
    // The diagram scene's keys first; QSchematic's own Wire mode (Ctrl+W)
    // becomes the scene's DrawMode.
    void keyPressEvent(QKeyEvent* event) override
    {
        if (auto* editor = qobject_cast<DiagramEditor*>(parentWidget())) {
            if (event->matches(QKeySequence::Copy)) { editor->copySelection(); return; }
            if (event->matches(QKeySequence::Cut)) { editor->cutSelection(); return; }
            if (event->matches(QKeySequence::Paste)) { editor->pasteSelection(); return; }
            if (event->matches(QKeySequence::SelectAll)) { editor->selectAll(); return; }
            if (event->key() == Qt::Key_F4 || event->key() == Qt::Key_Return) { editor->editSelectedElement(); return; }
        }
        if (diagramScene_) {
            if (event->modifiers().testFlag(Qt::ControlModifier) && event->key() == Qt::Key_W) {
                diagramScene_->setMode(diagramScene_->mode() == DiagramScene::DrawMode ? QSchematic::Scene::NormalMode
                                                                                       : DiagramScene::DrawMode);
                return;
            }
            if (diagramScene_->handleKey(event->key(), event->modifiers())) {
                return;
            }
        }
        QSchematic::View::keyPressEvent(event);
    }

private:
    DiagramScene* diagramScene_ = nullptr;
};

constexpr double kSceneUnitsPerMm = 96.0 / 25.4;
constexpr double kPageMarginMm = 20.0;
constexpr int kRulerThicknessPx = 22;

auto sceneUnitsFromMm(const double millimeters) -> double
{
    return millimeters * kSceneUnitsPerMm;
}

auto orientedPageRectMm(const DiagramPageLayout& pageLayout) -> QRectF
{
    if (pageLayout.orientation == PageOrientation::Portrait) {
        return {0.0, 0.0, std::min(pageLayout.widthMm, pageLayout.heightMm), std::max(pageLayout.widthMm, pageLayout.heightMm)};
    }

    return {0.0, 0.0, std::max(pageLayout.widthMm, pageLayout.heightMm), std::min(pageLayout.widthMm, pageLayout.heightMm)};
}

auto orientedPageRect(const DiagramPageLayout& pageLayout) -> QRectF
{
    const auto rectMm = orientedPageRectMm(pageLayout);
    return {0.0, 0.0, sceneUnitsFromMm(rectMm.width()), sceneUnitsFromMm(rectMm.height())};
}

bool fitRectInView(QSchematic::View* view, const QRectF& rect)
{
    if (!view || rect.isEmpty() || view->viewport()->width() < 32 || view->viewport()->height() < 32) {
        return false;
    }

    const double scale = std::min(view->viewport()->width() / rect.width(), view->viewport()->height() / rect.height());
    view->setZoomValue(std::clamp(scale, static_cast<double>(view->zoom_factor_min), static_cast<double>(view->zoom_factor_max)));
    view->centerOn(rect.center());
    return true;
}

} // namespace

DiagramEditor::DiagramEditor(QWidget* parent)
    : QWidget(parent),
      _scene(std::make_unique<DiagramScene>(this))
{

    auto layout = new QVBoxLayout(this);
    setMinimumSize(240, 180);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    // Create editor view
    auto* diagramView = new DiagramView(this);
    _view = diagramView;
    _view->setAccessibleName(QStringLiteral("Schematic diagram"));
    _view->setAccessibleDescription(QStringLiteral("Select elements, Ctrl+C/Ctrl+V to copy/paste, Delete to remove, F4 or Enter for properties. Ctrl+8 fits diagram, Ctrl+0 resets zoom."));
    // A complete sheet must fit even in a narrow editor or a short viewport.
    _view->zoom_factor_min = 0.01;
    diagramView->setDiagramScene(_scene.get());
    _view->setRenderHint(QPainter::Antialiasing);
    _view->setDragMode(QGraphicsView::RubberBandDrag);
    _view->setBackgroundBrush(QColor(0xd8, 0xd8, 0xd8));
    _view->setAlignment(Qt::AlignCenter);
    _view->setMinimumSize(160, 120);
    _view->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    diagramView->setViewportMargins(kRulerThicknessPx, kRulerThicknessPx, 0, 0);
    _view->viewport()->installEventFilter(this);
    connect(_scene.get(), &DiagramScene::diagramChanged, this, &DiagramEditor::updateSceneRectFromPage);

    horizontalRuler_ = new DiagramRuler(_view, DiagramRuler::Axis::Horizontal, _view);
    verticalRuler_ = new DiagramRuler(_view, DiagramRuler::Axis::Vertical, _view);
    rulerCorner_ = new QWidget(_view);
    rulerCorner_->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    rulerCorner_->setStyleSheet("background: #f5f5f5; border-right: 1px solid #b8b8b8; border-bottom: 1px solid #b8b8b8;");

    connect(_view, &QSchematic::View::zoomChanged, this, [this] {
        updateRulers();
    });
    connect(_view->horizontalScrollBar(), &QScrollBar::valueChanged, this, [this] {
        updateRulers();
    });
    connect(_view->verticalScrollBar(), &QScrollBar::valueChanged, this, [this] {
        updateRulers();
    });

    layout->addWidget(_view);
    layout->setContentsMargins(0, 0, 0, 0);
    setLayout(layout);

    updateRulerLayout();
    rebuildPageFrame();
}

void DiagramEditor::copySelection() { _scene->copySelection(); }
void DiagramEditor::cutSelection() {
    if (_scene->copySelection()) _scene->deleteSelection();
}
void DiagramEditor::pasteSelection() {
    if (_scene->pasteSelection(suggestedNewItemPosition(QSizeF(240, 180)))) {
        updateSceneRectFromPage();
        for (const auto& node : _scene->elementNodes()) if (node->isSelected()) { showItem(node.get()); break; }
    }
}
void DiagramEditor::selectAll() { _scene->selectAll(); }
void DiagramEditor::editSelectedElement() {
    for (const auto& node : _scene->elementNodes()) if (node->isSelected()) {
        Q_EMIT elementNodeDoubleClicked(QString::fromStdString(node->id()), QString::fromStdString(node->element().type), static_cast<int>(node->inputs().size()));
        return;
    }
}

DiagramEditor::~DiagramEditor() = default;

void DiagramEditor::clearDiagram()
{
    pageFrame_ = nullptr; // deleted with the scene's content
    _scene->clearDiagram();
    rebuildPageFrame();
}

void DiagramEditor::loadDocument(const DiagramDocument& document, const std::function<void(int)>& progress)
{
    pageFrame_ = nullptr; // deleted with the scene's content
    try {
        SceneSerializer{}.deserialize(document, *_scene, progress);
    } catch (...) {
        clearDiagram();
        throw;
    }
    setPageLayout(document.pageLayout);
    scheduleInitialPageFit();
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

void DiagramEditor::showItem(QGraphicsItem* item)
{
    if (!item || !_view) {
        return;
    }
    _scene->clearSelection();
    item->setSelected(true);
    _view->centerOn(item);
}

void DiagramEditor::fitToContents()
{
    fitDiagramInView();
}

void DiagramEditor::fitPageInView()
{
    if (!_view || !_scene) {
        return;
    }

    if (!isVisible() || _view->viewport()->width() < 32 || _view->viewport()->height() < 32) {
        _pendingPageFit = true;
        return;
    }

    const auto pageRect = orientedPageRect(pageLayout_);
    updateSceneRectFromPage();
    fitRectInView(_view, pageRect.adjusted(-4.0, -4.0, 4.0, 4.0));
    _pendingPageFit = false;
    updateRulers();
}

void DiagramEditor::fitDiagramInView()
{
    if (!_view || !_scene) {
        return;
    }
    QRectF content;
    for (const auto& node : _scene->elementNodes()) {
        content = content.united(node->sceneBoundingRect());
    }
    for (const auto& frame : _scene->textFrames()) {
        content = content.united(frame->sceneBoundingRect());
    }
    for (const auto& wire : _scene->signalWires()) {
        content = content.united(wire->sceneBoundingRect());
    }
    if (content.isEmpty()) {
        fitPageInView();
        return;
    }
    const double margin = 2.0 * kGridStep;
    if (fitRectInView(_view, content.adjusted(-margin, -margin, margin, margin))) {
        _pendingPageFit = false;
    }
    updateRulers();
}

void DiagramEditor::zoomIn()
{
    zoomBy(1.15);
}

void DiagramEditor::zoomOut()
{
    zoomBy(1.0 / 1.15);
}

void DiagramEditor::resetZoom()
{
    if (_view) {
        _pendingPageFit = false;
        _view->setZoomValue(1.0);
    }
}

void DiagramEditor::setPageLayout(const DiagramPageLayout& pageLayout)
{
    pageLayout_ = pageLayout;
    rebuildPageFrame();
    updateRulers();
}

auto DiagramEditor::pageLayout() const -> DiagramPageLayout
{
    return pageLayout_;
}

void DiagramEditor::scheduleInitialPageFit()
{
    _pendingPageFit = true;
    QTimer::singleShot(0, this, [this] {
        // Loading progress can process events before the main window is
        // shown. Keep the request pending until the real viewport is laid out.
        if (_pendingPageFit && isVisible()) {
            fitPageInView();
        }
    });
}

bool DiagramEditor::eventFilter(QObject* watched, QEvent* event)
{
    if (_view && watched == _view->viewport() && event && event->type() == QEvent::Resize) {
        updateRulerLayout();
        updateRulers();
        if (_pendingPageFit) scheduleInitialPageFit();
    }

    if (_view && watched == _view->viewport() && event && event->type() == QEvent::Wheel) {
        if (const auto* wheelEvent = dynamic_cast<QWheelEvent*>(event)) {
            if (wheelEvent->modifiers().testFlag(Qt::ControlModifier)) {
                if (wheelEvent->angleDelta().y() > 0) {
                    zoomIn();
                } else if (wheelEvent->angleDelta().y() < 0) {
                    zoomOut();
                }
                return true;
            }
        }
    }

    if (_view && watched == _view->viewport() && event && event->type() == QEvent::MouseButtonDblClick) {
        if (const auto* mouseEvent = dynamic_cast<QMouseEvent*>(event)) {
            const QPoint viewPos = mouseEvent->position().toPoint();
            const QPointF scenePos = _view->mapToScene(viewPos);
            QGraphicsItem* item = _scene && _scene->mode() == QSchematic::Scene::NormalMode ? _scene->itemAt(scenePos, QTransform()) : nullptr;

            while (item) {
                if (const auto* node = dynamic_cast<ElementNode*>(item)) {
                    const QString elementId = QString::fromStdString(node->id());
                    const QString type = QString::fromStdString(node->element().type);
                    const int channelCount = static_cast<int>(std::max<std::size_t>(1, node->inputs().size()));
                    emit elementNodeDoubleClicked(elementId, type, channelCount);
                    break;
                }
                if (const auto* frame = dynamic_cast<TextFrameNode*>(item)) {
                    emit elementNodeDoubleClicked(QString::fromStdString(frame->id()), QStringLiteral("TextFrame"), 1);
                    break;
                }
                item = item->parentItem();
            }
        }
    }

    return QWidget::eventFilter(watched, event);
}

void DiagramEditor::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);

    updateRulerLayout();
    updateRulers();

    if (_pendingPageFit) {
        scheduleInitialPageFit();
    }
}

void DiagramEditor::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);

    updateRulerLayout();
    updateRulers();

    if (_pendingPageFit && event->size().width() > 32 && event->size().height() > 32) {
        scheduleInitialPageFit();
    }
}

void DiagramEditor::rebuildPageFrame()
{
    if (!_scene) {
        return;
    }

    if (pageFrame_) {
        _scene->QGraphicsScene::removeItem(pageFrame_);
        delete pageFrame_;
        pageFrame_ = nullptr;
    }

    pageFrame_ = _scene->addRect(
        orientedPageRect(pageLayout_),
        QPen(QColor("#202020"), 1.0),
        QBrush(QColor("#ffffff")));
    pageFrame_->setZValue(-1000.0);
    pageFrame_->setFlag(QGraphicsItem::ItemIsMovable, false);
    pageFrame_->setFlag(QGraphicsItem::ItemIsSelectable, false);
    updateSceneRectFromPage();
    updateRulers();
}

auto DiagramEditor::suggestedNewItemPosition(const QSizeF itemSize) const -> QPointF
{
    constexpr double kGrid = kGridStep;
    constexpr double kMargin = kGrid;

    const QRectF page = orientedPageRect(pageLayout_);
    QRectF area = page;
    if (_view && _view->viewport()) {
        const QRectF clipped = _view->mapToScene(_view->viewport()->rect()).boundingRect().intersected(page);
        if (clipped.width() > 0.0 && clipped.height() > 0.0) {
            area = clipped;
        }
    }

    QList<QRectF> occupied;
    if (_scene) {
        for (const auto& node : _scene->items<QSchematic::Items::Node>()) {
            if (node) {
                occupied.append(node->sceneBoundingRect());
            }
        }
    }

    const auto snapUp = [](const double value) { return std::ceil(value / kGrid) * kGrid; };
    const double x0 = snapUp(area.left() + kMargin);
    const double y0 = snapUp(area.top() + kMargin);
    const double xMax = area.right() - itemSize.width();
    const double yMax = area.bottom() - itemSize.height();

    for (double y = y0; y <= yMax; y += kGrid) {
        for (double x = x0; x <= xMax; x += kGrid) {
            const QRectF candidate(QPointF(x, y), itemSize);
            const bool free = std::none_of(occupied.begin(), occupied.end(),
                                           [&](const QRectF& r) { return r.intersects(candidate); });
            if (free) {
                return QPointF(x, y);
            }
        }
    }

    // No free spot: stay inside the visible area (as far as the item fits) rather than jumping elsewhere.
    return QPointF(std::max(area.left(), std::min(x0, xMax)), std::max(area.top(), std::min(y0, yMax)));
}

void DiagramEditor::updateSceneRectFromPage()
{
    if (!_scene) {
        return;
    }

    const auto pageRect = orientedPageRect(pageLayout_);
    const double pageMargin = sceneUnitsFromMm(kPageMarginMm);
    _scene->setSceneRect(pageRect.united(_scene->itemsBoundingRect()).adjusted(-pageMargin, -pageMargin, pageMargin, pageMargin));
}

void DiagramEditor::updateRulerLayout()
{
    if (!_view || !horizontalRuler_ || !verticalRuler_ || !rulerCorner_) {
        return;
    }

    const int frame = _view->frameWidth();
    const QRect viewportRect = _view->viewport()->geometry();
    const int leftWidth = std::max(0, viewportRect.left() - frame);
    const int topHeight = std::max(0, viewportRect.top() - frame);

    horizontalRuler_->setGeometry(viewportRect.left(), frame, viewportRect.width(), topHeight);
    verticalRuler_->setGeometry(frame, viewportRect.top(), leftWidth, viewportRect.height());
    rulerCorner_->setGeometry(frame, frame, leftWidth, topHeight);
}

void DiagramEditor::updateRulers()
{
    if (horizontalRuler_) {
        horizontalRuler_->update();
    }
    if (verticalRuler_) {
        verticalRuler_->update();
    }
    if (rulerCorner_) {
        rulerCorner_->update();
    }
}

void DiagramEditor::zoomBy(const double factor)
{
    if (!_view) {
        return;
    }

    _pendingPageFit = false;
    const double currentZoom = _view->transform().m11();
    const double nextZoom = std::clamp(currentZoom * factor,
                                       static_cast<double>(_view->zoom_factor_min),
                                       static_cast<double>(_view->zoom_factor_max));
    _view->setZoomValue(nextZoom);
}

DiagramScene* DiagramEditor::scene() {
    return _scene.get();
}

const DiagramScene* DiagramEditor::scene() const {
    return _scene.get();
}

} // namespace s7studio::diagram
