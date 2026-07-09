#pragma once

#include "diagram_document.hpp"

#include <QWidget>
#include <boost/json.hpp>
#include <memory>

namespace QSchematic {
class View;
class Scene;
}

namespace s7studio::diagram {

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

    QSchematic::Scene* scene();
    const QSchematic::Scene* scene() const;

private:
    void scheduleFitToContents();

protected:
    void showEvent(QShowEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    std::unique_ptr<QSchematic::Scene> _scene;
    QSchematic::View* _view = nullptr;
    bool _pendingFitToContents = false;
};

} // namespace s7studio::diagram
