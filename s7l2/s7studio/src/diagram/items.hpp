// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-S7-plugin-permission

#pragma once

// Studio's diagram items: QSchematic subclasses carrying the schema identity
// as typed members (STUDIO_DIAGRAM_WIRING_DESIGN.md, section 3).

#include <qschematic/items/connector.hpp>
#include <qschematic/items/node.hpp>
#include <qschematic/items/wire.hpp>

#include <boost/json.hpp>

#include <QPointF>
#include <QString>
#include <QVector>

#include <memory>
#include <string>
#include <vector>

class QGraphicsSimpleTextItem;

namespace s7studio::diagram {

enum ItemType {
    kElementNodeType = QSchematic::Items::Item::QSchematicItemUserType + 1,
    kTextFrameNodeType,
    kPortConnectorType,
    kSignalWireType,
};

// Scene units per grid step. Diagram files store whole grid steps; nodes,
// ports, wire points and bends always lie on the grid.
constexpr int kGridStep = 20;
constexpr int kElementWidthSteps = 9;

enum class PortDirection { Input, Output };

class ElementNode;

class PortConnector final : public QSchematic::Items::Connector
{
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(PortConnector)

public:
    // How the port is shown while a connection is being drawn.
    enum class Mark { None, Target, Refused };

    PortConnector(PortDirection direction, int index, QString name, QGraphicsItem* parent = nullptr);

    [[nodiscard]] PortDirection direction() const { return direction_; }
    [[nodiscard]] int index() const { return index_; }
    [[nodiscard]] const QString& name() const { return name_; }
    [[nodiscard]] ElementNode* element() const;

    void setMark(Mark mark);
    [[nodiscard]] Mark mark() const { return mark_; }

    std::shared_ptr<QSchematic::Items::Item> deepCopy() const override;
    QRectF boundingRect() const override;
    QPainterPath shape() const override;
    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget = nullptr) override;

private:
    PortDirection direction_;
    int index_;
    QString name_;
    Mark mark_ = Mark::None;
};

// A schema element: its JSON identity (id, type, params, nested schema,
// placement) and one PortConnector per port, inputs on the left edge and
// outputs on the right.
class ElementNode final : public QSchematic::Items::Node
{
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(ElementNode)

public:
    struct Element {
        std::string id;
        std::string type;
        boost::json::object params;
        boost::json::value schema; // nested schema of schema-backed elements, else null
        std::string placement;     // node_uuid of the runtime node, empty: not placed
        QString title;             // shown name, empty: the id

        bool operator==(const Element&) const = default;
    };

    explicit ElementNode(Element element);

    [[nodiscard]] const Element& element() const { return element_; }
    [[nodiscard]] const std::string& id() const { return element_.id; }
    [[nodiscard]] QString title() const;
    // Replaces the element; ports are added or removed to match its port
    // counts (wires on removed ports must be removed first).
    void setElement(Element element);

    [[nodiscard]] const std::vector<std::shared_ptr<PortConnector>>& inputs() const { return inputs_; }
    [[nodiscard]] const std::vector<std::shared_ptr<PortConnector>>& outputs() const { return outputs_; }
    [[nodiscard]] std::shared_ptr<PortConnector> port(PortDirection direction, int index) const;
    // At least this many ports, whatever the element declares (elements of
    // types unknown to the registry show the ports their connections use).
    void setMinimumPorts(int inputs, int outputs);
    [[nodiscard]] int minimumInputs() const { return minimumInputs_; }
    [[nodiscard]] int minimumOutputs() const { return minimumOutputs_; }

    // "@ <node>" under the element, empty: hidden.
    void setPlacementLabel(const QString& text);
    // Why the element cannot be built (e.g. its plugin is missing): drawn red, tooltip. Empty: none.
    void setProblem(const QString& problem);
    [[nodiscard]] const QString& problem() const { return problem_; }

    std::shared_ptr<QSchematic::Items::Item> deepCopy() const override;
    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget = nullptr) override;

private:
    void rebuildPorts();

    Element element_;
    int minimumInputs_ = 0;
    int minimumOutputs_ = 0;
    std::vector<std::shared_ptr<PortConnector>> inputs_;
    std::vector<std::shared_ptr<PortConnector>> outputs_;
    QGraphicsSimpleTextItem* placementLabel_ = nullptr;
    QString problem_;
};

// A diagram-only text frame (not part of the runtime schema). Its params hold
// the text, colors, font and size in scene units.
class TextFrameNode final : public QSchematic::Items::Node
{
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(TextFrameNode)

public:
    TextFrameNode(std::string id, const boost::json::object& params);

    [[nodiscard]] const std::string& id() const { return id_; }
    // Params with every default filled in.
    [[nodiscard]] const boost::json::object& params() const { return params_; }
    void setParams(const boost::json::object& params);

    [[nodiscard]] static auto defaultParams() -> boost::json::object;

    std::shared_ptr<QSchematic::Items::Item> deepCopy() const override;
    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget = nullptr) override;

private:
    std::string id_;
    boost::json::object params_;
};

class SignalWire;

// One end of a wire: a port of an element, or a junction at a point of
// another wire of the same net.
struct WireEnd {
    enum class Kind { None, Port, Junction };

    Kind kind = Kind::None;
    std::weak_ptr<ElementNode> node;
    PortDirection direction = PortDirection::Output;
    int index = 0;
    std::weak_ptr<SignalWire> wire;
    QPointF at;

    [[nodiscard]] static auto onPort(const std::shared_ptr<ElementNode>& node, PortDirection direction, int index) -> WireEnd;
    [[nodiscard]] static auto onWire(const std::shared_ptr<SignalWire>& wire, QPointF at) -> WireEnd;

    [[nodiscard]] bool isPort() const { return kind == Kind::Port; }
    [[nodiscard]] bool isJunction() const { return kind == Kind::Junction; }
    [[nodiscard]] bool isOutput() const { return isPort() && direction == PortDirection::Output; }
    [[nodiscard]] bool isInput() const { return isPort() && direction == PortDirection::Input; }
    // The port, if this end is on a port of an element still in the scene.
    [[nodiscard]] auto port() const -> std::shared_ptr<PortConnector>;
    [[nodiscard]] bool sameAs(const WireEnd& other) const;
};

// A wire of a net. Every net is a tree: its first wire runs from the driving
// output to an input; every further wire starts at a junction on a wire of the
// net (upstream) and ends at an input (downstream). The route is derived from
// the ends and the user bends (WireRouter); the current route is saved as a
// cache so opening a diagram does not need to route every wire again.
class SignalWire final : public QSchematic::Items::Wire
{
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(SignalWire)

public:
    SignalWire();

    WireEnd upstream;
    WireEnd downstream;
    // Bends placed by hand, in order from upstream to downstream (scene units,
    // on the grid). The router routes the legs between them.
    QVector<QPointF> userBends;

    // Replaces the route (scene units). Connector attachments are restored by
    // the scene.
    void setRoute(const QVector<QPointF>& points);

    // Type shown at the start of the wire; `ok` false shows it as an error.
    void setTypeLabel(const QString& text, bool ok);
    [[nodiscard]] const QString& typeLabel() const { return typeLabel_; }
    // A problem of the wire's net (drawn red, tooltip).
    void setProblem(const QString& problem);
    [[nodiscard]] const QString& problem() const { return problem_; }
    // Name of the probe tapping this net's driving output, shown as a marker
    // partway along the wire; empty when the net has none (DiagramProbe).
    void setProbeLabel(const QString& name);
    [[nodiscard]] const QString& probeLabel() const { return probeLabel_; }

    std::shared_ptr<QSchematic::Items::Item> deepCopy() const override;
    QRectF boundingRect() const override;
    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override;

protected:
    // The scene handles bend editing; the wire only takes part in selection.
    void mousePressEvent(QGraphicsSceneMouseEvent* event) override;
    void mouseMoveEvent(QGraphicsSceneMouseEvent* event) override;
    void mouseReleaseEvent(QGraphicsSceneMouseEvent* event) override;
    void mouseDoubleClickEvent(QGraphicsSceneMouseEvent* event) override;
    void contextMenuEvent(QGraphicsSceneContextMenuEvent* event) override;

private:
    QString typeLabel_;
    bool typeOk_ = true;
    QString problem_;
    QString probeLabel_;
};

} // namespace s7studio::diagram
