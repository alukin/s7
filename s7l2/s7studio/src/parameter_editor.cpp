#include "parameter_editor.hpp"
#include <s7/element_registry.hpp>
#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QTabWidget>
#include <QVBoxLayout>
#include <stdexcept>

namespace s7studio::ui {
namespace {
bool parseObject(const QString& text, boost::json::object& result, QString& error) {
    try {
        const auto parsed = boost::json::parse(text.toStdString());
        if (!parsed.is_object()) throw std::invalid_argument("Parameters must be a JSON object.");
        result = parsed.as_object(); return true;
    } catch (const std::exception& e) { error = QString::fromUtf8(e.what()); return false; }
}
}
ParameterEditor::ParameterEditor(std::optional<s7::ElementMetadata> metadata, boost::json::object params, QWidget* parent)
    : QWidget(parent), metadata_(std::move(metadata)), original_(std::move(params))
{
    auto* layout = new QVBoxLayout(this); layout->setContentsMargins(0, 0, 0, 0);
    tabs_ = new QTabWidget(this); tabs_->setObjectName("element.parameterTabs");
    auto* scroll = new QScrollArea(tabs_); scroll->setWidgetResizable(true);
    auto* formWidget = new QWidget(scroll); auto* form = new QFormLayout(formWidget);
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    form->setRowWrapPolicy(QFormLayout::WrapLongRows);
    if (metadata_) for (const auto& spec : metadata_->parameters) {
        if (!original_.contains(spec.name) && !spec.default_value.is_null()) original_[spec.name] = spec.default_value;
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
        editor->setObjectName("parameter." + name); editor->setAccessibleName(name);
        QString help = QString::fromStdString(spec.description) + "\nType: " + QString::fromStdString(spec.type);
        if (spec.min_value) help += QStringLiteral("; minimum %1").arg(*spec.min_value);
        if (spec.max_value) help += QStringLiteral("; maximum %1").arg(*spec.max_value);
        editor->setToolTip(help); editor->setAccessibleDescription(help);
        auto* field = new QWidget(formWidget); auto* column = new QVBoxLayout(field); column->setContentsMargins(0, 0, 0, 0);
        auto* row = new QHBoxLayout;
        QCheckBox* enabled = nullptr;
        if (!spec.required && spec.default_value.is_null()) {
            enabled = new QCheckBox(QStringLiteral("Set"), field); enabled->setObjectName("parameter.set." + name);
            enabled->setAccessibleName(QStringLiteral("Set optional %1").arg(name));
            connect(enabled, &QCheckBox::toggled, editor, &QWidget::setEnabled); row->addWidget(enabled);
        }
        row->addWidget(editor, 1); column->addLayout(row);
        auto* description = new QLabel(QString::fromStdString(spec.description), field); description->setWordWrap(true);
        description->setTextFormat(Qt::PlainText); description->setTextInteractionFlags(Qt::TextSelectableByMouse);
        column->addWidget(description);
        auto* label = new QLabel(name + (spec.required ? " *" : ""), formWidget); label->setBuddy(editor);
        form->addRow(label, field); fields_.push_back({spec, editor, enabled});
    }
    scroll->setWidget(formWidget); tabs_->addTab(scroll, QStringLiteral("&Parameters"));
    json_ = new QPlainTextEdit(QString::fromStdString(boost::json::serialize(original_)), tabs_);
    json_->setObjectName("element.parameters"); json_->setAccessibleName("Advanced parameter JSON");
    json_->setToolTip("Exact JSON values, including arrays, complex values and 64-bit integers. Known parameters are validated before applying.");
    tabs_->addTab(json_, QStringLiteral("Advanced &JSON"));
    error_ = new QLabel(this); error_->setObjectName("element.parameterError"); error_->setWordWrap(true);
    error_->setTextFormat(Qt::PlainText); error_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(tabs_, 1); layout->addWidget(error_);
    loadForm(original_);
    if (!metadata_) { tabs_->setTabEnabled(0, false); tabs_->setCurrentIndex(1); previousTab_ = 1; }
    else try { static_cast<void>(s7::checked_params("properties", *metadata_, original_)); }
    catch (const std::exception& e) {
        // Keep invalid imported types in JSON until corrected; the form must
        // never silently turn a numeric JSON value into a string or enum.
        tabs_->setCurrentIndex(1); previousTab_ = 1; showError(QString::fromUtf8(e.what()));
    }
    connect(tabs_, &QTabWidget::currentChanged, this, [this](int index) {
        boost::json::object params; QString error;
        bool ok = previousTab_ == 0 ? readForm(params, error) : parseObject(json_->toPlainText(), params, error);
        if (ok && index == 0 && metadata_) try {
            for (const auto& spec : metadata_->parameters)
                if (const auto* value = params.if_contains(spec.name); !spec.required && value && value->is_null()) params.erase(spec.name);
            params = s7::checked_params("properties", *metadata_, params);
        } catch (const std::exception& e) { ok = false; error = QString::fromUtf8(e.what()); }
        if (!ok) { QSignalBlocker guard(tabs_); tabs_->setCurrentIndex(previousTab_); showError(error); return; }
        if (index == 1) json_->setPlainText(QString::fromStdString(boost::json::serialize(params)));
        else { original_ = params; loadForm(params); }
        previousTab_ = index; showError({});
    });
}
void ParameterEditor::loadForm(const boost::json::object& params) {
    for (const auto& field : fields_) {
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
bool ParameterEditor::readForm(boost::json::object& result, QString& error) const {
    result = original_;
    for (const auto& field : fields_) {
        if (field.enabled && !field.enabled->isChecked()) { result.erase(field.spec.name); continue; }
        const auto* line = qobject_cast<QLineEdit*>(field.editor);
        const auto text = line ? line->text() : qobject_cast<QComboBox*>(field.editor)->currentText();
        if (field.spec.type == "string" || field.spec.type == "type" || field.spec.type == "enum") result[field.spec.name] = text.toStdString();
        else try { result[field.spec.name] = boost::json::parse(text.toStdString()); }
        catch (const std::exception& e) { error = QStringLiteral("%1: %2").arg(QString::fromStdString(field.spec.name), QString::fromUtf8(e.what())); return false; }
    }
    return true;
}
bool ParameterEditor::readParameters(boost::json::object& result, QString& error) const {
    if (!(tabs_->currentIndex() == 0 ? readForm(result, error) : parseObject(json_->toPlainText(), result, error))) return false;
    if (metadata_) try {
        for (const auto& spec : metadata_->parameters)
            if (const auto* value = result.if_contains(spec.name); !spec.required && value && value->is_null()) result.erase(spec.name);
        result = s7::checked_params("properties", *metadata_, result);
    } catch (const std::exception& e) { error = QString::fromUtf8(e.what()); return false; }
    return true;
}
void ParameterEditor::showError(const QString& error) {
    error_->setText(error.isEmpty() ? QString() : QStringLiteral("Cannot apply parameters: ") + error);
    error_->setAccessibleName(error_->text());
}
} // namespace s7studio::ui
