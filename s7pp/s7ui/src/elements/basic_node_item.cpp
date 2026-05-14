#include "basic_node_item.hpp"
#include "s7_wire_item.hpp"
#include "s7ui_json_dto.hpp"

#include <s7core_json_dto.hpp>
#include <s7factory.hpp>

#include <boost/json.hpp>

#include <qschematic/items/connector.hpp>
#include <qschematic/items/label.hpp>
#include <qschematic/scene.hpp>

#include "dialogs/properties_dialog.hpp"

#include <QDialog>
#include <QFontMetricsF>
#include <QGraphicsScene>
#include <QGraphicsSceneMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QStyleOptionGraphicsItem>

#include <algorithm>
#include <cmath>
#include <unordered_set>

namespace s7ui::elements {

namespace
{
constexpr qreal kNodeMinWidthMm  = 60.0;
constexpr qreal kNodeMinHeightMm = 30.0;
constexpr qreal kPortPitchMm     = 10.0;
constexpr qreal kPortMarginMm    = 10.0;
constexpr qreal kNodeTitleTopPaddingMm = 2.0;

struct PortAxisLayout
{
    int count;
    qreal nodeHeightMm;
};

auto connectorOffsetMm(const int index, const PortAxisLayout& layout) -> qreal
{
    if (layout.count <= 1) {
        return layout.nodeHeightMm * 0.5;
    }

    // Slot model: endpoints are forbidden; connectors occupy interior slots only.
    // For N pins, use slot indices [1..N] out of [0..N+1].
    const qreal step = layout.nodeHeightMm / static_cast<qreal>(layout.count + 1);
    return step * static_cast<qreal>(index + 1);
}

/// Find the label for a port at the given index within one direction ("input" / "output").
auto portLabel(const std::vector<s7::PortDescriptor>& descs,
               const std::string& direction,
               int index) -> QString
{
    int counted = 0;
    for (const auto& d : descs) {
        if (d.direction == direction) {
            if (counted == index) {
                return QString::fromStdString(d.label);
            }
            ++counted;
        }
    }
    return {};
}

void configureConnectorLabel(const std::shared_ptr<QSchematic::Items::Connector>& connector,
                             const QString& label)
{
    auto lbl = connector->label();
    if (!lbl) {
        return;
    }
    lbl->setMovable(false);
    lbl->setFlag(QGraphicsItem::ItemIsSelectable, false);
    lbl->setAcceptedMouseButtons(Qt::NoButton);
    lbl->setHighlightEnabled(false);
    lbl->setSnapToGrid(false);
    // Keep connector->text() as stable port id (in0/out0/...) and avoid replacing it
    // with optional human label; this preserves connector lookup and wiring semantics.
    lbl->setVisible(false);
    if (!label.isEmpty()) {
        connector->setToolTip(label);
    }
}

auto findConnectorByName(const QList<std::shared_ptr<QSchematic::Items::Connector>>& connectors,
                         const QString& name)
    -> std::shared_ptr<QSchematic::Items::Connector>
{
    for (const auto& connector : connectors) {
        if (connector && connector->text() == name) {
            return connector;
        }
    }
    return nullptr;
}

} // namespace

BasicNodeItem::BasicNodeItem(const QString& typeName, QGraphicsItem* parent)
    : QSchematic::Items::Node(QSchematic::Items::Item::NodeType, parent)
    , typeName_(typeName)
    , nodeName_(typeName)
{
    setMovable(true);
    setFlag(QGraphicsItem::ItemIsMovable,   true);
    setFlag(QGraphicsItem::ItemIsSelectable, true);
    setAllowMouseResize(false);
    setAllowMouseRotate(false);
    setConnectorsMovable(false);
    setConnectorsSnapPolicy(QSchematic::Items::Connector::NodeSizerectOutline);
    setConnectorsSnapToGrid(true);
    connect(this, &QSchematic::Items::Item::settingsChanged, this, [this]() {
        relayoutConnectors();
    });

    // Initialise params from registry defaults.
    paramsJson_ = defaultParamsJsonFor(typeName.toStdString());
    setPortLayout(1, 1);
    refreshToolTip();
}

void BasicNodeItem::setNodeId(const QString& nodeId)
{
    nodeId_ = nodeId;
    refreshToolTip();
    update();
}

QString BasicNodeItem::nodeId() const { return nodeId_; }

void BasicNodeItem::setNodeName(const QString& nodeName)
{
    nodeName_ = nodeName.trimmed().isEmpty() ? typeName_ : nodeName.trimmed();
    refreshToolTip();
    update();
}

QString BasicNodeItem::nodeName() const { return nodeName_; }

void BasicNodeItem::setTypeName(const QString& typeName)
{
    typeName_ = typeName;
    if (nodeName_.trimmed().isEmpty()) nodeName_ = typeName_;
    refreshToolTip();
    update();
}

QString BasicNodeItem::typeName() const { return typeName_; }

void BasicNodeItem::setParamsJson(const QString& paramsJson)
{
    paramsJson_ = paramsJson.trimmed().isEmpty() ? QStringLiteral("{}") : paramsJson;
}

QString BasicNodeItem::paramsJson() const { return paramsJson_; }

// ---------------------------------------------------------------------------
// setPortLayout
// ---------------------------------------------------------------------------
void BasicNodeItem::setPortLayout(int inputPorts, int outputPorts,
                                  const std::vector<s7::PortDescriptor>& portDescs)
{
    inputPorts_  = std::clamp(inputPorts,  0, 32);
    outputPorts_ = std::clamp(outputPorts, 0, 32);

    int maxPorts = 1;
    maxPorts = std::max(maxPorts, inputPorts_);
    maxPorts = std::max(maxPorts, outputPorts_);
    const qreal requestedHeight = std::max(
        {kNodeMinHeightMm,
         2.0 * kPortMarginMm + static_cast<qreal>(maxPorts - 1) * kPortPitchMm});
    setSize(kNodeMinWidthMm, requestedHeight);

    const qreal nodeHeight = size().height();

    clearConnectors();

    const PortAxisLayout inputLayout{inputPorts_, nodeHeight};
    const PortAxisLayout outputLayout{outputPorts_, nodeHeight};

    for (int i = 0; i < inputPorts_; ++i) {
        const QString label = portLabel(portDescs, "input", i);
        const QString name  = QString("in%1").arg(i);
        auto in = std::make_shared<QSchematic::Items::Connector>(
            QSchematic::Items::Item::ConnectorType, QPoint(), name, this);
        in->setHighlightEnabled(false);
        in->setSnapToGrid(true);
        in->setPos(0.0, connectorOffsetMm(i, inputLayout));
        configureConnectorLabel(in, label);
        addConnector(in);
    }

    for (int i = 0; i < outputPorts_; ++i) {
        const QString label = portLabel(portDescs, "output", i);
        const QString name  = QString("out%1").arg(i);
        auto out = std::make_shared<QSchematic::Items::Connector>(
            QSchematic::Items::Item::ConnectorType, QPoint(), name, this);
        out->setHighlightEnabled(false);
        out->setSnapToGrid(true);
        out->setPos(size().width(), connectorOffsetMm(i, outputLayout));
        configureConnectorLabel(out, label);
        addConnector(out);
    }

    relayoutConnectors();

    update();
}

void BasicNodeItem::relayoutConnectors()
{
    const qreal nodeWidth = size().width();
    const qreal nodeHeight = size().height();
    const PortAxisLayout inputLayout{inputPorts_, nodeHeight};
    const PortAxisLayout outputLayout{outputPorts_, nodeHeight};
    const auto all = connectors();

    for (int i = 0; i < inputPorts_; ++i) {
        const auto connector = findConnectorByName(all, QString("in%1").arg(i));
        if (!connector) continue;
        connector->setPos(0.0, connectorOffsetMm(i, inputLayout));
    }

    for (int i = 0; i < outputPorts_; ++i) {
        const auto connector = findConnectorByName(all, QString("out%1").arg(i));
        if (!connector) continue;
        connector->setPos(nodeWidth, connectorOffsetMm(i, outputLayout));
    }
}

// ---------------------------------------------------------------------------
// applyParamsAndUpdatePorts
// ---------------------------------------------------------------------------
void BasicNodeItem::applyParamsAndUpdatePorts(const QString& newParamsJson)
{
    const auto descOpt = s7::ElementRegistry::instance().getDescriptor(typeName_.toStdString());

    // Snap-shot which connectors currently exist (by name).
    std::vector<std::string> oldConnectorNames;
    for (const auto& c : connectors()) {
        oldConnectorNames.push_back(c->text().toStdString());
    }

    // Compute new port counts.
    int newInputs  = inputPorts_;
    int newOutputs = outputPorts_;
    if (descOpt && descOpt->compute_port_layout) {
        try {
            const boost::json::value paramsJsonVal =
                boost::json::parse(newParamsJson.toStdString());
            const s7::ParamMap paramMap =
                s7::dto::NetlistJsonMapper::paramMapFromJson(paramsJsonVal);
            const auto [ni, no] = descOpt->compute_port_layout(paramMap);
            newInputs  = ni;
            newOutputs = no;
        } catch (...) {
            // Keep existing port counts if parsing fails.
        }
    } else if (descOpt) {
        newInputs  = descOpt->default_input_count;
        newOutputs = descOpt->default_output_count;
    }

    setParamsJson(newParamsJson);
    const std::vector<s7::PortDescriptor> ports = descOpt ? descOpt->ports
                                                          : std::vector<s7::PortDescriptor>{};
    setPortLayout(newInputs, newOutputs, ports);

    // Compute the set of connector names that were removed.
    std::vector<std::string> newConnectorNames;
    for (const auto& c : connectors()) {
        newConnectorNames.push_back(c->text().toStdString());
    }

    std::vector<std::string> removedNames;
    for (const auto& oldName : oldConnectorNames) {
        const bool stillPresent = std::ranges::any_of(
            newConnectorNames, [&](const std::string& n) { return n == oldName; });
        if (!stillPresent) {
            removedNames.push_back(oldName);
        }
    }

    if (!removedNames.empty()) {
        markBrokenWires(removedNames);
    }
}

// ---------------------------------------------------------------------------
// markBrokenWires
// ---------------------------------------------------------------------------
void BasicNodeItem::markBrokenWires(const std::vector<std::string>& removedConnectorNames)
{
    auto* qscene = static_cast<QSchematic::Scene*>(scene());
    if (!qscene) return;

    const auto wireManager = qscene->wire_manager();
    if (!wireManager) return;

    // Build a lookup of connectors that no longer exist by their wire attachments.
    // Strategy: iterate all wires, for each wire check if both endpoints are
    // still attached to live connectors; if not → broken.
    const auto allNodes = qscene->items<QSchematic::Items::Node>();
    std::unordered_set<const QSchematic::Items::Connector*> liveConnectors;
    for (const auto& node : allNodes) {
        for (const auto& c : node->connectors()) {
            liveConnectors.insert(c.get());
        }
    }

    const auto allWires = qscene->items<QSchematic::Items::Wire>();
    for (const auto& wire : allWires) {
        if (!wire) continue;

        bool wireHasOrphanedEnd = false;

        // For every connector that the wire is attached to:
        // Check if that connector still exists among live connectors.
        // We do this by asking the wireManager for all connections on this wire
        // by scanning all connectors it knows about.
        // Simpler heuristic: if the wire is no longer attached (via wireManager)
        // at both ends → it has an orphaned end.
        int attachedCount = 0;
        for (const auto& liveConnector : liveConnectors) {
            const auto rec = wireManager->attached_wire(liveConnector);
            if (rec && rec->wire == wire.get()) {
                ++attachedCount;
            }
        }

        // A wire should be attached at exactly two ends (from one connector, to another).
        // If fewer than 2 live connectors are recording this wire → at least one end is orphaned.
        // (A freshly-detached connector no longer shows up in liveConnectors after clearConnectors.)
        if (attachedCount < 2) {
            wireHasOrphanedEnd = true;
        }

        if (auto* s7wire = dynamic_cast<S7WireItem*>(wire.get())) {
            s7wire->setBroken(wireHasOrphanedEnd);
        }
    }
}

// ---------------------------------------------------------------------------
// Shape and Style Methods
// ---------------------------------------------------------------------------
void BasicNodeItem::setVisualStyle(const QColor& baseColor, Shape shape)
{
    setShape(shape);
    setFillColor(baseColor);
    refreshToolTip();
    update();
}

void BasicNodeItem::setShape(Shape shape)
{
    if (shape_ != shape) {
        shape_ = shape;
        update();
    }
}

BasicNodeItem::Shape BasicNodeItem::nodeShape() const
{
    return shape_;
}

void BasicNodeItem::setBorderColor(const QColor& color)
{
    if (borderColor_ != color) {
        borderColor_ = color;
        update();
    }
}

void BasicNodeItem::setFillColor(const QColor& color)
{
    if (color.isValid()) {
        fillColor_ = color;
    } else {
        fillColor_ = QColor(QStringLiteral("#e0e0e0"));
    }
    if (fillColor_.saturation() > 100) {
        fillColor_.setHsv(fillColor_.hue(), 100, std::max(200, fillColor_.value()));
    }
    update();
}

QColor BasicNodeItem::borderColor() const
{
    return borderColor_;
}

QColor BasicNodeItem::fillColor() const
{
    return fillColor_;
}


// ---------------------------------------------------------------------------
// paint
// ---------------------------------------------------------------------------
void BasicNodeItem::paint(QPainter* painter,
                          const QStyleOptionGraphicsItem* option,
                          QWidget* widget)
{
    Q_UNUSED(option);
    Q_UNUSED(widget);
    if (!painter) return;

    painter->save();

    const QRectF rect(0, 0, size().width(), size().height());

    // Dispatch to shape-specific paint method
    switch (shape_) {
        case Shape::Rectangle:
            paintRectangle(painter, rect);
            break;
        case Shape::RoundedRect:
            paintRoundedRect(painter, rect);
            break;
        case Shape::Diamond:
            paintDiamond(painter, rect);
            break;
        case Shape::Hexagon:
            paintHexagon(painter, rect);
            break;
        case Shape::Circle:
            paintCircle(painter, rect);
            break;
        case Shape::Custom:
            paintRoundedRect(painter, rect);  // Fallback to rounded rect
            break;
    }

    // Draw node title if visible
    const qreal lod = option->levelOfDetailFromTransform(painter->worldTransform());
    if (lod >= 0.5) {
        painter->setPen(Qt::black);
        QFont font = painter->font();
        font.setPointSizeF(3.0);
        painter->setFont(font);

        QTextOption textOption(Qt::AlignHCenter | Qt::AlignTop);
        textOption.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
        const QRectF titleRect = rect.adjusted(0.8, kNodeTitleTopPaddingMm, -0.8, 0.0);
        painter->drawText(titleRect, nodeName_, textOption);
    }

    painter->restore();
}

// ---------------------------------------------------------------------------
// Shape painting helpers (using QPainter primitives like QSchematic demo)
// ---------------------------------------------------------------------------

void BasicNodeItem::paintRectangle(QPainter* painter, const QRectF& rect)
{
    QPen pen(borderColor_);
    pen.setWidthF(0.3);
    QBrush brush(fillColor_);

    painter->setPen(pen);
    painter->setBrush(brush);
    painter->drawRect(rect);
}

void BasicNodeItem::paintRoundedRect(QPainter* painter, const QRectF& rect)
{
    const qreal radius = _settings.gridSize / 2;

    QPen pen(borderColor_);
    pen.setWidthF(0.3);
    QBrush brush(fillColor_);

    painter->setPen(pen);
    painter->setBrush(brush);
    painter->drawRoundedRect(rect, radius, radius);
}

void BasicNodeItem::paintDiamond(QPainter* painter, const QRectF& rect)
{
    // Create a diamond polygon (4 points)
    QPolygon diamond;
    diamond << QPoint(rect.width() / 2, 0);                    // Top
    diamond << QPoint(rect.width(), rect.height() / 2);       // Right
    diamond << QPoint(rect.width() / 2, rect.height());       // Bottom
    diamond << QPoint(0, rect.height() / 2);                  // Left

    QPen pen(borderColor_);
    pen.setWidthF(0.3);
    QBrush brush(fillColor_);

    painter->setPen(pen);
    painter->setBrush(brush);
    painter->drawPolygon(diamond);
}

void BasicNodeItem::paintHexagon(QPainter* painter, const QRectF& rect)
{
    // Create a hexagon polygon (6 points)
    const qreal w = rect.width();
    const qreal h = rect.height();
    const qreal qw = w * 0.25;   // Quarter width
    const qreal hh = h * 0.5;    // Half height

    QPolygon hexagon;
    hexagon << QPoint(qw, 0);              // Top-left
    hexagon << QPoint(w - qw, 0);          // Top-right
    hexagon << QPoint(w, hh);              // Middle-right
    hexagon << QPoint(w - qw, h);          // Bottom-right
    hexagon << QPoint(qw, h);              // Bottom-left
    hexagon << QPoint(0, hh);              // Middle-left

    QPen pen(borderColor_);
    pen.setWidthF(0.3);
    QBrush brush(fillColor_);

    painter->setPen(pen);
    painter->setBrush(brush);
    painter->drawPolygon(hexagon);
}

void BasicNodeItem::paintCircle(QPainter* painter, const QRectF& rect)
{
    QPen pen(borderColor_);
    pen.setWidthF(0.3);
    QBrush brush(fillColor_);

    painter->setPen(pen);
    painter->setBrush(brush);
    painter->drawEllipse(rect);
}

// ---------------------------------------------------------------------------
// sizeChangedEvent
// ---------------------------------------------------------------------------
void BasicNodeItem::sizeChangedEvent(QSizeF /*oldSize*/, QSizeF /*newSize*/)
{
    relayoutConnectors();
}

// ---------------------------------------------------------------------------
// mouseDoubleClickEvent
// ---------------------------------------------------------------------------
void BasicNodeItem::mouseDoubleClickEvent(QGraphicsSceneMouseEvent* event)
{
    openPropertiesDialog();
    if (event) event->accept();
}

// ---------------------------------------------------------------------------
// openPropertiesDialog
// ---------------------------------------------------------------------------
void BasicNodeItem::openPropertiesDialog()
{
    const auto descOpt = s7::ElementRegistry::instance().getDescriptor(typeName_.toStdString());

    // Build a port-hint string for the dialog ("2 inputs, 1 output").
    QString portHint;
    if (descOpt) {
        portHint = QString("%1 input(s), %2 output(s)").arg(inputPorts_).arg(outputPorts_);
        if (descOpt->compute_port_layout) {
            portHint += QStringLiteral(" — recalculated from params on OK");
        }
    }

    s7ui::dialogs::PropertiesDialog dialog(
        typeName_, nodeId_, nodeName_, paramsJson_, portHint, nullptr);

    if (dialog.exec() != QDialog::Accepted) return;

    setNodeName(dialog.nodeName());
    setNodeId(dialog.nodeId());
    applyParamsAndUpdatePorts(dialog.paramsJson());
}

// ---------------------------------------------------------------------------
// refreshToolTip
// ---------------------------------------------------------------------------
void BasicNodeItem::refreshToolTip()
{
    setToolTip(QString("%1\nname: %2\nid: %3")
                   .arg(typeName_)
                   .arg(nodeName_)
                   .arg(nodeId_.isEmpty() ? QStringLiteral("<unset>") : nodeId_));
}

// ---------------------------------------------------------------------------
// defaultParamsJsonFor  (static helper)
// ---------------------------------------------------------------------------
QString BasicNodeItem::defaultParamsJsonFor(const std::string& typeName)
{
    const auto descOpt = s7::ElementRegistry::instance().getDescriptor(typeName);
    if (!descOpt || descOpt->default_params.empty()) return QStringLiteral("{}");

    boost::json::object obj;
    for (const auto& [key, val] : descOpt->default_params) {
        if (std::holds_alternative<s7::Real>(val)) {
            obj[key] = std::get<s7::Real>(val);
        } else if (std::holds_alternative<std::string>(val)) {
            obj[key] = std::get<std::string>(val);
        } else if (std::holds_alternative<s7::Complex>(val)) {
            const auto& c = std::get<s7::Complex>(val);
            obj[key] = boost::json::object{{"real", c.real()}, {"imag", c.imag()}};
        }
    }
    return QString::fromStdString(boost::json::serialize(obj));
}

} // namespace s7ui::elements
