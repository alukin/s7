#pragma once

#include "diagram/items.hpp"
#include "diagram/net_model.hpp"

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

class QGraphicsPathItem;

namespace s7studio::diagram {

// A set of edits applied (and reverted) as one undo step.
struct DiagramChange {
    struct Rehang {
        std::shared_ptr<SignalWire> wire;
        WireEnd before;
        WireEnd after;
    };
    struct Bends {
        std::shared_ptr<SignalWire> wire;
        QVector<QPointF> before;
        QVector<QPointF> after;
    };
    struct ElementEdit {
        std::shared_ptr<ElementNode> node;
        ElementNode::Element before;
        ElementNode::Element after;
    };

    std::vector<std::shared_ptr<QSchematic::Items::Node>> addedNodes;
    std::vector<std::shared_ptr<QSchematic::Items::Node>> removedNodes;
    std::vector<std::shared_ptr<SignalWire>> addedWires;
    std::vector<std::shared_ptr<SignalWire>> removedWires;
    std::vector<Rehang> rehung;
    std::vector<Bends> bends;
    std::vector<ElementEdit> elements;

    [[nodiscard]] bool empty() const;
};

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

    // Probes (non-invasive output taps, not Functional Graph elements; see
    // DiagramProbe). Not part of the undo stack: adding/removing/renaming a
    // probe never changes what the diagram computes.
    [[nodiscard]] const std::vector<DiagramProbe>& probes() const { return probes_; }
    // False if `point` already has a probe.
    bool addProbe(PortRef point, QString name, QString unit = {});
    void removeProbe(const PortRef& point);
    // False if `point` has no probe.
    bool renameProbe(const PortRef& point, QString name);
    // Bulk-load (opening a document); replaces every existing probe.
    void setProbes(std::vector<DiagramProbe> probes);

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
    void deleteSelection();
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

protected:
    void mousePressEvent(QGraphicsSceneMouseEvent* event) override;
    void mouseMoveEvent(QGraphicsSceneMouseEvent* event) override;
    void mouseReleaseEvent(QGraphicsSceneMouseEvent* event) override;
    void mouseDoubleClickEvent(QGraphicsSceneMouseEvent* event) override;

private:
    struct Gesture;
    struct MoveGesture;

    // Routing.
    void route(std::vector<SignalWire*> forced, const std::function<void()>& routeProgress = {});
    void repairRoutes(std::vector<SignalWire*> forced, const std::function<void()>& routeProgress = {});
    [[nodiscard]] bool routeIsValid(const SignalWire& wire, bool checkObstacles = true) const;
    [[nodiscard]] auto anchorOf(const WireEnd& end) const -> std::optional<QPointF>;
    [[nodiscard]] auto placeJunction(const SignalWire& branch) const -> std::optional<QPointF>;
    [[nodiscard]] auto previewRoute(const WireEnd& start, const QVector<QPointF>& bends, QPointF end, const WireEnd* target) const
        -> QVector<QPointF>;
    [[nodiscard]] auto nodeRects() const -> std::vector<QRectF>;
    void attach(SignalWire& wire);
    void checkMovedNodes();
    void rememberPositions();

    // Topology helpers.
    [[nodiscard]] auto trunkOf(const WireEnd& output) const -> std::shared_ptr<SignalWire>;
    [[nodiscard]] auto branchesOf(const SignalWire& wire) const -> std::vector<std::shared_ptr<SignalWire>>;
    [[nodiscard]] auto wiresOf(const ElementNode& node) const -> std::vector<std::shared_ptr<SignalWire>>;
    [[nodiscard]] auto driverEnd(const WireEnd& end) const -> std::optional<WireEnd>;
    [[nodiscard]] auto typeProblem(const WireEnd& driver, const WireEnd& input) const -> QString;
    void collectRemoval(DiagramChange& change, std::vector<std::shared_ptr<SignalWire>> wires) const;

    // Gestures.
    [[nodiscard]] auto portAt(QPointF scenePos) const -> std::shared_ptr<PortConnector>;
    [[nodiscard]] auto wireAt(QPointF scenePos) const -> std::shared_ptr<SignalWire>;
    [[nodiscard]] auto endAt(QPointF scenePos, bool wires) const -> std::optional<WireEnd>;
    [[nodiscard]] bool insideElement(QPointF scenePos) const;
    void markTargets(const WireEnd* start);
    void showPreview(const QVector<QPointF>& points, bool valid);
    void hidePreview();
    void startDrawing(const WireEnd& start);
    void cancelDrawing();
    void updateDrawingPreview(QPointF scenePos);

    std::unique_ptr<Gesture> gesture_;
    std::unique_ptr<MoveGesture> moveGesture_;
    QGraphicsPathItem* preview_ = nullptr;
    std::unordered_map<const QSchematic::Items::Item*, QPointF> knownPositions_;
    std::vector<DiagramProbe> probes_;
    bool applying_ = false;
    bool mouseDown_ = false;
    // Whether the diagram's types resolved before the edit being checked
    // (refusals only apply to connections that break a resolving diagram).
    mutable std::optional<bool> typesResolve_;
};

} // namespace s7studio::diagram
