#pragma once

#include <QDialog>

#include <QString>

class QDialogButtonBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;

namespace s7ui::dialogs {

/// Properties dialog for a node element.
///
/// Parameters are displayed as a single editable JSON object text area.
/// The dialog refuses to close with OK/Apply while the JSON is malformed
/// or is not a JSON object.
class PropertiesDialog : public QDialog
{
    Q_OBJECT

public:
    /// @param typeName     Read-only element type label.
    /// @param nodeId       Initial ID value.
    /// @param nodeName     Initial display name.
    /// @param paramsJson   Current params serialised as a JSON object string.
    /// @param portHint     Optional human-readable port layout hint shown below the params editor.
    /// @param parent       Parent widget.
    explicit PropertiesDialog(const QString& typeName,
                              const QString& nodeId,
                              const QString& nodeName,
                              const QString& paramsJson,
                              const QString& portHint = {},
                              QWidget* parent = nullptr);

    /// Returns the edited node ID.
    [[nodiscard]] QString nodeId() const;

    /// Returns the edited node display name.
    [[nodiscard]] QString nodeName() const;

    /// Returns the params as a validated JSON object string.
    [[nodiscard]] QString paramsJson() const;

private slots:
    void onParamsTextChanged();

private:
    void tryAccept();

    QLineEdit*     nameEdit_    = nullptr;
    QLineEdit*     idEdit_      = nullptr;
    QPlainTextEdit* paramsEdit_ = nullptr;
    QLabel*        errorLabel_  = nullptr;
    QDialogButtonBox* buttonBox_= nullptr;
};

} // namespace s7ui::dialogs
