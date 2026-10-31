// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-S7-plugin-permission

#include "diagram/items.hpp"

#include "diagram/diagram_scene.hpp"
#include "diagram/element_ports.hpp"
#include "json_utils.hpp"

#include <s7/element_registry.hpp>

#include <qschematic/items/label.hpp>

#include <QBrush>
#include <QCoreApplication>
#include <QFont>
#include <QFontMetricsF>
#include <QGraphicsSceneContextMenuEvent>
#include <QGraphicsSceneHoverEvent>
#include <QGraphicsSceneMouseEvent>
#include <QGraphicsSimpleTextItem>
#include <QLineF>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QTextOption>
#include <QTimer>

#include <algorithm>
#include <cmath>
#include <optional>

namespace s7studio::diagram {

namespace {

constexpr double kPortMarker = 5.0;  // half size of the port symbol
constexpr double kPortHitArea = 8.0; // half size of the area a press on the port hits
constexpr double kHandle = 3.5;

const QColor kNodeBorder(0x3c, 0x4a, 0x60);
const QColor kSelected(0x4f, 0x6f, 0x9c);
const QColor kWire(0x20, 0x20, 0x20);
const QColor kProblem(0xc6, 0x28, 0x28);
const QColor kTypeLabel(0x30, 0x30, 0x90);
const QColor kProbeMarker(0x2e, 0x7d, 0x32);
constexpr double kProbeMarkerRadius = 3.5;

bool isSchemaBacked(const ElementNode::Element& element)
{
    if (element.schema.is_object()) return true;
    const auto* descriptor = s7::ElementRegistry::instance().find(element.type);
    return descriptor && descriptor->metadata().schema_definition.has_value();
}

QColor mix(const QColor& first, const QColor& second, const double secondWeight)
{
    const double firstWeight = 1.0 - secondWeight;
    return QColor::fromRgbF(first.redF() * firstWeight + second.redF() * secondWeight,
                            first.greenF() * firstWeight + second.greenF() * secondWeight,
                            first.blueF() * firstWeight + second.blueF() * secondWeight);
}

double luminance(const QColor& color)
{
    const auto linear = [](const double channel) {
        return channel <= 0.04045 ? channel / 12.92 : std::pow((channel + 0.055) / 1.055, 2.4);
    };
    return 0.2126 * linear(color.redF()) + 0.7152 * linear(color.greenF()) + 0.0722 * linear(color.blueF());
}

double contrast(const QColor& first, const QColor& second)
{
    const double a = luminance(first), b = luminance(second);
    return (std::max(a, b) + 0.05) / (std::min(a, b) + 0.05);
}

QColor familyColor(const ElementNode::Element& element)
{
    if (isSchemaBacked(element)) return QColor(0x91, 0x7e, 0xa7); // muted violet
    if (isObserverElementType(element.type)) return QColor(0x6c, 0x96, 0x91); // sea green
    const auto* descriptor = s7::ElementRegistry::instance().find(element.type);
    if (!descriptor) return QColor(0x83, 0x91, 0x9c); // unavailable type
    const auto& category = descriptor->metadata().category;
    if (category == "basic") return QColor(0x79, 0x9b, 0x80);
    if (category == "filters") return QColor(0x75, 0x91, 0xaf);
    if (category == "sources") return QColor(0x79, 0x9f, 0xbc);
    if (category == "control") return QColor(0xa6, 0x89, 0x79);
    if (category == "conversion") return QColor(0x72, 0x9c, 0xa0);
    if (category == "measurement") return QColor(0x8d, 0x9d, 0x76);
    if (category == "network") return QColor(0x84, 0x8d, 0xb0);
    if (category == "io") return QColor(0xa8, 0x91, 0x75);
    if (category == "spectral") return QColor(0x9b, 0x86, 0xa9);
    if (category == "noise" || category == "simulation") return QColor(0xaa, 0x9c, 0x72);
    const QColor metadata(QString::fromStdString(descriptor->metadata().visual.color));
    return metadata.isValid() ? mix(metadata, QColor(0x87, 0x96, 0x9e), 0.55) : QColor(0x83, 0x91, 0x9c);
}

struct ElementColors {
    QColor fill;
    QColor border;
    QColor title;
    QColor type;
};

ElementColors elementColors(const ElementNode::Element& element)
{
    const QColor accent = familyColor(element);
    const QColor fill = mix(accent, Qt::white, 0.78);
    QColor title = accent;
    for (int i = 0; i < 16 && contrast(title, fill) < 4.5; ++i) {
        title = mix(title, QColor(0x24, 0x30, 0x3a), 0.16);
    }
    return {fill, accent, title, mix(title, QColor(0x3f, 0x4b, 0x56), 0.25)};
}

// Grid rows of an element `rows` high holding `count` ports: two rows apart
// where they fit, centred.
auto portRow(const int rows, const int count, const int index) -> int
{
    const int pitch = rows - 2 >= 2 * (count - 1) ? 2 : 1;
    return (rows - pitch * (count - 1)) / 2 + pitch * index;
}

// Defers dropping a removed item: Qt may still touch it in this event loop round.
void releaseLater(std::shared_ptr<QSchematic::Items::Item> item)
{
    if (QCoreApplication::instance()) {
        QTimer::singleShot(0, QCoreApplication::instance(), [item = std::move(item)] {});
    }
}

} // namespace

// PortConnector

PortConnector::PortConnector(const PortDirection direction, const int index, QString name, QGraphicsItem* parent)
    : Connector(kPortConnectorType, QPoint(), QString(), parent)
    , direction_(direction)
    , index_(index)
    , name_(std::move(name))
{
    label()->setVisible(false);
    setToolTip(QStringLiteral("%1 %2").arg(direction_ == PortDirection::Input ? QStringLiteral("input") : QStringLiteral("output"), name_));
    setHighlightEnabled(false);
    setCursor(Qt::CrossCursor);
}

ElementNode* PortConnector::element() const
{
    return dynamic_cast<ElementNode*>(parentItem());
}

void PortConnector::setMark(const Mark mark)
{
    if (mark_ != mark) {
        mark_ = mark;
        update();
    }
}

std::shared_ptr<QSchematic::Items::Item> PortConnector::deepCopy() const
{
    auto clone = std::make_shared<PortConnector>(direction_, index_, name_);
    copyAttributes(*clone);
    return clone;
}

QRectF PortConnector::boundingRect() const
{
    return {-kPortHitArea, -kPortHitArea, 2 * kPortHitArea, 2 * kPortHitArea};
}

QPainterPath PortConnector::shape() const
{
    QPainterPath path;
    path.addRect(boundingRect());
    return path;
}

void PortConnector::paint(QPainter* painter, const QStyleOptionGraphicsItem*, QWidget*)
{
    painter->setRenderHint(QPainter::Antialiasing);
    // A triangle pointing in the direction of the signal flow (left to right).
    const double left = direction_ == PortDirection::Input ? -kPortMarker : 0.0;
    const QPolygonF triangle{QPointF(left, -kPortMarker), QPointF(left + kPortMarker, 0.0), QPointF(left, kPortMarker)};
    QColor fill = kNodeBorder;
    if (mark_ == Mark::Target) {
        painter->setPen(QPen(QColor(0x2e, 0x9d, 0x46), 2.0));
        painter->setBrush(Qt::NoBrush);
        painter->drawEllipse(QPointF(0.0, 0.0), kPortHitArea - 1.0, kPortHitArea - 1.0);
        fill = QColor(0x2e, 0x9d, 0x46);
    } else if (mark_ == Mark::Refused) {
        painter->setOpacity(0.3);
    }
    painter->setPen(Qt::NoPen);
    painter->setBrush(fill);
    painter->drawPolygon(triangle);
}

// ElementNode

ElementNode::ElementNode(Element element)
    : Node(kElementNodeType)
    , element_(std::move(element))
{
    setAllowMouseResize(false);
    setAllowMouseRotate(false);
    setConnectorsMovable(false);
    setConnectorsSnapToGrid(false);
    setHighlightEnabled(false);
    setAcceptHoverEvents(true);
    rebuildPorts();
}

QSize ElementNode::gridSize() const
{
    return {static_cast<int>(std::lround(width() / kGridStep)), static_cast<int>(std::lround(height() / kGridStep))};
}

QSize ElementNode::minimumGridSize() const
{
    const int ports = static_cast<int>(std::max(inputs_.size(), outputs_.size()));
    return {kMinimumElementWidthSteps, std::max(5, 2 * ports + 1)};
}

void ElementNode::setGridSize(QSize steps)
{
    steps = steps.expandedTo(minimumGridSize());
    steps.setWidth(std::min(steps.width(), kMaximumElementSizeSteps));
    steps.setHeight(std::min(steps.height(), kMaximumElementSizeSteps));
    setSize(steps.width() * kGridStep, steps.height() * kGridStep);
    placePorts();
    update();
}

QRectF ElementNode::resizeHandleSceneRect() const
{
    constexpr double hit = 16.0;
    return QRectF(pos().x() + width() - hit, pos().y() + height() - hit, hit, hit);
}

QString ElementNode::title() const
{
    return element_.title.isEmpty() ? QString::fromStdString(element_.id) : element_.title;
}

void ElementNode::setElement(Element element)
{
    element_ = std::move(element);
    rebuildPorts();
    update();
}

void ElementNode::setMinimumPorts(const int inputs, const int outputs)
{
    minimumInputs_ = inputs;
    minimumOutputs_ = outputs;
    rebuildPorts();
}

std::shared_ptr<PortConnector> ElementNode::port(const PortDirection direction, const int index) const
{
    const auto& ports = direction == PortDirection::Input ? inputs_ : outputs_;
    return index >= 0 && index < static_cast<int>(ports.size()) ? ports[static_cast<std::size_t>(index)] : nullptr;
}

void ElementNode::rebuildPorts()
{
    auto names = elementPorts(element_.type, element_.params, element_.schema);
    for (int index = static_cast<int>(names.inputs.size()); index < minimumInputs_; ++index) {
        names.inputs.push_back(QStringLiteral("in%1").arg(index));
    }
    for (int index = static_cast<int>(names.outputs.size()); index < minimumOutputs_; ++index) {
        names.outputs.push_back(QStringLiteral("out%1").arg(index));
    }
    const int ports = static_cast<int>(std::max(names.inputs.size(), names.outputs.size()));
    const int rows = std::max(5, 2 * ports + 1);
    const auto current = gridSize();
    setSize(std::max(kMinimumElementWidthSteps, geometryInitialized_ ? current.width() : kElementWidthSteps) * kGridStep,
            std::max(rows, geometryInitialized_ ? current.height() : rows) * kGridStep);
    geometryInitialized_ = true;

    auto sync = [&](std::vector<std::shared_ptr<PortConnector>>& ports, const std::vector<QString>& wanted, const PortDirection direction) {
        while (ports.size() > wanted.size()) {
            auto removed = ports.back();
            ports.pop_back();
            removeConnector(removed);
            if (auto* graphicsScene = removed->QGraphicsItem::scene()) {
                graphicsScene->removeItem(removed.get());
            }
            releaseLater(std::move(removed));
        }
        for (std::size_t index = 0; index < wanted.size(); ++index) {
            if (index >= ports.size() || ports[index]->name() != wanted[index]) {
                auto port = std::make_shared<PortConnector>(direction, static_cast<int>(index), wanted[index]);
                if (index < ports.size()) {
                    removeConnector(ports[index]);
                    if (auto* graphicsScene = ports[index]->QGraphicsItem::scene()) {
                        graphicsScene->removeItem(ports[index].get());
                    }
                    releaseLater(ports[index]);
                    ports[index] = port;
                } else {
                    ports.push_back(port);
                }
                addConnector(port);
            }
        }
    };
    sync(inputs_, names.inputs, PortDirection::Input);
    sync(outputs_, names.outputs, PortDirection::Output);
    placePorts();
}

void ElementNode::placePorts()
{
    const int rows = gridSize().height();
    for (const auto* ports : {&inputs_, &outputs_}) {
        const bool input = ports == &inputs_;
        for (std::size_t index = 0; index < ports->size(); ++index) {
            (*ports)[index]->setPos(input ? 0.0 : width(),
                                    portRow(rows, static_cast<int>(ports->size()), static_cast<int>(index)) * kGridStep);
        }
    }
    if (placementLabel_) {
        placementLabel_->setPos(0.0, height() + 2.0);
    }
}

void ElementNode::setPlacementLabel(const QString& text)
{
    if (text.isEmpty()) {
        delete placementLabel_;
        placementLabel_ = nullptr;
        return;
    }
    if (!placementLabel_) {
        placementLabel_ = new QGraphicsSimpleTextItem(this);
        QFont font = placementLabel_->font();
        font.setPointSizeF(font.pointSizeF() * 0.85);
        placementLabel_->setFont(font);
        placementLabel_->setBrush(QColor(0x20, 0x70, 0x40));
    }
    placementLabel_->setText(text);
    placementLabel_->setPos(0.0, height() + 2.0);
}

void ElementNode::setProblem(const QString& problem)
{
    if (problem != problem_) {
        problem_ = problem;
        setToolTip(problem_);
        update();
    }
}

std::shared_ptr<QSchematic::Items::Item> ElementNode::deepCopy() const
{
    auto clone = std::make_shared<ElementNode>(element_);
    clone->setMinimumPorts(minimumInputs_, minimumOutputs_);
    clone->setGridSize(gridSize());
    clone->setPos(pos());
    return clone;
}

void ElementNode::hoverMoveEvent(QGraphicsSceneHoverEvent* event)
{
    Node::hoverMoveEvent(event);
    if (isSelected() && resizeHandleSceneRect().contains(mapToScene(event->pos()))) setCursor(Qt::SizeFDiagCursor);
}

void ElementNode::hoverLeaveEvent(QGraphicsSceneHoverEvent* event)
{
    unsetCursor();
    Node::hoverLeaveEvent(event);
}

void ElementNode::paint(QPainter* painter, const QStyleOptionGraphicsItem*, QWidget*)
{
    painter->setRenderHint(QPainter::Antialiasing);
    const auto colors = elementColors(element_);
    const QColor border = !problem_.isEmpty() ? kProblem : (isSelected() ? kSelected : colors.border);
    painter->setPen(QPen(border, isSelected() || !problem_.isEmpty() ? 2.0 : 1.2));
    painter->setBrush(colors.fill);
    painter->drawRoundedRect(sizeRect(), 6.0, 6.0);
    if (isSchemaBacked(element_)) {
        painter->setBrush(Qt::NoBrush);
        painter->drawRoundedRect(sizeRect().adjusted(5.0, 5.0, -5.0, -5.0), 4.0, 4.0);
    }

    const QRectF text = sizeRect().adjusted(12.0, 6.0, -12.0, -6.0);
    QFont titleFont = painter->font();
    titleFont.setBold(true);
    painter->setFont(titleFont);
    painter->setPen(colors.title);
    const QFontMetricsF titleMetrics(titleFont);
    painter->drawText(QRectF(text.left(), text.top(), text.width(), titleMetrics.height()), Qt::AlignLeft | Qt::AlignVCenter,
                      titleMetrics.elidedText(title(), Qt::ElideRight, text.width()));

    QFont typeFont = painter->font();
    typeFont.setBold(false);
    typeFont.setPointSizeF(typeFont.pointSizeF() * 0.9);
    painter->setFont(typeFont);
    painter->setPen(colors.type);
    const QFontMetricsF typeMetrics(typeFont);
    painter->drawText(QRectF(text.left(), text.top() + titleMetrics.height(), text.width(), typeMetrics.height()),
                      Qt::AlignLeft | Qt::AlignVCenter,
                      typeMetrics.elidedText(QString::fromStdString(element_.type), Qt::ElideRight, text.width()));
    const auto* descriptor = s7::ElementRegistry::instance().find(element_.type);
    if (descriptor && descriptor->metadata().stateless) {
        QFont markerFont = typeFont;
        markerFont.setPointSizeF(markerFont.pointSizeF() * 0.85);
        painter->setFont(markerFont);
        painter->setPen(colors.title);
        painter->drawText(QRectF(text.left(), height() - 26.0, width() - 36.0, 17.0),
                          Qt::AlignLeft | Qt::AlignVCenter, QStringLiteral("stateless"));
    }
    if (isSelected()) {
        const QRectF handle(width() - 15.0, height() - 15.0, 12.0, 12.0);
        painter->setPen(QPen(kSelected, 1.5));
        painter->setBrush(colors.fill);
        painter->drawRect(handle);
        painter->drawLine(handle.topLeft() + QPointF(3, 9), handle.topLeft() + QPointF(9, 3));
    }
}

// TextFrameNode

TextFrameNode::TextFrameNode(std::string id, const boost::json::object& params)
    : Node(kTextFrameNodeType)
    , id_(std::move(id))
{
    setAllowMouseResize(false);
    setAllowMouseRotate(false);
    setHighlightEnabled(false);
    setParams(params);
}

auto TextFrameNode::defaultParams() -> boost::json::object
{
    return boost::json::object{
        {"text", "Text frame"},
        {"text_color", "#202020"},
        {"line_color", "#444444"},
        {"fill_color", "#fff4cc"},
        {"font_family", "Sans Serif"},
        {"font_size", kDefaultFontPoints},
        {"width", 260.0},
        {"height", 120.0},
    };
}

void TextFrameNode::setParams(const boost::json::object& params)
{
    params_ = defaultParams();
    for (const auto& [key, value] : params) {
        params_[key] = value;
    }
    const auto number = [&](const char* key, const double fallback) {
        const auto* value = json_utils::findObjectValue(params_, key);
        return value ? json_utils::numberValue(*value, fallback) : fallback;
    };
    params_["width"] = std::max(80.0, number("width", 260.0));
    params_["height"] = std::max(40.0, number("height", 120.0));
    params_["font_size"] = std::max(kMinimumFontPoints, json_utils::intValue(params_, "font_size", kDefaultFontPoints));
    setSize(params_.at("width").as_double(), params_.at("height").as_double());
    update();
}

std::shared_ptr<QSchematic::Items::Item> TextFrameNode::deepCopy() const
{
    auto clone = std::make_shared<TextFrameNode>(id_, params_);
    clone->setPos(pos());
    return clone;
}

void TextFrameNode::paint(QPainter* painter, const QStyleOptionGraphicsItem*, QWidget*)
{
    const auto color = [&](const char* key, const char* fallback) {
        return QColor(QString::fromStdString(json_utils::stringValue(params_, key, fallback)));
    };
    painter->setPen(QPen(isSelected() ? kSelected : color("line_color", "#444444"), isSelected() ? 2.0 : 1.4));
    painter->setBrush(color("fill_color", "#fff4cc"));
    painter->drawRect(sizeRect());

    QFont font(QString::fromStdString(json_utils::stringValue(params_, "font_family", "Sans Serif")));
    font.setPointSize(json_utils::intValue(params_, "font_size", kDefaultFontPoints));
    painter->setFont(font);
    painter->setPen(color("text_color", "#202020"));
    QTextOption option(Qt::AlignLeft | Qt::AlignTop);
    option.setWrapMode(QTextOption::WordWrap);
    painter->drawText(sizeRect().adjusted(10.0, 8.0, -10.0, -8.0), QString::fromStdString(json_utils::stringValue(params_, "text", "")), option);
}

// WireEnd

auto WireEnd::onPort(const std::shared_ptr<ElementNode>& node, const PortDirection direction, const int index) -> WireEnd
{
    WireEnd end;
    end.kind = Kind::Port;
    end.node = node;
    end.direction = direction;
    end.index = index;
    return end;
}

auto WireEnd::onWire(const std::shared_ptr<SignalWire>& wire, const QPointF at) -> WireEnd
{
    WireEnd end;
    end.kind = Kind::Junction;
    end.wire = wire;
    end.at = at;
    return end;
}

auto WireEnd::port() const -> std::shared_ptr<PortConnector>
{
    const auto element = node.lock();
    return isPort() && element ? element->port(direction, index) : nullptr;
}

bool WireEnd::sameAs(const WireEnd& other) const
{
    if (kind != other.kind) {
        return false;
    }
    if (isPort()) {
        return node.lock() == other.node.lock() && direction == other.direction && index == other.index;
    }
    if (isJunction()) {
        return wire.lock() == other.wire.lock() && at == other.at;
    }
    return true;
}

// SignalWire

SignalWire::SignalWire()
    : Wire(kSignalWireType)
{
    setMovable(false);
    setHighlightEnabled(false);
}

void SignalWire::setRoute(const QVector<QPointF>& points)
{
    about_to_change();
    m_points.clear();
    for (const auto& point : points) {
        m_points.append(wire_system::point(point));
    }
    if (upstream.isJunction() && !m_points.isEmpty()) {
        m_points.first().set_is_junction(true);
    }
    has_changed();
    update();
}

void SignalWire::setTypeLabel(const QString& text, const bool ok)
{
    if (text != typeLabel_ || ok != typeOk_) {
        prepareGeometryChange();
        typeLabel_ = text;
        typeOk_ = ok;
        setToolTip(problem_.isEmpty() ? typeLabel_ : problem_);
        update();
    }
}

void SignalWire::setProblem(const QString& problem)
{
    if (problem != problem_) {
        problem_ = problem;
        setToolTip(problem_.isEmpty() ? typeLabel_ : problem_);
        update();
    }
}

void SignalWire::setProbeLabel(const QString& name)
{
    if (name != probeLabel_) {
        prepareGeometryChange();
        probeLabel_ = name;
        update();
    }
}

std::shared_ptr<QSchematic::Items::Item> SignalWire::deepCopy() const
{
    auto clone = std::make_shared<SignalWire>();
    clone->upstream = upstream;
    clone->downstream = downstream;
    clone->userBends = userBends;
    clone->setRoute(pointsAbsolute());
    return clone;
}

namespace {

auto labelFont() -> QFont
{
    QFont font;
    font.setPointSizeF(font.pointSizeF() * 0.8);
    return font;
}

// Where the type label goes: above the first horizontal segment, else beside the first segment.
auto labelRect(const QVector<QPointF>& points, const QString& text) -> QRectF
{
    if (points.size() < 2 || text.isEmpty()) {
        return {};
    }
    const QFontMetricsF metrics(labelFont());
    const QSizeF size(metrics.horizontalAdvance(text) + 2.0, metrics.height());
    for (int index = 0; index + 1 < points.size(); ++index) {
        const QPointF a = points[index];
        const QPointF b = points[index + 1];
        if (std::abs(a.y() - b.y()) < 0.5 && std::abs(a.x() - b.x()) >= size.width() * 0.5) {
            return {QPointF(std::min(a.x(), b.x()) + 4.0, a.y() - size.height() - 1.0), size};
        }
    }
    return {points[0] + QPointF(4.0, -size.height() - 1.0), size};
}

// The point halfway along the route by path length, for the probe marker.
auto midpointOfRoute(const QVector<QPointF>& points) -> std::optional<QPointF>
{
    if (points.size() < 2) {
        return std::nullopt;
    }
    double total = 0.0;
    for (int index = 0; index + 1 < points.size(); ++index) {
        total += QLineF(points[index], points[index + 1]).length();
    }
    if (total <= 0.0) {
        return points[0];
    }
    double walked = 0.0;
    for (int index = 0; index + 1 < points.size(); ++index) {
        const QLineF segment(points[index], points[index + 1]);
        const double length = segment.length();
        if (walked + length >= total / 2.0) {
            return segment.pointAt(length > 0.0 ? (total / 2.0 - walked) / length : 0.0);
        }
        walked += length;
    }
    return points.last();
}

auto probeMarkerRect(const QVector<QPointF>& points, const QString& name) -> QRectF
{
    const auto center = midpointOfRoute(points);
    if (!center || name.isEmpty()) {
        return {};
    }
    const QFontMetricsF metrics(labelFont());
    const QSizeF size(metrics.horizontalAdvance(name) + 2.0, metrics.height());
    return {*center + QPointF(kProbeMarkerRadius + 3.0, -size.height() / 2.0), size};
}

} // namespace

QRectF SignalWire::boundingRect() const
{
    return Wire::boundingRect()
        .united(labelRect(pointsRelative(), typeLabel_))
        .united(probeMarkerRect(pointsRelative(), probeLabel_))
        .adjusted(-kHandle, -kHandle, kHandle, kHandle);
}

void SignalWire::paint(QPainter* painter, const QStyleOptionGraphicsItem*, QWidget*)
{
    const auto points = pointsRelative();
    if (points.size() < 2) {
        return;
    }
    painter->setRenderHint(QPainter::Antialiasing);
    const auto target = downstream.node.lock();
    const bool observationLink = target && isObserverElementType(target->element().type);
    const QColor color = !problem_.isEmpty() ? kProblem : (isSelected() ? kSelected : (observationLink ? kProbeMarker : kWire));
    painter->setPen(QPen(color, isSelected() ? 2.0 : 1.4, observationLink ? Qt::DashLine : Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter->setBrush(Qt::NoBrush);
    painter->drawPolyline(points.constData(), static_cast<int>(points.size()));

    if (upstream.isJunction()) {
        painter->setPen(Qt::NoPen);
        painter->setBrush(color);
        painter->drawEllipse(points.first(), 3.5, 3.5);
    }

    if (isSelected()) {
        // Handles: filled for bends placed by hand, hollow for routed ones.
        for (int index = 1; index + 1 < points.size(); ++index) {
            const bool user = userBends.contains(points[index] + pos());
            painter->setPen(QPen(kSelected, 1.0));
            painter->setBrush(user ? QBrush(kSelected) : QBrush(Qt::white));
            painter->drawRect(QRectF(points[index] - QPointF(kHandle, kHandle), QSizeF(2 * kHandle, 2 * kHandle)));
        }
    }

    if (const auto rect = labelRect(points, typeLabel_); !rect.isEmpty()) {
        painter->setFont(labelFont());
        painter->setPen(typeOk_ ? kTypeLabel : kProblem);
        painter->drawText(rect, Qt::AlignLeft | Qt::AlignVCenter, typeLabel_);
    }

    if (!probeLabel_.isEmpty()) {
        if (const auto center = midpointOfRoute(points)) {
            painter->setPen(Qt::NoPen);
            painter->setBrush(kProbeMarker);
            painter->drawEllipse(*center, kProbeMarkerRadius, kProbeMarkerRadius);
            if (const auto rect = probeMarkerRect(points, probeLabel_); !rect.isEmpty()) {
                painter->setFont(labelFont());
                painter->setPen(kProbeMarker);
                painter->drawText(rect, Qt::AlignLeft | Qt::AlignVCenter, probeLabel_);
            }
        }
    }
}

void SignalWire::mousePressEvent(QGraphicsSceneMouseEvent* event)
{
    QGraphicsObject::mousePressEvent(event);
}

void SignalWire::mouseMoveEvent(QGraphicsSceneMouseEvent* event)
{
    event->accept();
}

void SignalWire::mouseReleaseEvent(QGraphicsSceneMouseEvent* event)
{
    QGraphicsObject::mouseReleaseEvent(event);
}

void SignalWire::mouseDoubleClickEvent(QGraphicsSceneMouseEvent* event)
{
    event->accept();
}

void SignalWire::contextMenuEvent(QGraphicsSceneContextMenuEvent* event)
{
    if (auto* diagramScene = dynamic_cast<DiagramScene*>(scene())) {
        event->accept();
        diagramScene->showWireMenu(*this, event->scenePos(), event->screenPos());
    }
}

} // namespace s7studio::diagram
