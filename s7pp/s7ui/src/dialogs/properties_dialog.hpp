#pragma once

#include <s7factory.hpp>

#include <QDialog>

class QDialogButtonBox;
class QFormLayout;
class QLineEdit;
class QPlainTextEdit;

namespace s7ui::dialogs {

class PropertiesDialog : public QDialog
{
    Q_OBJECT

public:
    explicit PropertiesDialog(const QString& typeName,
                              const QString& nodeId,
                              const QString& nodeName,
                              const s7::ParamMap& params,
                              QWidget* parent = nullptr);

    QString nodeId() const;
    QString nodeName() const;
    s7::ParamMap params() const;

private:
    void onAccept();

    QString typeName_;
    QLineEdit* nameEdit_ = nullptr;
    QLineEdit* idEdit_ = nullptr;
    QFormLayout* form_ = nullptr;
    std::vector<std::pair<std::string, QLineEdit*>> paramEdits_;
};

} // namespace s7ui::dialogs

