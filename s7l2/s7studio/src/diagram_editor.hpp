#pragma once

#include "diagram_document.hpp"

#include <QWidget>
#include <boost/json.hpp>
#include <functional>
#include <memory>

class QGraphicsItem;
class QGraphicsRectItem;
class QWidget;

namespace QSchematic {
class View;
}

namespace s7studio::diagram {

class DiagramRuler;
class DiagramScene;

// Minimal diagram editor widget
// Embeds a QSchematic scene for visual editing of the schema diagram
class DiagramEditor : public QWidget {
    Q_OBJECT

public:
    explicit DiagramEditor(QWidget* parent = nullptr);
    ~DiagramEditor() override;

    void clearDiagram();
    // Both throw std::runtime_error when the document cannot be laid out;
    // the editor is then empty.
    void loadDocument(const DiagramDocument& document, const std::function<void(int)>& progress = {});
    void loadRuntimeSchema(const boost::json::value& schemaJson);
    // Selects the item and scrolls it into the middle of the view.
    void showItem(QGraphicsItem* item);
    void fitToContents();
    void fitPageInView();
    // Zooms so that the diagram's elements, text frames and wires fill the view (the page if empty).
    void fitDiagramInView();
    void zoomIn();
    void zoomOut();
    void resetZoom();
    void setPageLayout(const DiagramPageLayout& pageLayout);
    [[nodiscard]] auto pageLayout() const -> DiagramPageLayout;

    [[nodiscard]] DiagramScene* scene();
    [[nodiscard]] const DiagramScene* scene() const;

signals:
    void elementNodeDoubleClicked(const QString& elementId, const QString& elementType, int channelCount);

private:
    void scheduleFitToContents();
    void rebuildPageFrame();
    void updateSceneRectFromPage();
    void updateRulerLayout();
    void updateRulers();
    void zoomBy(double factor);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

    void showEvent(QShowEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    std::unique_ptr<DiagramScene> _scene;
    QSchematic::View* _view = nullptr;
    bool _pendingFitToContents = false;
    DiagramPageLayout pageLayout_;
    QGraphicsRectItem* pageFrame_ = nullptr;
    DiagramRuler* horizontalRuler_ = nullptr;
    DiagramRuler* verticalRuler_ = nullptr;
    QWidget* rulerCorner_ = nullptr;
};

} // namespace s7studio::diagram
