// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-S7-plugin-permission
#include "library_element.hpp"
#include "diagram_editor.hpp"
#include "diagram/diagram_scene.hpp"
#include "diagram/element_ports.hpp"
#include "viewers/viewer_registry.hpp"
#include <s7/element_registry.hpp>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMimeData>
#include <algorithm>
#include <cmath>

namespace {
const QString observerPrefix = QStringLiteral("StudioObserver:");
}
namespace s7studio::ui {
bool libraryElementAvailable(const QString& identifier)
{
    if (identifier.isEmpty()) return false;
    if (identifier.startsWith(observerPrefix))
        return viewers::ViewerRegistry::instance().find(identifier.mid(observerPrefix.size())) != nullptr;
    return s7::ElementRegistry::instance().find(identifier.toStdString()) != nullptr;
}

QMimeData* libraryElementMimeData(const QString& identifier)
{
    if (!libraryElementAvailable(identifier)) return nullptr;
    const auto payload = QJsonDocument(QJsonObject{{"version", 1}, {"type", identifier}}).toJson(QJsonDocument::Compact);
    if (payload.size() > maximumLibraryElementPayload) return nullptr;
    auto* data = new QMimeData;
    data->setData(libraryElementMimeType, payload);
    return data;
}

QString libraryElementFromMimeData(const QMimeData* data)
{
    if (!data || !data->hasFormat(libraryElementMimeType)) return {};
    const auto payload = data->data(libraryElementMimeType);
    if (payload.isEmpty() || payload.size() > maximumLibraryElementPayload) return {};
    const auto document = QJsonDocument::fromJson(payload);
    if (!document.isObject()) return {};
    const auto object = document.object();
    if (object.size() != 2 || !object["version"].isDouble() || object["version"].toDouble() != 1 || !object["type"].isString()) return {};
    const auto identifier = object["type"].toString();
    return libraryElementAvailable(identifier) ? identifier : QString();
}
}

namespace s7studio::diagram {
bool DiagramEditor::addLibraryElement(const QString& identifier, const std::optional<QPointF> scenePosition)
{
    if (!ui::libraryElementAvailable(identifier)) return false;
    if (scenePosition && (!std::isfinite(scenePosition->x()) || !std::isfinite(scenePosition->y()))) return false;
    const bool observer = identifier.startsWith(observerPrefix);
    const auto type = observer ? identifier.mid(observerPrefix.size()) : identifier;
    const auto* descriptor = observer ? nullptr : s7::ElementRegistry::instance().find(type.toStdString());
    ElementNode::Element element;
    const auto prefix = type.toLower().toStdString() + "_";
    const auto used = [&](const std::string& id) {
        if (scene()->elementNode(id)) return true;
        for (const auto& frame : scene()->textFrames()) if (frame->id() == id) return true;
        return false;
    };
    std::size_t suffix = 1;
    do { element.id = prefix + std::to_string(suffix++); } while (used(element.id));
    element.type = type.toStdString();
    // Ordinary parameter defaults are supplied by the existing metadata path
    // when configuring/compiling. Do not create runtime/plugin instances here.
    if (observer && (type == "Scope" || type == "LiveOscilloscope")) element.params["channel_count"] = 0;
    if (descriptor && descriptor->metadata().schema_definition) element.schema = *descriptor->metadata().schema_definition;
    const auto ports = elementPorts(element.type, element.params, element.schema);
    const auto rows = static_cast<int>(std::max(ports.inputs.size(), ports.outputs.size()));
    const QSizeF size(kElementWidthSteps * kGridStep, std::max(5, 2 * rows + 1) * kGridStep);
    const auto node = scene()->addElement(element, scenePosition ? *scenePosition : suggestedNewItemPosition(size));
    scene()->setMode(QSchematic::Scene::NormalMode);
    scene()->clearSelection();
    node->setSelected(true);
    emit libraryElementAdded(QString::fromStdString(element.id));
    return true;
}
}
