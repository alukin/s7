#pragma once

#include <s7probe.hpp>

#include <QWidget>

class QDial;
class QLineEdit;
class QSpinBox;
class OscilloscopeWidget;

namespace s7ui::elements {

class VisualSinkDetailWindow : public QWidget
{
    Q_OBJECT

public:
    explicit VisualSinkDetailWindow(const QString& title, QWidget* parent = nullptr);

    void setInputCount(int count);
    void setIdentity(const QString& nodeName, const QString& nodeId);
    void setProbe(const std::shared_ptr<s7::Probe>& probe);
    void setComplexProbe(const std::shared_ptr<s7::ComplexProbe>& probe);
    void clearProbe();

Q_SIGNALS:
    void inputCountChanged(int count);
    void identityChanged(const QString& nodeName, const QString& nodeId);

private:
    OscilloscopeWidget* oscilloscope_ = nullptr;
    QDial* windowDial_ = nullptr;
    QSpinBox* inputsSpin_ = nullptr;
    QLineEdit* nameEdit_ = nullptr;
    QLineEdit* idEdit_ = nullptr;
};

} // namespace s7ui::elements

