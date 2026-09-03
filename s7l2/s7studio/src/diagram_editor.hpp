#pragma once

#include "diagram_document.hpp"

#include <QWidget>
#include <boost/json.hpp>
#include <memory>

class QGraphicsRectItem;
class QWidget;

namespace QSchematic {
class View;
class Scene;
}

namespace s7studio::diagram {

class DiagramRuler;

// Minimal diagram editor widget
// Embeds a QSchematic scene for visual editing of the schema diagram
class DiagramEditor : public QWidget {
    Q_OBJECT

public:
    explicit DiagramEditor(QWidget* parent = nullptr);
    ~DiagramEditor() override;

    void clearDiagram();
    void loadDocument(const DiagramDocument& document);
    void loadRuntimeSchema(const boost::json::value& schemaJson);
    void fitToContents();
    void fitPageInView();
    void zoomIn();
    void zoomOut();
    void resetZoom();
    void setPageLayout(const DiagramPageLayout& pageLayout);
    [[nodiscard]] auto pageLayout() const -> DiagramPageLayout;

    [[nodiscard]] QSchematic::Scene* scene();
    [[nodiscard]] const QSchematic::Scene* scene() const;

signals:
    void scopeNodeDoubleClicked(const QString& scopeId, int channelCount);
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
    std::unique_ptr<QSchematic::Scene> _scene;
    QSchematic::View* _view = nullptr;
    bool _pendingFitToContents = false;
    DiagramPageLayout pageLayout_;
    QGraphicsRectItem* pageFrame_ = nullptr;
    DiagramRuler* horizontalRuler_ = nullptr;
    DiagramRuler* verticalRuler_ = nullptr;
    QWidget* rulerCorner_ = nullptr;
};

} // namespace s7studio::diagram
