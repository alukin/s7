#include "visual_sink_detail_window.hpp"

#include "oscilloscope_widget.hpp"

#include <QDial>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QSpinBox>
#include <QVBoxLayout>

namespace s7ui::elements {

VisualSinkDetailWindow::VisualSinkDetailWindow(const QString& title, QWidget* parent)
    : QWidget(parent)
{
    setWindowTitle(title + " - oscilloscope");
    resize(1080, 640);

    auto* root = new QVBoxLayout(this);

    oscilloscope_ = new OscilloscopeWidget(this);
    oscilloscope_->setTimeWindow(5.0);
    oscilloscope_->setSampleRate(1000.0);
    root->addWidget(oscilloscope_, 1);

    auto* controls = new QGroupBox("Controls", this);
    auto* form = new QFormLayout(controls);

    windowDial_ = new QDial(controls);
    windowDial_->setRange(5, 120);
    windowDial_->setValue(40);
    windowDial_->setNotchesVisible(true);

    inputsSpin_ = new QSpinBox(controls);
    inputsSpin_->setRange(1, 16);
    inputsSpin_->setValue(2);

    nameEdit_ = new QLineEdit(controls);
    idEdit_ = new QLineEdit(controls);

    auto* windowRow = new QWidget(controls);
    auto* windowLayout = new QHBoxLayout(windowRow);
    windowLayout->setContentsMargins(0, 0, 0, 0);
    windowLayout->addWidget(windowDial_);
    auto* windowValueLabel = new QLabel("4.0 s", controls);
    windowLayout->addWidget(windowValueLabel);

    form->addRow("Time Window", windowRow);
    form->addRow("Inputs", inputsSpin_);
    form->addRow("Name", nameEdit_);
    form->addRow("ID", idEdit_);

    root->addWidget(controls, 0);

    connect(windowDial_, &QDial::valueChanged, this, [this, windowValueLabel](int value) {
        const double seconds = static_cast<double>(value) / 10.0;
        windowValueLabel->setText(QString::number(seconds, 'f', 1) + " s");
        oscilloscope_->setTimeWindow(seconds);
    });

    connect(inputsSpin_, &QSpinBox::valueChanged, this, [this](int value) { emit inputCountChanged(value); });

    connect(nameEdit_, &QLineEdit::editingFinished, this, [this] {
        emit identityChanged(nameEdit_->text(), idEdit_->text());
    });

    connect(idEdit_, &QLineEdit::editingFinished, this, [this] {
        emit identityChanged(nameEdit_->text(), idEdit_->text());
    });
}

void VisualSinkDetailWindow::setInputCount(int count)
{
    inputsSpin_->setValue(count);
}

void VisualSinkDetailWindow::setIdentity(const QString& nodeName, const QString& nodeId)
{
    nameEdit_->setText(nodeName);
    idEdit_->setText(nodeId);
}

void VisualSinkDetailWindow::setProbe(const std::shared_ptr<s7::Probe>& probe)
{
    if (!oscilloscope_) return;
    oscilloscope_->clearAllProbes();
    if (probe) {
        oscilloscope_->attachProbe(probe, QStringLiteral("Signal"));
    }
}

void VisualSinkDetailWindow::setComplexProbe(const std::shared_ptr<s7::ComplexProbe>& probe)
{
    if (!oscilloscope_) return;
    oscilloscope_->clearAllProbes();
    if (probe) {
        oscilloscope_->attachComplexProbe(probe, QStringLiteral("Signal"));
    }
}

void VisualSinkDetailWindow::clearProbe()
{
    if (oscilloscope_) {
        oscilloscope_->clearAllProbes();
    }
}

} // namespace s7ui::elements

