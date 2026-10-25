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
#include <QScrollArea>
#include <QProgressBar>
#include <QPainter>
#include <QPaintEvent>
#include <QFocusEvent>
#include <QResizeEvent>
#include <QShowEvent>
#include <QLineEdit>
#include <QRadialGradient>
#include <QLinearGradient>
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
// Keep QDial's mouse, wheel, keyboard and accessibility behavior; paint the
// instrument face ourselves so it is consistent across desktop styles.
class InstrumentDial : public QDial {
public:
    using QDial::QDial;
protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this); p.setRenderHint(QPainter::Antialiasing);
        p.translate(width()/2., height()/2.);
        const double radius = std::min(width(), height())/2.-10;
        const QColor ink = palette().color(QPalette::WindowText);
        for (int i = 0; i <= 10; ++i) {
            p.save(); p.rotate(-150.+i*30.);
            p.setPen(QPen(ink, i%5 == 0 ? 1.8 : 1.));
            p.drawLine(QPointF(0, -radius-4), QPointF(0, -radius-(i%5 == 0 ? 9 : 7))); p.restore();
        }
        p.setPen(Qt::NoPen); p.setBrush(QColor(0, 0, 0, 60));
        p.drawEllipse(QPointF(1, 2), radius+1, radius+1);
        QLinearGradient rim(-radius, -radius, radius, radius);
        rim.setColorAt(0, QColor("#e8ecf0")); rim.setColorAt(.5, QColor("#737c86")); rim.setColorAt(1, QColor("#b8c0c8"));
        p.setBrush(rim); p.drawEllipse(QPointF(), radius, radius);
        QRadialGradient face(-radius*.35, -radius*.45, radius*1.6);
        face.setColorAt(0, QColor("#626d79")); face.setColorAt(.6, QColor("#303944")); face.setColorAt(1, QColor("#171d25"));
        p.setBrush(face); p.drawEllipse(QPointF(), radius-3, radius-3);
        const double position = double(value()-minimum())/double(maximum()-minimum());
        p.rotate(wrapping() ? position*360.-180. : position*300.-150.);
        p.setPen(QPen(isEnabled() ? palette().color(QPalette::Highlight) : ink, 3., Qt::SolidLine, Qt::RoundCap));
        p.drawLine(QPointF(0, -radius*.35), QPointF(0, -radius+6));
        if (hasFocus()) {
            p.resetTransform(); p.setPen(QPen(palette().color(QPalette::Highlight), 1., Qt::DashLine));
            p.setBrush(Qt::NoBrush); p.drawRoundedRect(rect().adjusted(1, 1, -2, -2), 5, 5);
        }
    }
};
class ExactNumber : public QDoubleSpinBox {
    QLabel* summary_ = nullptr;
    QString numericText(QString text) const {
        if (!prefix().isEmpty() && text.startsWith(prefix())) text.remove(0, prefix().size());
        if (!suffix().isEmpty() && text.endsWith(suffix())) text.chop(suffix().size());
        return text.trimmed();
    }
    void updateSummary() {
        summary_->setText(prefix()+QString::number(value(), 'g', 5)+suffix());
        summary_->setGeometry(lineEdit()->rect());
        summary_->setVisible(!hasFocus() && !lineEdit()->hasFocus());
    }
public:
    explicit ExactNumber(QWidget* parent) : QDoubleSpinBox(parent) {
        setLocale(QLocale::c()); setDecimals(323); setKeyboardTracking(false);
        setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        setFixedWidth(fontMetrics().horizontalAdvance("-0.0000e+00") + 28);
        setAlignment(Qt::AlignRight);
        // The compact face is presentation only. The editor and spinbox retain
        // all digits, so merely focusing or leaving it never rounds the value.
        summary_ = new QLabel(lineEdit()); summary_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        summary_->setAutoFillBackground(true); summary_->setBackgroundRole(QPalette::Base);
        summary_->setForegroundRole(QPalette::Text); summary_->setAttribute(Qt::WA_TransparentForMouseEvents);
        connect(this, &QDoubleSpinBox::valueChanged, this, [this] { updateSummary(); });
        updateSummary();
    }
    QSize sizeHint() const override { return {width(), QDoubleSpinBox::sizeHint().height()}; }
    QSize minimumSizeHint() const override { return {width(), QDoubleSpinBox::minimumSizeHint().height()}; }
protected:
    void showEvent(QShowEvent* event) override { QDoubleSpinBox::showEvent(event); updateSummary(); }
    void focusInEvent(QFocusEvent* event) override { QDoubleSpinBox::focusInEvent(event); summary_->hide(); }
    void focusOutEvent(QFocusEvent* event) override { QDoubleSpinBox::focusOutEvent(event); updateSummary(); }
    void resizeEvent(QResizeEvent* event) override { QDoubleSpinBox::resizeEvent(event); if (summary_) updateSummary(); }
    QString textFromValue(double value) const override { return QString::number(value, 'g', QLocale::FloatingPointShortest); }
    double valueFromText(const QString& text) const override { return numericText(text).toDouble(); }
    QValidator::State validate(QString& text, int& position) const override {
        QDoubleValidator validator(minimum(), maximum(), 323);
        validator.setLocale(QLocale::c()); validator.setNotation(QDoubleValidator::ScientificNotation);
        auto number = numericText(text);
        int cursor = std::clamp(position, 0, static_cast<int>(number.size()));
        return validator.validate(number, cursor);
    }
};
}
QDoubleSpinBox* compactNumber(QWidget* parent) { return new ExactNumber(parent); }
InstrumentSidebar::InstrumentSidebar(const QString& name, QWidget* parent) : QWidget(parent)
{
    setObjectName(name+".controls");
    auto* root = new QVBoxLayout(this); root->setContentsMargins(0, 0, 0, 0);
    auto* scroll = new QScrollArea(this); scroll->setObjectName(name+".settingsScroll");
    scroll->setWidgetResizable(true); scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto* content = new QWidget(scroll); settings_ = new QVBoxLayout(content);
    settings_->setContentsMargins(10, 4, 10, 4); settings_->addStretch(); scroll->setWidget(content); root->addWidget(scroll, 1);
    const auto textArea = [&](const QString& suffix, int lines, Qt::Alignment alignment, QLabel*& label) {
        auto* area = new QScrollArea(this); area->setObjectName(name+suffix+"Scroll");
        area->setWidgetResizable(true); area->setFrameShape(QFrame::NoFrame);
        area->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        label = new QLabel(area); label->setObjectName(name+suffix); label->setTextFormat(Qt::PlainText);
        label->setWordWrap(true); label->setMargin(4); label->setAlignment(alignment);
        label->setTextInteractionFlags(Qt::TextSelectableByMouse);
        area->setFixedHeight(label->fontMetrics().lineSpacing()*lines+(lines == 2 ? 12 : 16));
        area->setWidget(label); return area;
    };
    root->addWidget(textArea(".readout", 8, Qt::AlignLeft | Qt::AlignTop, readout_));
    busy_ = new QProgressBar(this); busy_->setObjectName(name+".processing"); busy_->setRange(0, 0);
    busy_->setTextVisible(false); busy_->setMaximumHeight(12); busy_->setAccessibleName("Processing analysis");
    auto policy = busy_->sizePolicy(); policy.setRetainSizeWhenHidden(true); busy_->setSizePolicy(policy);
    busy_->hide(); root->addWidget(busy_);
    root->addWidget(textArea(".status", 2, Qt::AlignLeft | Qt::AlignBottom, status_));
}
void InstrumentSidebar::addSection(QWidget* section) { settings_->insertWidget(settings_->count()-1, section); }

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
    toggle->setProperty("sectionHeader", true);
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
    auto* layout = new QVBoxLayout(this); layout->setContentsMargins(6, 2, 6, 2); layout->setSpacing(4);
    auto* title = new QLabel(mapping == Mapping::LogGain ? "Gain ×" : mapping == Mapping::LogTime ? "Time / div (s)" : "Position", this);
    title->setAlignment(Qt::AlignCenter); layout->addWidget(title);
    dial_ = new InstrumentDial(this); dial_->setObjectName(name + ".dial");
    dial_->setWrapping(mapping == Mapping::RelativeOffset);
    dial_->setRange(0, mapping == Mapping::LogGain ? 1200 : mapping == Mapping::LogTime ? 1800 : 999); dial_->setFixedSize(80, 80);
    dial_->setSingleStep(1); dial_->setPageStep(10);
    dial_->setAccessibleName(title->text()); layout->addWidget(dial_, 0, Qt::AlignHCenter);
    number_ = new ExactNumber(this); number_->setObjectName(name); number_->setAccessibleName(title->text() + " exact value");
    number_->setRange(mapping == Mapping::LogTime ? 1e-12 : mapping == Mapping::LogGain ? 1e-6 : -1e12, mapping != Mapping::RelativeOffset ? 1e6 : 1e12);
    number_->setValue(mapping == Mapping::LogGain ? 1 : mapping == Mapping::LogTime ? 2 : 0); layout->addWidget(number_, 0, Qt::AlignHCenter);
    if (mapping == Mapping::RelativeOffset) {
        dial_->setToolTip("Turn to move this channel vertically. One turn moves half the visible plot height. Numeric entry sets an exact offset.");
        const QSignalBlocker block(dial_); dial_->setValue(previousPosition_);
    } else dial_->setToolTip(mapping == Mapping::LogTime ? "Model seconds per division, ten divisions across the screen. Does not change simulation dt." : "Display multiplier for this channel. Numeric entry sets an exact gain.");
    connect(number_, &QDoubleSpinBox::valueChanged, this, [this] { updateDial(); });
    const auto applyPosition = [this](int position) {
        if (mapping_ != Mapping::RelativeOffset) number_->setValue(std::pow(10., (mapping_ == Mapping::LogTime ? -12. : -6.) + position/100.));
        else {
            int delta = position-previousPosition_;
            if (delta > 500) delta -= 1000;
            if (delta < -500) delta += 1000;
            previousPosition_ = position;
            number_->setValue(number_->value() + delta*offsetStep_);
        }
    };
    connect(dial_, &QDial::valueChanged, this, applyPosition);
    // End/Home must still reach an endpoint when an exact gain rounds to it.
    connect(dial_, &QDial::actionTriggered, this, [this, applyPosition](int) { applyPosition(dial_->sliderPosition()); });
    updateDial();
}
void RotaryValueControl::setOffsetStep(double step)
{
    if (mapping_ != Mapping::RelativeOffset || !std::isfinite(step) || step <= 0) return;
    offsetStep_ = step; number_->setSingleStep(step);
}
void RotaryValueControl::updateDial()
{
    const QSignalBlocker block(dial_);
    number_->setToolTip(QString::number(number_->value(), 'g', QLocale::FloatingPointShortest));
    if (mapping_ != Mapping::RelativeOffset) dial_->setValue(qRound((std::log10(number_->value())+(mapping_ == Mapping::LogTime ? 12. : 6.))*100.));
}
}
