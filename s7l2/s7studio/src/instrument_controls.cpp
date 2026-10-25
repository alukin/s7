// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-S7-plugin-permission
#include "instrument_controls.hpp"
#include <QButtonGroup>
#include <QDial>
#include <QDoubleSpinBox>
#include <QDoubleValidator>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QLabel>
#include <QLayout>
#include <QLocale>
#include <QSignalBlocker>
#include <QToolButton>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>

namespace s7studio::ui {
namespace {
class SettingsLayout : public QLayout {
public:
    explicit SettingsLayout(QWidget* parent) : QLayout(parent) { setContentsMargins(0, 0, 0, 0); setSpacing(6); }
    ~SettingsLayout() override { while (auto* item = takeAt(0)) delete item; }
    void addItem(QLayoutItem* item) override { items_.push_back(item); invalidate(); }
    int count() const override { return static_cast<int>(items_.size()); }
    QLayoutItem* itemAt(int index) const override { return index >= 0 && index < count() ? items_[index] : nullptr; }
    QLayoutItem* takeAt(int index) override { if (index < 0 || index >= count()) return nullptr; auto* item = items_.takeAt(index); invalidate(); return item; }
    Qt::Orientations expandingDirections() const override { return Qt::Horizontal; }
    bool hasHeightForWidth() const override { return true; }
    int heightForWidth(int width) const override { return arrange({0, 0, width, 0}, false); }
    QSize minimumSize() const override {
        QSize result; for (auto* item : items_) if (!item->isEmpty()) result = result.expandedTo(item->minimumSize()); return result;
    }
    QSize sizeHint() const override { return {600, heightForWidth(600)}; }
    void setGeometry(const QRect& rect) override { QLayout::setGeometry(rect); arrange(rect, true); }
private:
    int arrange(const QRect& rect, bool apply) const {
        const int columns = std::max(1, (rect.width()+spacing())/(230+spacing()));
        const int cellWidth = std::max(0, (rect.width() - (columns-1)*spacing())/columns);
        int column = 0, y = rect.y(), rowHeight = 0;
        for (auto* item : items_) {
            if (item->isEmpty()) continue;
            if (column == columns) { y += rowHeight+spacing(); column = 0; rowHeight = 0; }
            const int height = item->hasHeightForWidth() ? item->heightForWidth(cellWidth) : item->sizeHint().height();
            if (apply) item->setGeometry({rect.x()+column*(cellWidth+spacing()), y, cellWidth, height});
            rowHeight = std::max(rowHeight, height); ++column;
        }
        return y+rowHeight-rect.y();
    }
    QList<QLayoutItem*> items_;
};
class ExactNumber : public QDoubleSpinBox {
public:
    explicit ExactNumber(QWidget* parent) : QDoubleSpinBox(parent) {
        setLocale(QLocale::c()); setDecimals(323); setKeyboardTracking(false);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        setMinimumWidth(100);
    }
    QSize sizeHint() const override { return {130, QDoubleSpinBox::sizeHint().height()}; }
    QSize minimumSizeHint() const override { return {100, QDoubleSpinBox::minimumSizeHint().height()}; }
protected:
    QString textFromValue(double value) const override { return QString::number(value, 'g', QLocale::FloatingPointShortest); }
    double valueFromText(const QString& text) const override { return text.toDouble(); }
    QValidator::State validate(QString& text, int& position) const override {
        QDoubleValidator validator(minimum(), maximum(), 323);
        validator.setLocale(QLocale::c()); validator.setNotation(QDoubleValidator::ScientificNotation);
        return validator.validate(text, position);
    }
};
}
ChoiceButtons::ChoiceButtons(const QStringList& labels, const QString& name, QWidget* parent) : QWidget(parent)
{
    setObjectName(name);
    auto* layout = new QHBoxLayout(this); layout->setContentsMargins(0, 0, 0, 0); layout->setSpacing(2);
    auto* group = new QButtonGroup(this); group->setExclusive(true);
    for (int i = 0; i < labels.size(); ++i) {
        auto* button = new QToolButton(this); button->setText(labels[i]); button->setCheckable(true);
        button->setObjectName(name + "." + QString::number(i)); button->setAccessibleName(labels[i]);
        button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        group->addButton(button, i); buttons_.push_back(button); layout->addWidget(button);
    }
    if (!buttons_.isEmpty()) buttons_.front()->setChecked(true);
    connect(group, &QButtonGroup::idToggled, this, [this](int index, bool checked) { if (checked) emit currentIndexChanged(index); });
}
int ChoiceButtons::currentIndex() const {
    for (int i = 0; i < buttons_.size(); ++i) if (buttons_[i]->isChecked()) return i;
    return -1;
}
void ChoiceButtons::setCurrentIndex(int index) { if (index >= 0 && index < buttons_.size()) buttons_[index]->setChecked(true); }

WrappingSettings::WrappingSettings(QWidget* parent) : QWidget(parent) { new SettingsLayout(this); }
void WrappingSettings::addField(const QString& text, QWidget* control) {
    auto* cell = new QWidget(this); auto* row = new QVBoxLayout(cell); row->setContentsMargins(0, 0, 0, 0); row->setSpacing(2);
    auto* label = new QLabel(text, cell); label->setWordWrap(true); label->setBuddy(control); row->addWidget(label); row->addWidget(control, 1);
    control->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed); layout()->addWidget(cell);
}
void WrappingSettings::addControl(QWidget* control) { layout()->addWidget(control); }

CollapsibleSection::CollapsibleSection(const QString& title, const QString& name, QWidget* content, bool expanded, QWidget* parent) : QWidget(parent)
{
    setObjectName(name);
    auto* layout = new QVBoxLayout(this); layout->setContentsMargins(0, 0, 0, 0); layout->setSpacing(4);
    auto* toggle = new QToolButton(this); toggle->setObjectName(name + ".toggle"); toggle->setText(title);
    toggle->setToolButtonStyle(Qt::ToolButtonTextBesideIcon); toggle->setCheckable(true); toggle->setChecked(expanded);
    toggle->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed); toggle->setArrowType(expanded ? Qt::DownArrow : Qt::RightArrow);
    layout->addWidget(toggle); layout->addWidget(content); content->setVisible(expanded);
    connect(toggle, &QToolButton::toggled, this, [toggle, content](bool checked) {
        toggle->setArrowType(checked ? Qt::DownArrow : Qt::RightArrow); content->setVisible(checked);
    });
}

RotaryValueControl::RotaryValueControl(Mapping mapping, const QString& name, QWidget* parent) : QWidget(parent), mapping_(mapping)
{
    setObjectName(name + ".control");
    auto* layout = new QGridLayout(this); layout->setContentsMargins(0, 0, 0, 0); layout->setSpacing(3); layout->setColumnStretch(0, 1);
    auto* title = new QLabel(mapping == Mapping::LogGain ? "Display gain" : "Display offset", this); layout->addWidget(title, 0, 0, 1, 2);
    dial_ = new QDial(this); dial_->setObjectName(name + ".dial"); dial_->setWrapping(false); dial_->setNotchesVisible(true);
    dial_->setRange(0, mapping == Mapping::LogGain ? 1200 : 1000); dial_->setFixedSize(64, 64);
    dial_->setAccessibleName(title->text()); layout->addWidget(dial_, 1, 1);
    number_ = new ExactNumber(this); number_->setObjectName(name); number_->setAccessibleName(title->text() + " exact value");
    number_->setRange(mapping == Mapping::LogGain ? 1e-6 : -1e12, mapping == Mapping::LogGain ? 1e6 : 1e12);
    number_->setValue(mapping == Mapping::LogGain ? 1 : 0); layout->addWidget(number_, 1, 0);
    if (mapping == Mapping::LocalOffset) {
        auto* spanLabel = new QLabel("Dial half-span (raw units)", this); spanLabel->setWordWrap(true); layout->addWidget(spanLabel, 2, 0, 1, 2);
        span_ = new ExactNumber(this); span_->setObjectName(name + ".span"); span_->setRange(1e-12, 1e12); span_->setValue(1);
        span_->setAccessibleName("Offset dial half-span in raw signal units"); layout->addWidget(span_, 3, 0, 1, 2);
        interval_ = new QLabel(this); interval_->setObjectName(name + ".interval"); interval_->setWordWrap(true);
        interval_->setTextFormat(Qt::PlainText); layout->addWidget(interval_, 4, 0, 1, 2);
        connect(span_, &QDoubleSpinBox::valueChanged, this, [this] { recenter(); });
        connect(number_, &QDoubleSpinBox::editingFinished, this, [this] { recenter(); });
    }
    connect(number_, &QDoubleSpinBox::valueChanged, this, [this] { updateDial(); });
    const auto applyPosition = [this](int position) {
        if (mapping_ == Mapping::LogGain) number_->setValue(std::pow(10., -6. + position/100.));
        else {
            const double lo = std::max(-1e12, center_ - span_->value()), hi = std::min(1e12, center_ + span_->value());
            number_->setValue(lo + (hi-lo)*position/1000.);
        }
    };
    connect(dial_, &QDial::valueChanged, this, applyPosition);
    // A precise value can round to the same dial position as an endpoint.
    // Explicit key/wheel actions still apply that endpoint, even without a
    // valueChanged signal; a final mouse release applies its last position.
    connect(dial_, &QDial::actionTriggered, this, [this, applyPosition](int) { applyPosition(dial_->sliderPosition()); });
    connect(dial_, &QDial::sliderReleased, this, [this, applyPosition] { applyPosition(dial_->value()); });
    updateDial();
}
void RotaryValueControl::recenter() { center_ = number_->value(); updateDial(); }
void RotaryValueControl::updateDial()
{
    const QSignalBlocker block(dial_);
    number_->setToolTip(QString::number(number_->value(), 'g', QLocale::FloatingPointShortest));
    if (mapping_ == Mapping::LogGain) dial_->setValue(qRound((std::log10(number_->value())+6.)*100.));
    else {
        const double lo = std::max(-1e12, center_ - span_->value()), hi = std::min(1e12, center_ + span_->value());
        dial_->setEnabled(hi > lo);
        dial_->setValue(hi > lo ? qRound(std::clamp((number_->value()-lo)/(hi-lo), 0., 1.)*1000.) : 500);
        interval_->setText(QStringLiteral("Dial: [%1, %2]").arg(lo, 0, 'g', QLocale::FloatingPointShortest).arg(hi, 0, 'g', QLocale::FloatingPointShortest));
    }
}
}
