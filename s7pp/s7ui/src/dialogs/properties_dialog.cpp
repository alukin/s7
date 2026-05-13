#include "properties_dialog.hpp"

#include <s7.hpp>

#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLineEdit>
#include <QVBoxLayout>
#include <QLabel>

namespace s7ui::dialogs {

PropertiesDialog::PropertiesDialog(const QString& typeName,
                                   const QString& nodeId,
                                   const QString& nodeName,
                                   const s7::ParamMap& params,
                                   QWidget* parent)
    : QDialog(parent), typeName_(typeName)
{
    setWindowTitle(typeName_ + " Properties");
    setMinimumWidth(400);

    auto* layout = new QVBoxLayout(this);
    form_ = new QFormLayout();

    nameEdit_ = new QLineEdit(nodeName, this);
    idEdit_ = new QLineEdit(nodeId, this);
    auto* typeLabel = new QLabel(typeName, this);

    form_->addRow("Name", nameEdit_);
    form_->addRow("ID", idEdit_);
    form_->addRow("Type", typeLabel);

    for (const auto& [key, value] : params) {
        auto* edit = new QLineEdit(this);
        if (std::holds_alternative<s7::Real>(value)) {
            edit->setText(QString::number(std::get<s7::Real>(value)));
        } else if (std::holds_alternative<s7::Complex>(value)) {
            const auto& c = std::get<s7::Complex>(value);
            edit->setText(QString("%1, %2").arg(c.real()).arg(c.imag()));
        } else if (std::holds_alternative<std::string>(value)) {
            edit->setText(QString::fromStdString(std::get<std::string>(value)));
        }
        form_->addRow(QString::fromStdString(key), edit);
        paramEdits_.emplace_back(key, edit);
    }

    layout->addLayout(form_);

    auto* buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttonBox, &QDialogButtonBox::accepted, this, &PropertiesDialog::onAccept);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttonBox);
}

QString PropertiesDialog::nodeId() const
{
    return idEdit_ ? idEdit_->text() : QString();
}

QString PropertiesDialog::nodeName() const
{
    return nameEdit_ ? nameEdit_->text() : QString();
}

s7::ParamMap PropertiesDialog::params() const
{
    s7::ParamMap newParams;
    for (const auto& [key, edit] : paramEdits_) {
        // This is a simplification. A real implementation would need to know the type
        // of the parameter to parse it correctly. For now, we'll treat everything as a string.
        newParams[key] = edit->text().toStdString();
    }
    return newParams;
}

void PropertiesDialog::onAccept()
{
    // Validation could be added here
    accept();
}

} // namespace s7ui::dialogs

