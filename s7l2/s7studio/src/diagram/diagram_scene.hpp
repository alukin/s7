// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-S7-plugin-permission

#pragma once

#include "diagram/diagram_change.hpp"

#include <qschematic/scene.hpp>

#include <QPointF>
#include <QString>
#include <QVector>

#include <memory>
#include <functional>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace s7studio::diagram {

class DiagramInteraction;

// The Studio diagram scene (STUDIO_DIAGRAM_WIRING_DESIGN.md). Wires form one
// tree per net, rooted at the driving output; connections are drawn by
// dragging from a port (or Alt+drag from a wire) or click by click in
// DrawMode, and refused when they would join two outputs, feed an input twice
// or connect incompatible types. Routes are derived from the wire ends and
// the bends placed by hand.
class DiagramScene final : public QSchematic::Scene
{
    Q_OBJECT

public:
    static constexpr int DrawMode = QSchematic::Scene::UserMode + 1;

    explicit DiagramScene(QObject* parent = nullptr);
    ~DiagramScene() override;

    // Content, in insertion order.
    [[nodiscard]] auto elementNodes() const -> std::vector<std::shared_ptr<ElementNode>>;
    [[nodiscard]] auto elementNode(const std::string& id) const -> std::shared_ptr<ElementNode>;
    [[nodiscard]] auto textFrames() const -> std::vector<std::shared_ptr<TextFrameNode>>;
    [[nodiscard]] auto signalWires() const -> std::vector<std::shared_ptr<SignalWire>>;
    [[nodiscard]] auto sharedWire(const SignalWire* wire) const -> std::shared_ptr<SignalWire>;

    // Probes are non-invasive output taps. Annotation edits share the document
    // undo history; they never change what the diagram computes.
    [[nodiscard]] const std::vector<DiagramProbe>& probes() const { return probes_; }
    // False if `point` already has a probe.
    bool addProbe(PortRef point, QString name, QString unit = {});
    void removeProbe(const PortRef& point);
    // False if `point` has no probe.
    bool renameProbe(const PortRef& point, QString name);
    // Bulk-load (opening a document); replaces every existing probe.
    void setProbes(std::vector<DiagramProbe> probes);
    const boost::json::array& observersPanel() const { return observersPanel_; }
    void setObserversPanel(boost::json::array selection, bool undoable = false, bool manual = true);
    bool observersPanelManual() const { return observersPanelManual_; }

    // Building without undo (loading). rerouteAll() routes every wire.
    void clearDiagram();
    void addNodeItem(const std::shared_ptr<QSchematic::Items::Node>& node);
    void addWireItem(const std::shared_ptr<SignalWire>& wire);
    void rerouteAll();
    // Restore saved wire paths, rerouting only paths that are absent or stale.
    void restoreRoutes(const std::function<void(int)>& progress = {});

    // Undoable edits.
    void applyChange(const DiagramChange& change, const QString& text);
    auto addElement(ElementNode::Element element, QPointF position) -> std::shared_ptr<ElementNode>;
    auto addTextFrame(const std::string& id, const boost::json::object& params, QPointF position) -> std::shared_ptr<TextFrameNode>;
    // Replaces an element's identity and params; wires on ports it no longer has are removed.
    void changeElement(const std::shared_ptr<ElementNode>& node, ElementNode::Element element);
    void changeTextFrame(const std::shared_ptr<TextFrameNode>& node, boost::json::object params);
    void deleteSelection();
    bool copySelection() const;
    bool pasteSelection(QPointF position);
    void selectAll();
    void deleteWire(const std::shared_ptr<SignalWire>& wire);
    void setUserBends(const std::shared_ptr<SignalWire>& wire, QVector<QPointF> bends);

    // Why the two ends (in any order) cannot be connected; empty if they can.
    [[nodiscard]] auto checkConnection(const WireEnd& a, const WireEnd& b) const -> QString;
    // Connects the two ends (in any order) through the bends; returns the
    // reason when refused.
    auto connectEnds(const WireEnd& a, const WireEnd& b, QVector<QPointF> bends = {}) -> QString;

    void showWireMenu(SignalWire& wire, QPointF scenePos, QPoint screenPos);

    // Keys of the view: Escape cancels drawing, Delete and Backspace remove
    // the last bend while drawing and the selection otherwise.
    bool handleKey(int key, Qt::KeyboardModifiers modifiers);

    // Applies a change without pushing it (undo commands).
    void apply(const DiagramChange& change, bool revert);

Q_SIGNALS:
    // Elements, wires, params or placement changed (not plain moves).
    void diagramChanged();
    void statusMessage(const QString& message);
    // A probe was added, removed or renamed.
    void probesChanged();
    void observersPanelChanged();

protected:
    void mousePressEvent(QGraphicsSceneMouseEvent* event) override;
    void mouseMoveEvent(QGraphicsSceneMouseEvent* event) override;
    void mouseReleaseEvent(QGraphicsSceneMouseEvent* event) override;
    void mouseDoubleClickEvent(QGraphicsSceneMouseEvent* event) override;

private:
    friend class DiagramInteraction;

    // Routing.
    void repairRoutes(std::vector<SignalWire*> forced, const std::function<void()>& routeProgress = {});
    [[nodiscard]] auto anchorOf(const WireEnd& end) const -> std::optional<QPointF>;
    [[nodiscard]] auto nodeRects() const -> std::vector<QRectF>;
    void attach(SignalWire& wire);
    void checkMovedNodes();
    void rememberPositions();

    // Topology helpers.
    [[nodiscard]] auto branchesOf(const SignalWire& wire) const -> std::vector<std::shared_ptr<SignalWire>>;
    [[nodiscard]] auto wiresOf(const ElementNode& node) const -> std::vector<std::shared_ptr<SignalWire>>;

    std::unique_ptr<DiagramInteraction> interaction_;
    std::unordered_map<const QSchematic::Items::Item*, QPointF> knownPositions_;
    std::vector<DiagramProbe> probes_;
    boost::json::array observersPanel_;
    bool observersPanelManual_ = false;
    bool applying_ = false;
    // Whether the diagram's types resolved before the edit being checked
    // (refusals only apply to connections that break a resolving diagram).
    mutable std::optional<bool> typesResolve_;
};

} // namespace s7studio::diagram
