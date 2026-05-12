#include "basic_node_item.hpp"

#include <boost/json.hpp>
#include <qschematic/items/connector.hpp>
#include <qschematic/items/label.hpp>

#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGraphicsSceneMouseEvent>
#include <QLineEdit>
#include <QMessageBox>
#include <QPainter>
#include <QPainterPath>
#include <QPlainTextEdit>
#include <QVBoxLayout>

#include <algorithm>

namespace s7ui::elements {

BasicNodeItem::BasicNodeItem(const QString& typeName, QGraphicsItem* parent)
    : QSchematic::Items::Node(QSchematic::Items::Item::NodeType, parent), typeName_(typeName), nodeName_(typeName)
{
    setSize(50, 30);
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

    clearConnectors();

    for (int i = 0; i < inputPorts_; ++i)
    {
        const int y = 2 + i * 2;
        auto in = std::make_shared<QSchematic::Items::Connector>(
            QSchematic::Items::Item::ConnectorType,
            QPoint(0, y),
            QString("in%1").arg(i));
        in->label()->setVisible(true);
        addConnector(in);
    }

    for (int i = 0; i < outputPorts_; ++i)
    {
        const int y = 2 + i * 2;
        auto out = std::make_shared<QSchematic::Items::Connector>(
            QSchematic::Items::Item::ConnectorType,
            QPoint(8, y),
            QString("out%1").arg(i));
        out->label()->setVisible(true);
        addConnector(out);
    }

    update();
}

void BasicNodeItem::setVisualStyle(const QString& shape, const QColor& baseColor)
{
    shape_ = shape.trimmed().isEmpty() ? QStringLiteral("rect") : shape.trimmed().toLower();
    baseColor_ = baseColor;
    if (!baseColor_.isValid()) {
        baseColor_ = QColor(79, 79, 79, 120);
    }
    refreshToolTip();
    update();
}

void BasicNodeItem::paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget)
{
    QSchematic::Items::Node::paint(painter, option, widget);

    painter->save();
    painter->setPen(QColor(30, 30, 30));

    const QRectF r = sizeRect();
    const QString idText = nodeId_.isEmpty() ? QStringLiteral("(no id)") : nodeId_;

    // Draw a visual-shape hint behind text; unknown styles implicitly fall back to rectangle.
    painter->setBrush(baseColor_);
    painter->setPen(QPen(baseColor_.darker(170), 1.4));
    if (shape_ == "round_rect") {
        painter->drawRoundedRect(r.adjusted(6, 6, -6, -6), 10.0, 10.0);
    } else if (shape_ == "hex") {
        QPolygonF poly;
        const auto midY = r.center().y();
        poly << QPointF(r.left() + 16, r.top() + 8)
             << QPointF(r.right() - 16, r.top() + 8)
             << QPointF(r.right() - 4, midY)
             << QPointF(r.right() - 16, r.bottom() - 8)
             << QPointF(r.left() + 16, r.bottom() - 8)
             << QPointF(r.left() + 4, midY);
        painter->drawPolygon(poly);
    } else if (shape_ == "diamond") {
        QPolygonF poly;
        poly << QPointF(r.center().x(), r.top() + 6)
             << QPointF(r.right() - 6, r.center().y())
             << QPointF(r.center().x(), r.bottom() - 6)
             << QPointF(r.left() + 6, r.center().y());
        painter->drawPolygon(poly);
    } else {
        painter->drawRect(r.adjusted(6, 6, -6, -6));
    }

    painter->setPen(QColor(30, 30, 30));

    QFont typeFont = painter->font();
    typeFont.setBold(true);
    typeFont.setPointSizeF(typeFont.pointSizeF() + 0.5);
    painter->setFont(typeFont);
    painter->drawText(r.adjusted(8, 6, -8, -8), Qt::AlignTop | Qt::AlignLeft, typeName_);

    QFont bodyFont = painter->font();
    bodyFont.setBold(false);
    bodyFont.setPointSizeF(bodyFont.pointSizeF() - 0.5);
    painter->setFont(bodyFont);
    painter->drawText(r.adjusted(8, 24, -8, -8), Qt::AlignTop | Qt::AlignLeft, QString("name: %1").arg(nodeName_));
    painter->drawText(r.adjusted(8, 40, -8, -8), Qt::AlignTop | Qt::AlignLeft, QString("id: %1").arg(idText));

    painter->restore();
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
    QDialog dialog;
    dialog.setWindowTitle(typeName_ + " parameters");
    dialog.resize(560, 420);

    auto* root = new QVBoxLayout(&dialog);
    auto* form = new QFormLayout();

    auto* nameEdit = new QLineEdit(nodeName_, &dialog);
    auto* idEdit = new QLineEdit(nodeId_, &dialog);
    auto* typeEdit = new QLineEdit(typeName_, &dialog);
    typeEdit->setReadOnly(true);

    auto* paramsEdit = new QPlainTextEdit(&dialog);
    paramsEdit->setPlainText(paramsJson_);

    form->addRow("Name", nameEdit);
    form->addRow("ID", idEdit);
    form->addRow("Type", typeEdit);
    root->addLayout(form);
    root->addWidget(paramsEdit, 1);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    root->addWidget(buttons);

    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, [&dialog, paramsEdit] {
        try
        {
            const auto parsed = boost::json::parse(paramsEdit->toPlainText().toStdString());
            if (!parsed.is_object())
            {
                throw std::runtime_error("Parameters JSON must be an object");
            }
            dialog.accept();
        }
        catch (const std::exception& ex)
        {
            QMessageBox::warning(&dialog, "Invalid Parameters", QString::fromUtf8(ex.what()));
        }
    });
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    if (dialog.exec() != QDialog::Accepted)
    {
        return;
    }

    setNodeName(nameEdit->text());
    setNodeId(idEdit->text());
    setParamsJson(paramsEdit->toPlainText());
}

void BasicNodeItem::refreshToolTip()
{
    setToolTip(QString("%1\nname: %2\nid: %3\nshape: %4")
        .arg(typeName_)
        .arg(nodeName_)
        .arg(nodeId_.isEmpty() ? QStringLiteral("<unset>") : nodeId_)
        .arg(shape_));
}

} // namespace s7ui::elements
