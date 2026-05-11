#include "visual_sink_detail_window.hpp"

#include "visual_sink_preview_widget.hpp"

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
    setWindowTitle(title + " - details");
    resize(980, 560);

    auto* root = new QVBoxLayout(this);

    preview_ = new VisualSinkPreviewWidget(false, this);
    root->addWidget(preview_, 1);

    auto* controls = new QGroupBox("Controls", this);
    auto* form = new QFormLayout(controls);

    gainDial_ = new QDial(controls);
    gainDial_->setRange(1, 60);
    gainDial_->setValue(10);
    gainDial_->setNotchesVisible(true);

    windowDial_ = new QDial(controls);
    windowDial_->setRange(5, 120);
    windowDial_->setValue(40);
    windowDial_->setNotchesVisible(true);

    inputsSpin_ = new QSpinBox(controls);
    inputsSpin_->setRange(1, 16);
    inputsSpin_->setValue(2);

    nameEdit_ = new QLineEdit(controls);
    idEdit_ = new QLineEdit(controls);

    gainValueLabel_ = new QLabel("1.0", controls);
    windowValueLabel_ = new QLabel("4.0 s", controls);

    auto* gainRow = new QWidget(controls);
    auto* gainLayout = new QHBoxLayout(gainRow);
    gainLayout->setContentsMargins(0, 0, 0, 0);
    gainLayout->addWidget(gainDial_);
    gainLayout->addWidget(gainValueLabel_);

    auto* windowRow = new QWidget(controls);
    auto* windowLayout = new QHBoxLayout(windowRow);
    windowLayout->setContentsMargins(0, 0, 0, 0);
    windowLayout->addWidget(windowDial_);
    windowLayout->addWidget(windowValueLabel_);

    form->addRow("Gain", gainRow);
    form->addRow("Time Window", windowRow);
    form->addRow("Inputs", inputsSpin_);
    form->addRow("Name", nameEdit_);
    form->addRow("ID", idEdit_);

    root->addWidget(controls, 0);

    connect(gainDial_, &QDial::valueChanged, this, [this](int value) {
        const double gain = static_cast<double>(value) / 10.0;
        gainValueLabel_->setText(QString::number(gain, 'f', 1));
        preview_->setGain(gain);
    });

    connect(windowDial_, &QDial::valueChanged, this, [this](int value) {
        const double seconds = static_cast<double>(value) / 10.0;
        windowValueLabel_->setText(QString::number(seconds, 'f', 1) + " s");
        preview_->setTimeWindow(seconds);
    });

    connect(inputsSpin_, &QSpinBox::valueChanged, this, [this](int value) {
        preview_->setInputCount(value);
        emit inputCountChanged(value);
    });

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
    preview_->setInputCount(count);
}

void VisualSinkDetailWindow::setIdentity(const QString& nodeName, const QString& nodeId)
{
    nameEdit_->setText(nodeName);
    idEdit_->setText(nodeId);
}

} // namespace s7ui::elements

