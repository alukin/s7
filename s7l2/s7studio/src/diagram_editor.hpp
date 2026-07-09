#pragma once

#include <QWidget>
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

    QSchematic::Scene* scene();
    const QSchematic::Scene* scene() const;

private:
    std::unique_ptr<QSchematic::Scene> _scene;
    QSchematic::View* _view = nullptr;
};

} // namespace s7studio::diagram
