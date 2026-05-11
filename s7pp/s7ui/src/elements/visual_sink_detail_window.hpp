#pragma once

#include <QWidget>

class QDial;
class QLabel;
class QLineEdit;
class QSpinBox;

namespace s7ui::elements {

class VisualSinkPreviewWidget;

class VisualSinkDetailWindow : public QWidget
{
    Q_OBJECT

public:
    explicit VisualSinkDetailWindow(const QString& title, QWidget* parent = nullptr);

    void setInputCount(int count);
    void setIdentity(const QString& nodeName, const QString& nodeId);

Q_SIGNALS:
    void inputCountChanged(int count);
    void identityChanged(const QString& nodeName, const QString& nodeId);

private:
    VisualSinkPreviewWidget* preview_ = nullptr;
    QDial* gainDial_ = nullptr;
    QDial* windowDial_ = nullptr;
    QSpinBox* inputsSpin_ = nullptr;
    QLineEdit* nameEdit_ = nullptr;
    QLineEdit* idEdit_ = nullptr;
    QLabel* gainValueLabel_ = nullptr;
    QLabel* windowValueLabel_ = nullptr;
};

} // namespace s7ui::elements

