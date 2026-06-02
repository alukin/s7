#pragma once

#include <qschematic/settings.hpp>

#include <QTreeWidget>
#include <functional>

namespace s7ui::ui {

class NodeFactory;

class PaletteTreeWidget : public QTreeWidget
{
    Q_OBJECT

public:
    explicit PaletteTreeWidget(QWidget* parent = nullptr);

    void setNodeFactory(const NodeFactory* nodeFactory);

    /// Provide a callable that returns the current view pixels-per-scene-unit
    /// (i.e. QGraphicsView::transform().m11()).  Used to scale the drag preview
    /// pixmap so it matches the apparent node size on the canvas.
    void setViewScaleProvider(std::function<qreal()> provider);

    /// Store the scene settings so that prototype items get the correct gridSize
    /// applied (which affects connector symbol rect size) before rendering the
    /// drag-preview pixmap.
    void setSceneSettings(const QSchematic::Settings& settings);

protected:
    void startDrag(Qt::DropActions supportedActions) override;

private:
    const NodeFactory*      nodeFactory_       = nullptr;
    std::function<qreal()>  viewScaleProvider_;
    QSchematic::Settings    sceneSettings_;
};

} // namespace s7ui::ui
