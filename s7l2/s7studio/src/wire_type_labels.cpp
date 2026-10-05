#include "wire_type_labels.hpp"

#include "scene_serializer.hpp"
#include "local_runtime_v2.hpp"
#include "runtime_schema_extractor.hpp"

#include <qschematic/items/label.hpp>
#include <qschematic/items/wire.hpp>
#include <qschematic/scene.hpp>

#include <QBrush>
#include <QFont>
#include <QGraphicsSimpleTextItem>

#include <unordered_map>
#include <unordered_set>

namespace s7studio::diagram {

namespace {

// Each wire gets its own label (a child item, so it moves with the wire and is
// not part of the saved diagram). QSchematic nets are not used: wires whose
// paths touch share a net although they carry different signals.
constexpr int kWireTypeLabelRole = 0x5737; // QGraphicsItem::data key marking the label

auto findWireLabel(QSchematic::Items::Wire& wire) -> QGraphicsSimpleTextItem*
{
    for (auto* child : wire.childItems()) {
        if (child->data(kWireTypeLabelRole).toBool()) {
            return static_cast<QGraphicsSimpleTextItem*>(child);
        }
    }
    return nullptr;
}

void setWireLabel(QSchematic::Items::Wire& wire, const QString& type)
{
    auto* label = findWireLabel(wire);
    if (!label) {
        label = new QGraphicsSimpleTextItem(&wire);
        label->setData(kWireTypeLabelRole, true);
        QFont font = label->font();
        font.setPointSizeF(font.pointSizeF() * 0.85);
        label->setFont(font);
    }
    if (label->text() != type) {
        label->setText(type);
    }
    label->setBrush(type == QStringLiteral("?") ? QBrush(Qt::red) : QBrush(QColor(0x30, 0x30, 0x90)));
    // Above the middle of the first segment, in the wire's coordinates.
    if (const auto points = wire.pointsRelative(); points.size() >= 2) {
        const auto middle = (points[0] + points[1]) / 2.0;
        const QPointF position = middle + QPointF(-label->boundingRect().width() / 2.0, -label->boundingRect().height() - 2.0);
        if (label->pos() != position) {
            label->setPos(position);
        }
    }
}

} // namespace

auto updateWireTypeLabels(QSchematic::Scene& scene, QString* error) -> std::vector<WireTypeLabel>
{
    std::vector<WireSource> sources;
    const auto schemaJson = RuntimeSchemaExtractor{}.extract(scene, SerializationOptions{}, &sources);

    // element id -> output port types, from the resolved elements
    std::unordered_map<std::string, std::vector<s7::SignalType>> outputTypes;
    QString firstError;
    try {
        const auto schema = runtime::buildSchema(schemaJson);
        auto resolved = s7::TypeResolver::resolve(schema);
        if (!resolved.ok()) {
            firstError = QString::fromStdString(resolved.errors.front());
        } else {
            for (std::size_t index = 0; index < schema.elements().size(); ++index) {
                const auto element = resolved.factories[index]();
                auto& types = outputTypes[schema.elements()[index].id];
                for (const auto& port : element->output_ports()) {
                    types.push_back(port.type);
                }
            }
        }
    } catch (const std::exception& ex) {
        firstError = QString::fromUtf8(ex.what());
    }
    if (error) {
        *error = firstError;
    }

    std::vector<WireTypeLabel> labels;
    std::unordered_set<const QSchematic::Items::Wire*> labelled;
    for (const auto& source : sources) {
        QString type = QStringLiteral("?");
        if (const auto it = outputTypes.find(source.element);
            it != outputTypes.end() && source.port >= 0 && static_cast<std::size_t>(source.port) < it->second.size()) {
            type = QString::fromUtf8(s7::signal_type_name(it->second[static_cast<std::size_t>(source.port)]));
        }
        if (source.wire) {
            setWireLabel(*source.wire, type);
            labelled.insert(source.wire);
        }
        labels.push_back(WireTypeLabel{QString::fromStdString(source.element), source.port, type});
    }
    // Wires that no longer connect an output to an input lose their label.
    for (const auto& wire : scene.items<QSchematic::Items::Wire>()) {
        if (wire && !labelled.contains(wire.get())) {
            delete findWireLabel(*wire);
        }
    }
    return labels;
}

} // namespace s7studio::diagram
