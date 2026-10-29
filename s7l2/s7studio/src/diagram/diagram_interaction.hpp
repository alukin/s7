// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-S7-plugin-permission

#pragma once

#include "diagram/items.hpp"

#include <QPoint>
#include <QPointF>

#include <functional>
#include <memory>
#include <optional>

class QGraphicsPathItem;
class QGraphicsSceneMouseEvent;

namespace s7studio::diagram {

class DiagramScene;

// Owns transient drawing, bend and move gestures. DiagramScene supplies the
// document edits, routing attachments and base Qt event handling.
class DiagramInteraction final
{
public:
    explicit DiagramInteraction(DiagramScene& scene);
    ~DiagramInteraction();

    void clearForSceneReset();
    [[nodiscard]] bool mouseDown() const { return mouseDown_; }
    void modeChanged(int mode);

    void mousePressEvent(QGraphicsSceneMouseEvent* event, const std::function<void()>& base);
    void mouseMoveEvent(QGraphicsSceneMouseEvent* event, const std::function<void()>& base);
    void mouseReleaseEvent(QGraphicsSceneMouseEvent* event, const std::function<void()>& base);
    void mouseDoubleClickEvent(QGraphicsSceneMouseEvent* event, const std::function<void()>& base);
    bool handleKey(int key, Qt::KeyboardModifiers modifiers);
    void showWireMenu(SignalWire& wire, QPointF scenePos, QPoint screenPos);

private:
    struct Gesture;
    struct MoveGesture;

    [[nodiscard]] auto portAt(QPointF scenePos) const -> std::shared_ptr<PortConnector>;
    [[nodiscard]] auto wireAt(QPointF scenePos) const -> std::shared_ptr<SignalWire>;
    [[nodiscard]] auto endAt(QPointF scenePos, bool wires) const -> std::optional<WireEnd>;
    [[nodiscard]] bool insideElement(QPointF scenePos) const;
    void markTargets(const WireEnd* start);
    void showPreview(const QVector<QPointF>& points, bool valid);
    void hidePreview();
    [[nodiscard]] auto previewRoute(const WireEnd& start, const QVector<QPointF>& bends, QPointF end,
                                    const WireEnd* target) const -> QVector<QPointF>;
    void startDrawing(const WireEnd& start);
    void cancelDrawing();
    void updateDrawingPreview(QPointF scenePos);

    DiagramScene& scene_;
    std::unique_ptr<Gesture> gesture_;
    std::unique_ptr<MoveGesture> moveGesture_;
    QGraphicsPathItem* preview_ = nullptr; // owned by the scene
    bool mouseDown_ = false;
};

} // namespace s7studio::diagram
