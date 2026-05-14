#include "properties_dialog.hpp"

#include <boost/json.hpp>

#include <QDialogButtonBox>
#include <QFont>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QVBoxLayout>

namespace s7ui::dialogs {

namespace {

/// Returns an empty string when the text is valid JSON object, otherwise an error message.
auto validateParamsJson(const QString& text) -> QString
{
    if (text.trimmed().isEmpty()) return {};
    try {
        const boost::json::value parsed = boost::json::parse(text.toStdString());
        if (!parsed.is_object()) {
            return QStringLiteral("Parameters must be a JSON object { … }");
        }
    } catch (const std::exception& e) {
        return QString::fromStdString(e.what());
    }
    return {};
}

} // namespace

PropertiesDialog::PropertiesDialog(const QString& typeName,
                                   const QString& nodeId,
                                   const QString& nodeName,
                                   const QString& paramsJson,
                                   const QString& portHint,
                                   QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(typeName + QStringLiteral(" Properties"));
    setMinimumWidth(480);

    auto* layout = new QVBoxLayout(this);
    auto* form   = new QFormLayout();
    layout->addLayout(form);

    nameEdit_ = new QLineEdit(nodeName, this);
    idEdit_   = new QLineEdit(nodeId,   this);
    auto* typeLabel = new QLabel(typeName, this);
    typeLabel->setStyleSheet(QStringLiteral("color: grey;"));

    form->addRow(QStringLiteral("Name"), nameEdit_);
    form->addRow(QStringLiteral("ID"),   idEdit_);
    form->addRow(QStringLiteral("Type"), typeLabel);

    layout->addWidget(new QLabel(QStringLiteral("Parameters (JSON):"), this));

    paramsEdit_ = new QPlainTextEdit(this);
    paramsEdit_->setMinimumHeight(120);
    {
        QFont mono(QStringLiteral("monospace"));
        mono.setStyleHint(QFont::TypeWriter);
        mono.setPointSize(10);
        paramsEdit_->setFont(mono);
    }
    // Show current params, or a minimal placeholder
    const QString displayJson = paramsJson.trimmed().isEmpty()
                                    ? QStringLiteral("{}")
                                    : paramsJson;
    paramsEdit_->setPlainText(displayJson);
    layout->addWidget(paramsEdit_);

    if (!portHint.isEmpty()) {
        auto* hint = new QLabel(QStringLiteral("Port layout: ") + portHint, this);
        hint->setStyleSheet(QStringLiteral("color: grey; font-style: italic;"));
        layout->addWidget(hint);
    }

    errorLabel_ = new QLabel(this);
    errorLabel_->setStyleSheet(QStringLiteral("color: red;"));
    errorLabel_->setWordWrap(true);
    errorLabel_->hide();
    layout->addWidget(errorLabel_);

    buttonBox_ = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttonBox_, &QDialogButtonBox::accepted, this, &PropertiesDialog::tryAccept);
    connect(buttonBox_, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttonBox_);

    connect(paramsEdit_, &QPlainTextEdit::textChanged, this, &PropertiesDialog::onParamsTextChanged);
    onParamsTextChanged(); // initial validation pass
}

QString PropertiesDialog::nodeId() const
{
    return idEdit_ ? idEdit_->text().trimmed() : QString{};
}

QString PropertiesDialog::nodeName() const
{
    return nameEdit_ ? nameEdit_->text().trimmed() : QString{};
}

QString PropertiesDialog::paramsJson() const
{
    if (!paramsEdit_) return QStringLiteral("{}");
    const QString text = paramsEdit_->toPlainText().trimmed();
    return text.isEmpty() ? QStringLiteral("{}") : text;
}

void PropertiesDialog::onParamsTextChanged()
{
    const QString error = validateParamsJson(paramsEdit_->toPlainText());
    if (error.isEmpty()) {
        errorLabel_->hide();
        if (buttonBox_) buttonBox_->button(QDialogButtonBox::Ok)->setEnabled(true);
    } else {
        errorLabel_->setText(error);
        errorLabel_->show();
        if (buttonBox_) buttonBox_->button(QDialogButtonBox::Ok)->setEnabled(false);
    }
}

void PropertiesDialog::tryAccept()
{
    const QString error = validateParamsJson(paramsEdit_->toPlainText());
    if (!error.isEmpty()) {
        errorLabel_->setText(error);
        errorLabel_->show();
        return; // block close
    }
    accept();
}

} // namespace s7ui::dialogs
