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
#include <QAbstractTextDocumentLayout>
#include <QRegularExpression>
#include <QTextCursor>
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
    if (category == "basic" || category == "math") return QColor(0x79, 0x9b, 0x80);
    if (category == "memory_integration") return QColor(0x9b, 0x8b, 0x71);
    if (category == "types_complex") return QColor(0x72, 0x9c, 0xa0);
    if (category == "signal_processing" || category == "filters") return QColor(0x75, 0x91, 0xaf);
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

enum class BasicSymbol { None, Gain, Sum, Product, Delay, Accumulator, Integral };

BasicSymbol basicSymbol(const ElementNode::Element& element)
{
    if (isSchemaBacked(element)) return BasicSymbol::None;
    const auto& type = element.type;
    if (type == "Gain") return BasicSymbol::Gain;
    if (type == "Adder") return BasicSymbol::Sum;
    if (type == "Multiply") return BasicSymbol::Product;
    if (type == "Delay" || type == "PreviousValue") return BasicSymbol::Delay;
    if (type == "Accumulator") return BasicSymbol::Accumulator;
    if (type == "EulerIntegrator" || type == "BackwardEulerIntegrator" || type == "TrapezoidIntegrator" ||
        type == "GaussLegendreIntegrator" || type == "SymplecticIntegrator" || type == "ControlledIntegrator")
        return BasicSymbol::Integral;
    return BasicSymbol::None;
}

QString symbolNumber(const boost::json::value& value, const QString& symbolic = QStringLiteral("K"))
{
    if (value.is_double()) {
        const auto text = QString::number(value.as_double(), 'g', 5);
        return text.toDouble() == value.as_double() ? text : QStringLiteral("≈") + text;
    }
    if (value.is_int64()) return QString::number(value.as_int64());
    if (value.is_uint64()) return QString::number(value.as_uint64());
    if (value.is_string()) return QString::fromStdString(std::string(value.as_string()));
    // Complex coefficients need a mathematical name rather than truncated JSON.
    return symbolic;
}

bool exactUnit(const boost::json::value& value, const int sign)
{
    return (value.is_double() && value.as_double() == sign) ||
           (value.is_int64() && value.as_int64() == sign) ||
           (sign == 1 && value.is_uint64() && value.as_uint64() == 1);
}

QString symbolQualifier(const ElementNode::Element& element)
{
    const auto& type = element.type;
    if (type == "EulerIntegrator") return QStringLiteral("Forward Euler");
    if (type == "BackwardEulerIntegrator") return QStringLiteral("Backward Euler");
    if (type == "GaussLegendreIntegrator") return QStringLiteral("Gauss–Legendre");
    if (type == "SymplecticIntegrator") return QStringLiteral("Symplectic (v first)");
    if (type == "ControlledIntegrator") return QStringLiteral("Controlled integral");
    if (type == "Accumulator") return QStringLiteral("Accumulator · current input");
    if (type == "TrapezoidIntegrator") {
        const auto* mode = element.params.if_contains("mode");
        return mode && mode->is_string() && mode->as_string() == "sampled"
            ? QStringLiteral("Trapezoid · sampled") : QStringLiteral("Trapezoid · implicit");
    }
    if (type == "Delay" || type == "PreviousValue") return QString::fromStdString(type) + QStringLiteral(" · z⁻¹");
    auto text = QString::fromStdString(type);
    const auto* descriptor = s7::ElementRegistry::instance().find(type);
    if (descriptor && descriptor->metadata().stateless) text += QStringLiteral(" · stateless");
    return text;
}

QString integralCode(const ElementNode::Element& element)
{
    if (element.type == "EulerIntegrator") return QStringLiteral("FE");
    if (element.type == "BackwardEulerIntegrator") return QStringLiteral("BE");
    if (element.type == "GaussLegendreIntegrator") return QStringLiteral("GL");
    if (element.type == "SymplecticIntegrator") return QStringLiteral("SE");
    if (element.type == "ControlledIntegrator") return QStringLiteral("CE");
    const auto* mode=element.params.if_contains("mode");
    return mode && mode->is_string() && mode->as_string()=="sampled" ? QStringLiteral("TS") : QStringLiteral("TI");
}

QString symbolDescription(const ElementNode::Element& element)
{
    QString equation;
    switch (basicSymbol(element)) {
    case BasicSymbol::Gain: equation = QStringLiteral("y[n] = K x[n]"); break;
    case BasicSymbol::Sum: equation = QStringLiteral("y[n] = Σᵢ wᵢ xᵢ[n]"); break;
    case BasicSymbol::Product: equation = QStringLiteral("y[n] = Πᵢ (kᵢ xᵢ[n])"); break;
    case BasicSymbol::Delay: equation = QStringLiteral("y[n] = x[n−1], n ≥ 1; y[0] = initial_value"); break;
    case BasicSymbol::Accumulator: equation = QStringLiteral("y[n] = y[n−1] + x[n]; y[−1] = initial_value"); break;
    case BasicSymbol::Integral:
        if (element.type == "EulerIntegrator") {
            const auto* substeps = element.params.if_contains("substeps");
            equation = substeps && !exactUnit(*substeps,1)
                ? QStringLiteral("Coupled forward Euler substeps; see the integrator help")
                : QStringLiteral("y[n+1] = y[n] + h x[n]; y[0] = initial_value");
        } else if (element.type == "BackwardEulerIntegrator")
            equation = QStringLiteral("y[n] = y[n−1] + h x[n], n ≥ 1; y[0] = initial_value");
        else if (element.type == "TrapezoidIntegrator") {
            const auto* mode = element.params.if_contains("mode");
            equation = mode && mode->is_string() && mode->as_string() == "sampled"
                ? QStringLiteral("y[n] = y[n−1] + h/2 (x[n−1]+x[n]), n ≥ 1; y[0] = initial_value")
                : QStringLiteral("y[n+1] = y[n] + h/2 (f[n]+f[n+1]); coupled implicit endpoint stage");
        } else if (element.type == "SymplecticIntegrator")
            equation = QStringLiteral("v[n+1] = v[n] + h a[n]\nx[n+1] = x[n] + h v[n+1]");
        else if (element.type == "GaussLegendreIntegrator")
            equation = QStringLiteral("Two-stage, fourth-order coupled Gauss–Legendre integration");
        else equation = QStringLiteral("Causal rectangle-rule state with enable, reset and limits; see the integrator help");
        break;
    case BasicSymbol::None: return {};
    }
    return symbolQualifier(element) + QStringLiteral("\n") + equation +
        QStringLiteral("\nParameters: ") + QString::fromStdString(boost::json::serialize(element.params));
}

// A symbol is drawn inside the existing node footprint. Short local leads
// connect it to the original grid ports; no saved anchor or router geometry
// changes when a diagram is opened with the mathematical presentation.
bool paintBasicSymbol(const ElementNode& node, QPainter* painter, const QRectF& footprint, const ElementColors& colors)
{
    const auto kind = basicSymbol(node.element());
    if (kind == BasicSymbol::None) return false;
    if (kind == BasicSymbol::Gain && (node.inputs().size() != 1 || node.outputs().size() != 1)) return false;
    const double captionHeight = std::min(34.0, footprint.height() * .34);
    double cy = footprint.center().y();
    const auto& reference = node.outputs().empty() ? node.inputs() : node.outputs();
    if (!reference.empty()) cy = (reference.front()->pos().y() + reference.back()->pos().y()) / 2;
    const double radius = std::max(4.0, std::min({cy-4.0, footprint.height()-captionHeight-cy,
                                                (footprint.width()-12.0)/2}));
    const bool circular = kind == BasicSymbol::Sum || kind == BasicSymbol::Product;
    const double halfWidth = circular ? radius : std::min((footprint.width()-12.0)/2,
        radius * (kind == BasicSymbol::Gain ? 1.5 : 1.3));
    const QRectF body(footprint.center().x()-halfWidth, cy-radius, 2*halfWidth, 2*radius);
    QPainterPath outline;
    if (kind == BasicSymbol::Gain) {
        outline.moveTo(body.topLeft()); outline.lineTo(body.right(),cy); outline.lineTo(body.bottomLeft()); outline.closeSubpath();
    } else if (circular) outline.addEllipse(body);
    else outline.addRect(body);
    painter->drawPath(outline);

    const auto terminal = [&](const bool input, const std::size_t index, const std::size_t count) {
        const double dy = count < 2 ? 0 : radius * 1.6 * (double(index)/double(count-1)-.5);
        if (circular) return QPointF(body.center().x() + (input ? -1 : 1) * std::sqrt(radius*radius-dy*dy),cy+dy);
        return QPointF(input ? body.left() : body.right(),cy+dy);
    };
    painter->save();
    painter->setPen(QPen(kWire, 1.1));
    for (const auto* ports : {&node.inputs(), &node.outputs()}) {
        const bool input = ports == &node.inputs();
        for (std::size_t i=0; i<ports->size(); ++i)
            painter->drawLine((*ports)[i]->pos(), terminal(input,i,ports->size()));
    }
    painter->restore();

    QString glyph;
    switch (kind) {
    case BasicSymbol::Gain:
        glyph = node.element().params.if_contains("factor") ? symbolNumber(node.element().params.at("factor")) : QStringLiteral("1"); break;
    case BasicSymbol::Sum: glyph = QStringLiteral("+"); break;
    case BasicSymbol::Product: glyph = QStringLiteral("×"); break;
    case BasicSymbol::Delay: glyph = QStringLiteral("R"); break;
    case BasicSymbol::Accumulator: glyph = QStringLiteral("Σ"); break;
    case BasicSymbol::Integral:
        glyph = radius < 12 || node.element().type == "SymplecticIntegrator"
            ? integralCode(node.element()) : QStringLiteral("∫"); break;
    case BasicSymbol::None: break;
    }
    painter->save();
    QFont glyphFont = painter->font(); glyphFont.setBold(false);
    glyphFont.setPointSizeF(std::clamp(radius*.65,6.,22.)); painter->setFont(glyphFont);
    painter->setPen(colors.title);
    QRectF glyphRect = body.adjusted(3,2,-3,-2);
    if (kind == BasicSymbol::Gain) glyphRect.setRight(body.left()+body.width()*.7);
    painter->drawText(glyphRect, Qt::AlignCenter, QFontMetricsF(glyphFont).elidedText(glyph,Qt::ElideRight,glyphRect.width()));
    if (kind == BasicSymbol::Integral && radius >= 12 && node.element().type != "SymplecticIntegrator") {
        QFont methodFont = glyphFont; methodFont.setPointSizeF(7.); painter->setFont(methodFont);
        painter->drawText(body.adjusted(2,2,-3,-2), Qt::AlignBottom | Qt::AlignRight, integralCode(node.element()));
    }
    if (kind == BasicSymbol::Sum || kind == BasicSymbol::Product) {
        const bool sum = kind == BasicSymbol::Sum;
        const auto* weights = node.element().params.if_contains(sum ? "weights" : "factors");
        QFont signFont = glyphFont; signFont.setPointSizeF(std::clamp(radius*.3,6.,9.)); painter->setFont(signFont);
        for (std::size_t i=0; i<node.inputs().size(); ++i) {
            QString weight = sum ? QStringLiteral("+") : QString();
            if (weights && weights->is_array() && i<weights->as_array().size()) {
                const auto& value = weights->as_array()[i];
                if (exactUnit(value,1)) weight = sum ? QStringLiteral("+") : QString();
                else if (sum && exactUnit(value,-1)) weight = QStringLiteral("−");
                else weight = symbolNumber(value, QStringLiteral("%1%2").arg(sum ? "w" : "k").arg(i));
            }
            const auto end = terminal(true,i,node.inputs().size());
            const auto start = node.inputs()[i]->pos();
            const auto middle = (start+end)/2;
            const double width = std::max(8.,std::min(end.x()-start.x()-2.,QFontMetricsF(signFont).horizontalAdvance(weight)+4.));
            const QRectF label(middle.x()-width/2,middle.y()-QFontMetricsF(signFont).height()-1,width,QFontMetricsF(signFont).height());
            painter->drawText(label,Qt::AlignCenter,QFontMetricsF(signFont).elidedText(weight,Qt::ElideRight,label.width()));
        }
    }
    if (node.element().type == "SymplecticIntegrator") {
        QFont stateFont = glyphFont; stateFont.setPointSizeF(8.); painter->setFont(stateFont);
        for (std::size_t i=0; i<node.outputs().size() && i<2; ++i) {
            const auto end = terminal(false,i,node.outputs().size());
            const auto start = node.outputs()[i]->pos();
            painter->drawText(QRectF(end.x()+2,end.y()-15,start.x()-end.x()-4,14),
                              Qt::AlignLeft, i == 0 ? QStringLiteral("x") : QStringLiteral("v"));
        }
    }
    const double captionTop = footprint.height()-captionHeight+1;
    QFont titleFont = painter->font(); titleFont.setBold(true);
    titleFont.setPointSizeF(std::clamp(footprint.height()*.085,6.,10.)); painter->setFont(titleFont);
    const QFontMetricsF titleMetrics(titleFont);
    const QRectF titleRect(2,captionTop,footprint.width()-4,titleMetrics.height());
    if (titleRect.bottom() <= footprint.bottom())
        painter->drawText(titleRect,Qt::AlignCenter,titleMetrics.elidedText(node.title(),Qt::ElideRight,titleRect.width()));
    QFont typeFont = titleFont; typeFont.setBold(false); typeFont.setPointSizeF(std::max(6.,titleFont.pointSizeF()*.85));
    painter->setFont(typeFont);painter->setPen(colors.type);
    const QFontMetricsF typeMetrics(typeFont);
    const QRectF typeRect(2,titleRect.bottom()+1,footprint.width()-4,typeMetrics.height());
    if (typeRect.bottom() <= footprint.bottom())
        painter->drawText(typeRect,Qt::AlignCenter,typeMetrics.elidedText(symbolQualifier(node.element()),Qt::ElideRight,typeRect.width()));
    painter->restore();
    return true;
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
    setToolTip(symbolDescription(element_));
}

QSize ElementNode::gridSize() const
{
    return {static_cast<int>(std::lround(width() / kGridStep)), static_cast<int>(std::lround(height() / kGridStep))};
}

QSize ElementNode::minimumGridSize() const
{
    const int ports = static_cast<int>(std::max(inputs_.size(), outputs_.size()));
    return {kMinimumElementWidthSteps, minimumElementRows(ports)};
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
    setToolTip(problem_.isEmpty() ? symbolDescription(element_) : problem_);
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
    const auto current = gridSize();
    setSize(std::max(kMinimumElementWidthSteps, geometryInitialized_ ? current.width() : kElementWidthSteps) * kGridStep,
            (geometryInitialized_ ? elementRowsForPorts(current.height(), ports) : defaultElementRows(ports)) * kGridStep);
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
        setToolTip(problem_.isEmpty() ? symbolDescription(element_) : problem_);
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
    if (paintBasicSymbol(*this,painter,sizeRect(),colors)) {
        if (isSelected()) {
            painter->setBrush(Qt::NoBrush);
            painter->setPen(QPen(kSelected,1.,Qt::DashLine));
            painter->drawRect(sizeRect());
            const QRectF handle(width()-15.,height()-15.,12.,12.);
            painter->setBrush(colors.fill);painter->setPen(QPen(kSelected,1.5));painter->drawRect(handle);
            painter->drawLine(handle.topLeft()+QPointF(3,9),handle.topLeft()+QPointF(9,3));
        }
        return;
    }
    painter->drawRoundedRect(sizeRect(), 6.0, 6.0);
    // Below the full-text size the text, its margins and the inner frame
    // shrink with the element, down to a legible limit (the two-step minimum
    // height still holds the title); lines that do not fit are left out.
    constexpr double kMinimumTextScale = 0.6;
    const double scale = std::clamp(std::min(width() / (kFullTextWidthSteps * kGridStep), height() / (kFullTextHeightSteps * kGridStep)),
                                    kMinimumTextScale, 1.0);
    if (isSchemaBacked(element_)) {
        painter->setBrush(Qt::NoBrush);
        const double inset = 5.0 * scale;
        painter->drawRoundedRect(sizeRect().adjusted(inset, inset, -inset, -inset), 4.0 * scale, 4.0 * scale);
    }

    painter->save();
    painter->scale(scale, scale);
    const QRectF body(0.0, 0.0, width() / scale, height() / scale);
    const QRectF text = body.adjusted(12.0, 6.0, -12.0, -6.0);
    QFont titleFont = painter->font();
    titleFont.setBold(true);
    painter->setFont(titleFont);
    painter->setPen(colors.title);
    const QFontMetricsF titleMetrics(titleFont);
    // A further text line must end inside the body.
    const auto fits = [&body](const double top, const double lineHeight) { return top + lineHeight <= body.bottom() - 2.0; };
    painter->drawText(QRectF(text.left(), text.top(), text.width(), titleMetrics.height()), Qt::AlignLeft | Qt::AlignVCenter,
                      titleMetrics.elidedText(title(), Qt::ElideRight, text.width()));
    double next = text.top() + titleMetrics.height();

    QFont typeFont = painter->font();
    typeFont.setBold(false);
    typeFont.setPointSizeF(typeFont.pointSizeF() * 0.9);
    painter->setFont(typeFont);
    painter->setPen(colors.type);
    const QFontMetricsF typeMetrics(typeFont);
    if (fits(next, typeMetrics.height())) {
        painter->drawText(QRectF(text.left(), next, text.width(), typeMetrics.height()), Qt::AlignLeft | Qt::AlignVCenter,
                          typeMetrics.elidedText(QString::fromStdString(element_.type), Qt::ElideRight, text.width()));
        next += typeMetrics.height();
    }
    const auto* descriptor = s7::ElementRegistry::instance().find(element_.type);
    if (descriptor && descriptor->metadata().stateless) {
        QFont markerFont = typeFont;
        markerFont.setPointSizeF(markerFont.pointSizeF() * 0.85);
        painter->setFont(markerFont);
        painter->setPen(colors.title);
        // At the bottom, clear of the resize handle, below the other lines and
        // whole, or not at all.
        const QRectF footer(text.left(), body.bottom() - 26.0, body.width() - 36.0, 17.0);
        const QString marker = QStringLiteral("stateless");
        if (footer.top() >= next && QFontMetricsF(markerFont).horizontalAdvance(marker) <= footer.width())
            painter->drawText(footer, Qt::AlignLeft | Qt::AlignVCenter, marker);
    }
    painter->restore();
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
        {"text_format", "plain"},
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
    QFont font(QString::fromStdString(json_utils::stringValue(params_, "font_family", "Sans Serif")));
    font.setPointSize(json_utils::intValue(params_, "font_size", kDefaultFontPoints));
    textDocument_.setDefaultFont(font);
    textDocument_.setDocumentMargin(0);
    textDocument_.setTextWidth(width() - 20);
    const auto text = QString::fromStdString(json_utils::stringValue(params_, "text", ""));
    if (json_utils::stringValue(params_, "text_format", "plain") == "markdown") {
        textDocument_.setMarkdown(text);
    } else {
        textDocument_.setPlainText(text);
        // Plain notes retain their line breaks and mathematical punctuation.
        // Bare web references can still be activated without Markdown syntax.
        static const QRegularExpression urls(QStringLiteral(R"(https?://[^\s<>"\x{2028}\x{2029}]+)"));
        auto matches = urls.globalMatch(text);
        while (matches.hasNext()) {
            const auto match = matches.next();
            auto url = match.captured();
            while (url.endsWith('.') || url.endsWith(',') || url.endsWith(';')) url.chop(1);
            QTextCursor cursor(&textDocument_);
            cursor.setPosition(static_cast<int>(match.capturedStart()));
            cursor.setPosition(static_cast<int>(match.capturedStart()+url.size()), QTextCursor::KeepAnchor);
            QTextCharFormat format;
            format.setAnchor(true); format.setAnchorHref(url);
            format.setForeground(QColor("#1565c0")); format.setFontUnderline(true);
            cursor.mergeCharFormat(format);
        }
    }
    update();
}

auto TextFrameNode::linkAt(const QPointF localPosition) const -> QUrl
{
    if (!sizeRect().adjusted(10, 8, -10, -8).contains(localPosition)) return {};
    return QUrl(textDocument_.documentLayout()->anchorAt(localPosition-QPointF(10, 8)));
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

    painter->save();
    painter->setClipRect(sizeRect().adjusted(10, 8, -10, -8));
    painter->translate(10, 8);
    QAbstractTextDocumentLayout::PaintContext context;
    context.palette.setColor(QPalette::Text, color("text_color", "#202020"));
    textDocument_.documentLayout()->draw(painter, context);
    painter->restore();
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
