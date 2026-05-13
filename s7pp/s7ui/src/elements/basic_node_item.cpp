#include "basic_node_item.hpp"
#include "s7ui_json_dto.hpp"

#include <qschematic/items/connector.hpp>
#include <qschematic/items/label.hpp>
#include "dialogs/properties_dialog.hpp"

#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGraphicsSceneMouseEvent>
#include <QFontMetricsF>
#include <QLineEdit>
#include <QMessageBox>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QBrush>
#include <QPlainTextEdit>
#include <QVBoxLayout>
#include <QGraphicsDropShadowEffect>
#include <QStyleOptionGraphicsItem>

#include <cmath>
#include <algorithm>

namespace s7ui::elements {

namespace
{
constexpr qreal kNodeMinWidthMm = 10.0;
constexpr qreal kNodeMinHeightMm = 5.0;
constexpr qreal kPortPitchMm = 2.0;
constexpr qreal kPortMarginMm = 1.0;

auto connectorOffsetMm(const int index, const int count, const qreal nodeHeightMm) -> qreal
{
    if (count <= 1)
    {
        return nodeHeightMm * 0.5;
    }

    const qreal span = std::max<qreal>(0.0, nodeHeightMm - 2.0 * kPortMarginMm);
    const qreal step = span / static_cast<qreal>(count - 1);
    qreal offset = kPortMarginMm + static_cast<qreal>(index) * step;

    return offset;
}
}

BasicNodeItem::BasicNodeItem(const QString& typeName, QGraphicsItem* parent)
    : QSchematic::Items::Node(QSchematic::Items::Item::NodeType, parent), typeName_(typeName), nodeName_(typeName)
{
    setMovable(true);
    setFlag(QGraphicsItem::ItemIsMovable, true);
    setFlag(QGraphicsItem::ItemIsSelectable, true);

    // QSchematic defaults for behavior/dragging
    setAllowMouseResize(false); // Can be enabled if needed, but false is safer for auto layout
    setAllowMouseRotate(false);
    setConnectorsMovable(false);
    setConnectorsSnapPolicy(QSchematic::Items::Connector::NodeSizerectOutline);
    setConnectorsSnapToGrid(true);

    auto graphicsEffect = new QGraphicsDropShadowEffect(this);
    graphicsEffect->setOffset(5);
    graphicsEffect->setBlurRadius(10);
    graphicsEffect->setColor(QColor(63, 63, 63, 100));
    setGraphicsEffect(graphicsEffect);

    setPortLayout(1, 1);
    refreshToolTip();
}

void BasicNodeItem::setNodeId(const QString& nodeId)
{
    nodeId_ = nodeId;
    refreshToolTip();
    update();
}

QString BasicNodeItem::nodeId() const
{
    return nodeId_;
}

void BasicNodeItem::setNodeName(const QString& nodeName)
{
    nodeName_ = nodeName.trimmed().isEmpty() ? typeName_ : nodeName.trimmed();
    refreshToolTip();
    update();
}

QString BasicNodeItem::nodeName() const
{
    return nodeName_;
}

void BasicNodeItem::setTypeName(const QString& typeName)
{
    typeName_ = typeName;
    if (nodeName_.trimmed().isEmpty())
    {
        nodeName_ = typeName_;
    }
    refreshToolTip();
    update();
}

QString BasicNodeItem::typeName() const
{
    return typeName_;
}

void BasicNodeItem::setParamsJson(const QString& paramsJson)
{
    paramsJson_ = paramsJson.trimmed().isEmpty() ? QStringLiteral("{}") : paramsJson;
}

QString BasicNodeItem::paramsJson() const
{
    return paramsJson_;
}

void BasicNodeItem::setPortLayout(int inputPorts, int outputPorts)
{
    inputPorts_ = std::clamp(inputPorts, 0, 32);
    outputPorts_ = std::clamp(outputPorts, 0, 32);

    const int maxPorts = std::max(1, std::max(inputPorts_, outputPorts_));
    const qreal nodeWidth = kNodeMinWidthMm;
    const qreal nodeHeight = std::max<qreal>(kNodeMinHeightMm,
        2.0 * kPortMarginMm + static_cast<qreal>(maxPorts - 1) * kPortPitchMm);
    setSize(nodeWidth, nodeHeight);

    clearConnectors();

    for (int i = 0; i < inputPorts_; ++i)
    {
        auto in = std::make_shared<QSchematic::Items::Connector>(
            QSchematic::Items::Item::ConnectorType,
            QPoint(),
            QString("in%1").arg(i),
            this);
        in->setPos(0.0, connectorOffsetMm(i, inputPorts_, nodeHeight));
        in->label()->setVisible(false);
        addConnector(in);
    }

    for (int i = 0; i < outputPorts_; ++i)
    {
        auto out = std::make_shared<QSchematic::Items::Connector>(
            QSchematic::Items::Item::ConnectorType,
            QPoint(),
            QString("out%1").arg(i),
            this);
        out->setPos(nodeWidth, connectorOffsetMm(i, outputPorts_, nodeHeight));
        out->label()->setVisible(false);
        addConnector(out);
    }

    update();
}

void BasicNodeItem::setVisualStyle(const QColor& baseColor)
{
    // Fall back to a light gray if not valid (like QSchematic demo)
    if (baseColor.isValid()) {
        baseColor_ = baseColor;
    } else {
        baseColor_ = QColor(QStringLiteral("#e0e0e0"));
    }

    // Ensure color isn't too "screaming"
    if (baseColor_.saturation() > 100) {
        baseColor_.setHsv(baseColor_.hue(), 100, std::max(200, baseColor_.value()));
    }

    refreshToolTip();
    update();
}

void BasicNodeItem::paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget)
{
    Q_UNUSED(option);
    Q_UNUSED(widget);

    if (!painter)
    {
        return;
    }

    painter->save();

    const qreal lod = option->levelOfDetailFromTransform(painter->worldTransform());
    const bool showDetails = (lod >= 0.5);

    painter->setPen(QPen(baseColor_.darker(150), 0.3));
    painter->setBrush(baseColor_);

    const QRectF rect(0, 0, size().width(), size().height());

    // Default electronic component shape
    painter->drawRect(rect);
    if (showDetails) {
        const qreal lineOffset = 2.0;
        painter->setPen(QPen(baseColor_.darker(200), 0.2));
        if (inputPorts_ > 0) {
            painter->drawLine(QPointF(rect.left() + lineOffset, rect.top()),
                              QPointF(rect.left() + lineOffset, rect.bottom()));
        }
        if (outputPorts_ > 0) {
            painter->drawLine(QPointF(rect.right() - lineOffset, rect.top()),
                              QPointF(rect.right() - lineOffset, rect.bottom()));
        }
    }

    if (showDetails) {
        // Name label inside the node
        painter->setPen(Qt::black);
        QFont font = painter->font();
        font.setPointSizeF(3.0);
        painter->setFont(font);

        QTextOption textOption(Qt::AlignCenter);
        textOption.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);

        painter->drawText(rect, nodeName_, textOption);
    }

    painter->restore();
}

void BasicNodeItem::sizeChangedEvent(QSizeF oldSize, QSizeF newSize)
{
    Q_UNUSED(oldSize);
    Q_UNUSED(newSize);

    // Do nothing. This is to override the default implementation in QSchematic::Items::Node
}

void BasicNodeItem::mouseDoubleClickEvent(QGraphicsSceneMouseEvent* event)
{
    openPropertiesDialog();
    if (event)
    {
        event->accept();
    }
}

void BasicNodeItem::openPropertiesDialog()
{
    auto element = s7::ElementRegistry::instance().create(typeName_.toStdString(), nodeId_.toStdString(), {});
    auto propertyProvider = std::dynamic_pointer_cast<s7::IElementPropertyProvider>(element);
    s7::ParamMap params;
    if (propertyProvider) {
        params = propertyProvider->getProperties();
    }

    s7ui::dialogs::PropertiesDialog dialog(typeName_, nodeId_, nodeName_, params, nullptr);
    if (dialog.exec() != QDialog::Accepted)
    {
        return;
    }

    setNodeName(dialog.nodeName());
    setNodeId(dialog.nodeId());

    if (propertyProvider) {
        propertyProvider->setProperties(dialog.params());
        // Note: This is where we would serialize the properties back to JSON.
        // This part is complex and depends on the JSON library and structure.
        // For now, we are just updating the element in memory.
    }
}

void BasicNodeItem::refreshToolTip()
{
    setToolTip(QString("%1\nname: %2\nid: %3")
        .arg(typeName_)
        .arg(nodeName_)
        .arg(nodeId_.isEmpty() ? QStringLiteral("<unset>") : nodeId_));
}

} // namespace s7ui::elements
