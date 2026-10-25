// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-S7-plugin-permission
#include "element_documentation.hpp"
#include "viewers/viewer_registry.hpp"
#include <s7/element_registry.hpp>
#include <QDialog>
#include <QDialogButtonBox>
#include <QPlainTextEdit>
#include <QVBoxLayout>

namespace s7studio::ui {
namespace {
QString text(const std::string& value) { return QString::fromStdString(value); }
QString json(const boost::json::value& value) { return text(boost::json::serialize(value)); }
void ports(QStringList& lines, const char* title, const std::vector<s7::PortSpec>& specs)
{
    lines << QString::fromLatin1(title);
    if (specs.empty()) lines << QStringLiteral("  None.");
    for (const auto& port : specs)
        lines << QStringLiteral("  %1 (%2%3): %4").arg(text(port.name),
            port.type_rule.empty() ? QString::fromLatin1(s7::signal_type_name(port.type)) : text(port.type_rule),
            port.optional ? QStringLiteral(", optional") : QString(),
            port.description.empty() ? QStringLiteral("Port documentation unavailable.") : text(port.description));
}
void useSchema(s7::ElementMetadata& metadata, const boost::json::value& schema)
{
    metadata.schema_definition = schema;
    metadata.input_ports.clear(); metadata.output_ports.clear();
    for (const auto* key : {"exposed_inputs", "exposed_outputs"}) {
        const auto* values = schema.as_object().if_contains(key);
        if (!values || !values->is_array()) continue;
        for (const auto& value : values->as_array()) if (value.is_object()) {
            s7::PortSpec port;
            const auto& object = value.as_object();
            if (const auto* name = object.if_contains("name"); name && name->is_string()) port.name = name->as_string();
            port.description = "Exposed schema signal";
            if (const auto* signalType = object.if_contains("type"); signalType && signalType->is_string()) {
                const auto name = std::string(signalType->as_string());
                try { port.type = s7::parse_signal_type(name); }
                catch (const std::exception&) { port.type_rule = "Unknown signal type: " + name; }
            }
            (std::string_view(key) == "exposed_inputs" ? metadata.input_ports : metadata.output_ports).push_back(port);
        }
    }
}
}

QString elementDocumentation(const s7::ElementMetadata& metadata)
{
    QStringList lines{text(metadata.display_name.empty() ? metadata.type_name : metadata.display_name),
        QStringLiteral("Type: %1").arg(text(metadata.type_name)), {},
        metadata.description.empty() ? QStringLiteral("Documentation unavailable for this element.") : text(metadata.description), {}};
    ports(lines, "Inputs", metadata.input_ports);
    if (metadata.variadic_inputs.enabled) {
        const auto& inputs = metadata.variadic_inputs;
        lines << QStringLiteral("  Variable input count: at least %1; declared maximum %2. Count follows '%3' (array length or integer). Port template: %4.")
            .arg(inputs.min_count).arg(inputs.max_count).arg(text(inputs.count_parameter), text(inputs.port_name_template));
    }
    ports(lines, "Outputs", metadata.output_ports);
    if (!metadata.type_variables.empty()) {
        lines << "" << "Signal types";
        for (const auto& variable : metadata.type_variables) {
            QStringList allowed;
            for (const auto type : variable.allowed) allowed << QString::fromLatin1(s7::signal_type_name(type));
            lines << QStringLiteral("  %1: %2. Pin with '%3'; %4.").arg(text(variable.name), allowed.join(", "),
                text(variable.pin_parameter), variable.defaults_to.empty() ? QStringLiteral("otherwise inferred from connections")
                    : QStringLiteral("otherwise defaults to %1").arg(text(variable.defaults_to)));
        }
        lines << "  Values use JSON numbers (or decimal strings where exact typed values are supported); complex values use [real, imaginary]. Ports require matching signal types; use Convert for explicit conversion.";
    }
    lines << "" << "JSON parameters";
    if (metadata.parameters.empty()) lines << "  No configurable JSON parameters are declared by this element.";
    for (const auto& parameter : metadata.parameters) {
        const auto presence = parameter.required ? QStringLiteral("required; must be supplied%1").arg(
            parameter.default_value.is_null() ? QString() : QStringLiteral("; suggested value %1").arg(json(parameter.default_value)))
            : parameter.default_value.is_null() ? QStringLiteral("optional; omitted unless set")
            : QStringLiteral("optional; default %1").arg(json(parameter.default_value));
        lines << QStringLiteral("%1 — %2; %3").arg(text(parameter.name), text(parameter.type), presence);
        lines << (parameter.description.empty() ? QStringLiteral("  Parameter documentation unavailable.") : "  " + text(parameter.description));
        if (parameter.min_value) lines << QStringLiteral("  Minimum: %1").arg(*parameter.min_value, 0, 'g', 16);
        if (parameter.max_value) lines << QStringLiteral("  Maximum: %1").arg(*parameter.max_value, 0, 'g', 16);
        if (!parameter.enum_values.empty()) {
            QStringList choices;
            for (const auto& choice : parameter.enum_values) choices << json(boost::json::value(choice));
            lines << "  Choices: " + choices.join(", ");
        }
    }
    if (metadata.schema_definition) {
        lines << "" << "Schema configuration"
              << "This is an explicit nested network. Exposed inputs (including controller gains and time constants) are signals, not wrapper JSON parameters. Double-click opens the nested editor. Tune internal elements there; their own information describes accepted parameters.";
        if (metadata.schema_definition->is_object()) {
            const auto& schema = metadata.schema_definition->as_object();
            if (const auto* dt = schema.if_contains("dt"))
                lines << "Default schema dt (seconds): " + json(*dt) + ". Run Configuration sets the model discretization.";
            if (const auto* elements = schema.if_contains("elements"); elements && elements->is_array()) {
                lines << "Internal element settings (unspecified keys use that element's defaults):";
                for (const auto& value : elements->as_array()) if (value.is_object()) {
                    const auto& element = value.as_object();
                    const auto* id = element.if_contains("id");
                    const auto* type = element.if_contains("type");
                    const auto* params = element.if_contains("params");
                    if (id && type) lines << "  " + json(*id) + " (" + json(*type) + "): " + (params ? json(*params) : "{}");
                }
            }
        }
    }
    return lines.join('\n');
}

QString elementDocumentation(const s7::ElementRegistry& registry, const QString& type,
                             const boost::json::value& nestedSchema)
{
    if (const auto* viewer = viewers::ViewerRegistry::instance().find(type)) {
        if (viewer->documentation) return elementDocumentation(*viewer->documentation);
        return viewer->displayName + "\n\n" + viewer->description + "\n\nParameter documentation unavailable for this observer.";
    }
    if (const auto* descriptor = registry.find(type.toStdString())) {
        auto metadata = descriptor->metadata();
        if (nestedSchema.is_object()) useSchema(metadata, nestedSchema);
        return elementDocumentation(metadata);
    }
    if (nestedSchema.is_object()) {
        s7::ElementMetadata metadata;
        metadata.type_name = type.toStdString();
        useSchema(metadata, nestedSchema);
        return elementDocumentation(metadata);
    }
    return type + "\n\nDocumentation unavailable. The element's library or plugin may not be loaded.";
}

QPlainTextEdit* createElementDocumentation(const QString& text, QWidget* parent)
{
    auto* view = new QPlainTextEdit(parent);
    view->setObjectName(QStringLiteral("element.documentation"));
    view->setAccessibleName(QStringLiteral("Element documentation"));
    view->setReadOnly(true);
    view->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
    view->setTabChangesFocus(true);
    view->setPlainText(text);
    view->setMinimumSize(160, 70);
    return view;
}

void showElementInformation(const s7::ElementRegistry& registry, const QString& type,
                            QWidget* parent, const boost::json::value& nestedSchema)
{
    QDialog dialog(parent);
    dialog.setObjectName(QStringLiteral("dialog.elementInformation"));
    dialog.setWindowTitle(QStringLiteral("Element Information — %1").arg(type));
    dialog.resize(680, 560);
    auto* layout = new QVBoxLayout(&dialog);
    layout->addWidget(createElementDocumentation(elementDocumentation(registry, type, nestedSchema), &dialog), 1);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);
    dialog.exec();
}
}
