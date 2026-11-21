// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-S7-plugin-permission

#include "parameter_editor.hpp"
#include "instrument_controls.hpp"
#include <s7/element_registry.hpp>
#include <QCheckBox>
#include <QComboBox>
#include <QDial>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QTabWidget>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <utility>

namespace s7studio::ui {
namespace {
bool parseObject(const QString& text, boost::json::object& result, QString& error) {
    try {
        const auto parsed = boost::json::parse(text.toStdString());
        if (!parsed.is_object()) throw std::invalid_argument("Parameters must be a JSON object.");
        result = parsed.as_object(); return true;
    } catch (const std::exception& e) { error = QString::fromUtf8(e.what()); return false; }
}

QDial* tuningDial(QLineEdit* input, QCheckBox* enabled, const s7::ParameterSpec& spec, QWidget* parent)
{
    auto* dial = createInstrumentDial(parent);
    const auto name = QString::fromStdString(spec.name);
    dial->setObjectName("control.dial." + name);
    dial->setAccessibleName(QStringLiteral("Adjust %1").arg(name));
    dial->setToolTip(QStringLiteral("Turn to adjust %1. The field accepts exact values.").arg(name));
    dial->setFixedSize(58, 58);
    constexpr int positions = 200;
    dial->setWrapping(true);
    dial->setRange(0, positions - 1);
    dial->setValue(positions / 2);
    // A relative dial also works for parameters without a finite range. The
    // text field stays authoritative, including values the dial cannot tune.
    const auto readNumber = [input, enabled]() -> std::optional<double> {
        if (enabled && !enabled->isChecked()) return std::nullopt;
        boost::system::error_code error;
        const auto value = boost::json::parse(input->text().toStdString(), error);
        if (error || !value.is_number()) return std::nullopt;
        constexpr std::int64_t exactLimit = std::int64_t{1} << 53;
        if (value.is_int64() && (value.as_int64() > exactLimit || value.as_int64() < -exactLimit)) return std::nullopt;
        if (value.is_uint64() && value.as_uint64() > std::uint64_t(exactLimit)) return std::nullopt;
        const double result = value.to_number<double>();
        return std::isfinite(result) ? std::optional{result} : std::nullopt;
    };
    const auto refresh = [dial, readNumber] { dial->setEnabled(readNumber().has_value()); };
    QObject::connect(input, &QLineEdit::textChanged, dial, refresh);
    if (enabled) QObject::connect(enabled, &QCheckBox::toggled, dial, refresh);
    const double typicalValue = spec.default_value.is_number() ? std::abs(spec.default_value.to_number<double>()) : 0.0;
    const double minimumScale = typicalValue > 0 ? typicalValue / 10 : 0.1;
    QObject::connect(dial, &QDial::valueChanged, input,
        [input, readNumber, minimumScale, minimum = spec.min_value, maximum = spec.max_value, previous = positions / 2](int position) mutable {
            int delta = position - std::exchange(previous, position);
            if (delta > positions / 2) delta -= positions;
            if (delta < -positions / 2) delta += positions;
            const auto current = readNumber();
            if (!current) return;
            const double step = minimum && maximum ? (*maximum / 100 - *minimum / 100)
                : std::max(std::abs(*current), minimumScale) / 100;
            double next = *current + delta * step;
            if (minimum) next = std::max(next, *minimum);
            if (maximum) next = std::min(next, *maximum);
            if (std::isfinite(next) && next != *current) input->setText(QString::fromStdString(boost::json::serialize(next)));
        });
    dial->setEnabled(false);
    return dial;
}
}
ParameterEditor::ParameterEditor(std::optional<s7::ElementMetadata> metadata, boost::json::object params, QWidget* parent)
    : QWidget(parent), metadata_(std::move(metadata)), original_(std::move(params))
{
    auto* layout = new QVBoxLayout(this); layout->setContentsMargins(0, 0, 0, 0);
    tabs_ = new QTabWidget(this); tabs_->setObjectName("element.parameterTabs");
    if (metadata_) for (const auto& spec : metadata_->parameters)
        if (!spec.legacy && !original_.contains(spec.name) && !spec.default_value.is_null()) original_[spec.name] = spec.default_value;
    tabs_->addTab(buildForm(fields_, false), QStringLiteral("&Parameters"));
    tabs_->addTab(buildForm(controls_, true), QStringLiteral("&Controls"));
    json_ = new QPlainTextEdit(QString::fromStdString(boost::json::serialize(original_)), tabs_);
    json_->setObjectName("element.parameters"); json_->setAccessibleName("Advanced parameter JSON");
    json_->setToolTip("Exact JSON values, including arrays, complex values and 64-bit integers. Known parameters are validated before applying.");
    tabs_->addTab(json_, QStringLiteral("Advanced &JSON"));
    tabs_->setTabEnabled(Parameters, metadata_.has_value());
    tabs_->setTabEnabled(Controls, !controls_.empty());
    error_ = new QLabel(this); error_->setObjectName("element.parameterError"); error_->setWordWrap(true);
    error_->setTextFormat(Qt::PlainText); error_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(tabs_, 1); layout->addWidget(error_);
    loadForm(fields_, original_);
    auto checked = original_;
    QString initialError;
    if (!metadata_ || !validate(checked, initialError)) {
        tabs_->setCurrentIndex(AdvancedJson);
        previousTab_ = AdvancedJson;
        showError(initialError);
    }
    connect(tabs_, &QTabWidget::currentChanged, this, [this](int index) {
        boost::json::object params; QString error;
        if (!readTab(previousTab_, params, error) || (index != AdvancedJson && !validate(params, error))) {
            const QSignalBlocker guard(tabs_);
            tabs_->setCurrentIndex(previousTab_);
            showError(error);
            return;
        }
        if (index == AdvancedJson) json_->setPlainText(QString::fromStdString(boost::json::serialize(params)));
        else {
            original_ = params;
            loadForm(index == Controls ? controls_ : fields_, params);
        }
        previousTab_ = index;
        showError({});
    });
}

QWidget* ParameterEditor::buildForm(std::vector<Field>& fields, bool controls)
{
    auto* scroll = new QScrollArea(tabs_); scroll->setWidgetResizable(true);
    auto* formWidget = new QWidget(scroll);
    auto* grid = controls ? new QGridLayout(formWidget) : nullptr;
    auto* form = controls ? nullptr : new QFormLayout(formWidget);
    if (grid) {
        grid->setColumnStretch(0, 1);
        grid->setColumnStretch(1, 1);
    } else {
        form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
        form->setRowWrapPolicy(QFormLayout::WrapLongRows);
    }
    if (metadata_) for (const auto& spec : metadata_->parameters) {
        // Legacy fields stay in the parameters (and the JSON tab) unedited.
        if (spec.legacy) continue;
        const auto name = QString::fromStdString(spec.name);
        QWidget* editor;
        if (spec.type == "enum" || spec.type == "bool") {
            auto* combo = new QComboBox(formWidget);
            if (spec.type == "bool") combo->addItems({"false", "true"});
            else for (const auto& value : spec.enum_values) combo->addItem(QString::fromStdString(value));
            // Keep malformed/imported enum values visible for correction.
            if (spec.enum_values.empty() && spec.type == "enum") combo->setEditable(true);
            editor = combo;
        } else {
            auto* line = new QLineEdit(formWidget);
            line->setPlaceholderText(spec.type == "string" || spec.type == "type" ? QStringLiteral("text") : QStringLiteral("JSON value, e.g. 0.001 or [1, 2]"));
            editor = line;
        }
        const QString prefix = controls ? QStringLiteral("control.") : QStringLiteral("parameter.");
        editor->setObjectName(prefix + name); editor->setAccessibleName(name);
        QString help = QString::fromStdString(spec.description) + "\nType: " + QString::fromStdString(spec.type);
        if (spec.min_value) help += QStringLiteral("; minimum %1").arg(*spec.min_value);
        if (spec.max_value) help += QStringLiteral("; maximum %1").arg(*spec.max_value);
        editor->setToolTip(help); editor->setAccessibleDescription(help);
        auto title = name + (spec.required ? " *" : "");
        QWidget* field;
        if (grid) {
            title.replace('_', ' ');
            if (!title.isEmpty()) title[0] = title[0].toUpper();
            field = new QGroupBox(title, formWidget);
            field->setToolTip(help);
        } else field = new QWidget(formWidget);
        auto* column = new QVBoxLayout(field);
        if (form) column->setContentsMargins(0, 0, 0, 0);
        auto* row = new QHBoxLayout;
        QCheckBox* enabled = nullptr;
        if (!spec.required && spec.default_value.is_null()) {
            enabled = new QCheckBox(QStringLiteral("Set"), field); enabled->setObjectName(prefix + "set." + name);
            enabled->setAccessibleName(QStringLiteral("Set optional %1").arg(name));
            connect(enabled, &QCheckBox::toggled, editor, &QWidget::setEnabled); row->addWidget(enabled);
        }
        if (controls && (spec.type == "real" || spec.type == "value"))
            row->addWidget(tuningDial(qobject_cast<QLineEdit*>(editor), enabled, spec, field));
        row->addWidget(editor, 1); column->addLayout(row);
        if (grid) {
            const int index = static_cast<int>(fields.size());
            grid->addWidget(field, index / 2, index % 2);
        } else {
            auto* description = new QLabel(QString::fromStdString(spec.description), field); description->setWordWrap(true);
            description->setTextFormat(Qt::PlainText); description->setTextInteractionFlags(Qt::TextSelectableByMouse);
            column->addWidget(description);
            auto* label = new QLabel(title, formWidget); label->setBuddy(editor);
            form->addRow(label, field);
        }
        fields.push_back({spec, editor, enabled});
    }
    if (grid) grid->setRowStretch((static_cast<int>(fields.size()) + 1) / 2, 1);
    scroll->setWidget(formWidget);
    return scroll;
}
void ParameterEditor::loadForm(const std::vector<Field>& fields, const boost::json::object& params) {
    for (const auto& field : fields) {
        const auto* value = params.if_contains(field.spec.name);
        const bool present = value && !value->is_null();
        if (field.enabled) { field.enabled->setChecked(present); field.editor->setEnabled(present); }
        const auto text = !present ? QString() : (field.spec.type == "string" || field.spec.type == "type" || field.spec.type == "enum") && value->is_string()
            ? QString::fromStdString(std::string(value->as_string())) : QString::fromStdString(boost::json::serialize(*value));
        if (auto* line = qobject_cast<QLineEdit*>(field.editor)) line->setText(text);
        else if (auto* combo = qobject_cast<QComboBox*>(field.editor)) {
            if (combo->findText(text) < 0) combo->addItem(text);
            combo->setCurrentText(text);
        }
    }
}
bool ParameterEditor::readTab(int index, boost::json::object& result, QString& error) const {
    if (index == AdvancedJson) return parseObject(json_->toPlainText(), result, error);
    result = original_;
    for (const auto& field : index == Controls ? controls_ : fields_) {
        if (field.enabled && !field.enabled->isChecked()) { result.erase(field.spec.name); continue; }
        const auto* line = qobject_cast<QLineEdit*>(field.editor);
        const auto text = line ? line->text() : qobject_cast<QComboBox*>(field.editor)->currentText();
        if (field.spec.type == "string" || field.spec.type == "type" || field.spec.type == "enum") result[field.spec.name] = text.toStdString();
        else try { result[field.spec.name] = boost::json::parse(text.toStdString()); }
        catch (const std::exception& e) { error = QStringLiteral("%1: %2").arg(QString::fromStdString(field.spec.name), QString::fromUtf8(e.what())); return false; }
    }
    return true;
}
bool ParameterEditor::validate(boost::json::object& result, QString& error) const {
    if (metadata_) try {
        for (const auto& spec : metadata_->parameters)
            if (const auto* value = result.if_contains(spec.name); !spec.required && value && value->is_null()) result.erase(spec.name);
        result = s7::checked_params("properties", *metadata_, result);
    } catch (const std::exception& e) { error = QString::fromUtf8(e.what()); return false; }
    return true;
}
bool ParameterEditor::readParameters(boost::json::object& result, QString& error) const {
    return readTab(tabs_->currentIndex(), result, error) && validate(result, error);
}
void ParameterEditor::showError(const QString& error) {
    error_->setText(error.isEmpty() ? QString() : QStringLiteral("Cannot apply parameters: ") + error);
    error_->setAccessibleName(error_->text());
}
} // namespace s7studio::ui
