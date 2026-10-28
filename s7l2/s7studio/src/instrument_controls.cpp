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
#include <QPushButton>
#include <QPainter>
#include <QPaintEvent>
#include <QFocusEvent>
#include <QResizeEvent>
#include <QShowEvent>
#include <QLineEdit>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QRadialGradient>
#include <QLinearGradient>
#include <QToolButton>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>

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
    std::function<void()> resetPosition;
    std::function<void(int)> rotateScale;
protected:
    void wheelEvent(QWheelEvent* event) override {
        if (!rotateScale) { QDial::wheelEvent(event); return; }
        const auto delta = event->angleDelta();
        wheelRemainder_ += delta.y() ? delta.y() : delta.x();
        const int steps = wheelRemainder_/120; wheelRemainder_ %= 120;
        if (steps) rotateScale(steps);
        event->accept();
    }
    void mouseDoubleClickEvent(QMouseEvent* event) override {
        if (resetPosition && event->button() == Qt::LeftButton) { resetPosition(); event->accept(); }
        else QDial::mouseDoubleClickEvent(event);
    }
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
        const double position = maximum() == minimum() ? 0 : double(value()-minimum())/double(maximum()-minimum());
        p.rotate(wrapping() ? position*360.-180. : position*300.-150.);
        p.setPen(QPen(isEnabled() ? palette().color(QPalette::Highlight) : ink, 3., Qt::SolidLine, Qt::RoundCap));
        p.drawLine(QPointF(0, -radius*.35), QPointF(0, -radius+6));
        if (hasFocus()) {
            p.resetTransform(); p.setPen(QPen(palette().color(QPalette::Highlight), 1., Qt::DashLine));
            p.setBrush(Qt::NoBrush); p.drawRoundedRect(rect().adjusted(1, 1, -2, -2), 5, 5);
        }
    }
private:
    int wheelRemainder_ = 0;
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
    std::function<void(int)> step;
    void refreshReadout() { updateSummary(); }
    explicit ExactNumber(QWidget* parent) : QDoubleSpinBox(parent) {
        setLocale(QLocale::c()); setDecimals(323); setKeyboardTracking(false);
        setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        setFixedWidth(fontMetrics().horizontalAdvance("-0.0000e+00") + 28);
        setAlignment(Qt::AlignRight);
        // The compact face is presentation only. The editor and spinbox retain
        // all digits, so merely focusing or leaving it never rounds the value.
        summary_ = new QLabel(lineEdit()); summary_->setObjectName(QStringLiteral("control.numberReadout")); summary_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        summary_->setAutoFillBackground(true); summary_->setBackgroundRole(QPalette::Base);
        summary_->setForegroundRole(QPalette::Text); summary_->setAttribute(Qt::WA_TransparentForMouseEvents);
        connect(this, &QDoubleSpinBox::valueChanged, this, [this] { updateSummary(); });
        updateSummary();
    }
    QSize sizeHint() const override { return {width(), QDoubleSpinBox::sizeHint().height()}; }
    QSize minimumSizeHint() const override { return {width(), QDoubleSpinBox::minimumSizeHint().height()}; }
protected:
    void stepBy(int steps) override { if (step) step(steps); else QDoubleSpinBox::stepBy(steps); }
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
        // Keep an out-of-range signed value editable until commit. Rejecting
        // '-' immediately would turn a typed '-1' into the valid positive '1'.
        if (number == "-" || number == "+") return QValidator::Intermediate;
        bool parsed = false; const auto value = number.toDouble(&parsed);
        if (parsed && (!std::isfinite(value) || value < minimum() || value > maximum())) return QValidator::Intermediate;
        int cursor = std::clamp(position, 0, static_cast<int>(number.size()));
        return validator.validate(number, cursor);
    }
};
}
QDoubleSpinBox* compactNumber(QWidget* parent) { return new ExactNumber(parent); }
void compactActionButton(QPushButton* button, const QStyle::StandardPixmap icon,
                         const QString& name, const QString& help)
{
    button->setText({});
    button->setIcon(button->style()->standardIcon(icon));
    button->setIconSize({18, 18});
    button->setFixedSize(30, 28);
    button->setToolTip(name + QStringLiteral(" — ") + help);
    button->setAccessibleName(name);
    button->setAccessibleDescription(help);
}
void setCompactNumberValueSilently(QDoubleSpinBox* number, double value) {
    const QSignalBlocker block(number);
    number->setValue(value); static_cast<ExactNumber*>(number)->refreshReadout();
}
Throttle::Throttle(std::function<void()> update, QObject* parent, int intervalMs)
    : QObject(parent), update_(std::move(update)), intervalMs_(intervalMs) {
    trailing_.setSingleShot(true); connect(&trailing_, &QTimer::timeout, this, &Throttle::run);
}
void Throttle::request() {
    if (trailing_.isActive()) return;
    const auto elapsed = last_.isValid() ? last_.elapsed() : intervalMs_;
    if (elapsed >= intervalMs_) run(); else trailing_.start(static_cast<int>(intervalMs_ - elapsed));
}
void Throttle::flush() { if (trailing_.isActive()) run(); }
void Throttle::run() { trailing_.stop(); last_.start(); update_(); }
void InstrumentPanel::wheelEvent(QWheelEvent* event) { event->accept(); }
InstrumentSidebar::InstrumentSidebar(const QString& name, QWidget* parent) : InstrumentPanel(parent)
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
    readoutArea_ = textArea(".readout", 8, Qt::AlignLeft | Qt::AlignTop, readout_);
    root->addWidget(readoutArea_);
    busy_ = new QProgressBar(this); busy_->setObjectName(name+".processing"); busy_->setRange(0, 0);
    busy_->setTextVisible(false); busy_->setMaximumHeight(12); busy_->setAccessibleName("Processing analysis");
    auto policy = busy_->sizePolicy(); policy.setRetainSizeWhenHidden(true); busy_->setSizePolicy(policy);
    busy_->hide(); root->addWidget(busy_);
    root->addWidget(textArea(".status", 2, Qt::AlignLeft | Qt::AlignBottom, status_));
}
void InstrumentSidebar::addSection(QWidget* section) { settings_->insertWidget(settings_->count()-1, section); }
void InstrumentSidebar::setReadoutLines(int lines)
{
    readoutArea_->setFixedHeight(readout_->fontMetrics().lineSpacing()*std::max(1, lines)+12);
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
    toggle->setToolTip(QStringLiteral("Show or hide %1 settings.").arg(title));
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
    auto* layout = new QVBoxLayout(this); layout->setContentsMargins(2, 2, 2, 2); layout->setSpacing(4);
    auto* title = new QLabel(mapping == Mapping::LogScale ? "Scale / div" : mapping == Mapping::LogTime ? "Time / div (s)" : "Position", this);
    title->setObjectName(name + ".title"); title->setAlignment(Qt::AlignCenter);
    auto* dial = new InstrumentDial(this); dial_ = dial; dial_->setObjectName(name + ".dial");
    dial_->setWrapping(mapping == Mapping::RelativeOffset);
    dial_->setFixedSize(72, 72);
    dial_->setRange(0, 999); dial_->setSingleStep(1); dial_->setPageStep(3);
    dial_->setAccessibleName(title->text());
    auto* exact = new ExactNumber(this); number_ = exact;
    number_->setObjectName(name); number_->setAccessibleName(title->text() + " exact value");
    number_->setRange(mapping == Mapping::LogTime ? 1e-12 : mapping == Mapping::LogScale ? 1e-6 : -1e12,
                      mapping != Mapping::RelativeOffset ? 1e6 : 1e12);
    number_->setValue(mapping == Mapping::LogScale ? 1 : mapping == Mapping::LogTime ? 2 : 0);
    auto* action = new QToolButton(this);
    action->setFixedSize(24, 24);
    action->setIconSize({16, 16});
    action->setProperty("compactAction", true);
    if (mapping == Mapping::RelativeOffset) {
        dial_->setToolTip("Turn to move this channel vertically. One turn moves half the visible plot height. Double-click to reset to zero.");
        action->setObjectName(name + ".zero"); action->setIcon(style()->standardIcon(QStyle::SP_DialogResetButton));
        action->setToolTip("Reset this channel's display position to zero"); action->setAccessibleName("Reset channel position to zero");
        dial->resetPosition = [this] { number_->setValue(0); };
        connect(action, &QToolButton::clicked, this, dial->resetPosition);
        const QSignalBlocker block(dial_); dial_->setValue(previousPosition_);
    } else {
        fine_ = action; fine_->setObjectName(name + ".fine");
        fine_->setIcon(style()->standardIcon(QStyle::SP_FileDialogDetailedView)); fine_->setCheckable(true);
        fine_->setAccessibleName(title->text() + " fine adjustment");
        fine_->setToolTip("Unchecked: 1–2–5 steps. Checked: 100 logarithmic steps per decade. Exact entry is always available.");
        dial_->setToolTip("Clockwise zooms in. Coarse: 1–2–5 scale steps; Fine: smaller logarithmic steps. Does not change samples or simulation dt.");
        connect(fine_, &QToolButton::toggled, this, [this] { updateDial(); });
        // Spin arrows increase/decrease the numeric scale, while clockwise on
        // the dial decreases it (zoom in). Both start from the exact value.
        exact->step = [this](int steps) { stepScale(steps); };
        // One wheel detent is one scale step, independent of the desktop's
        // text-scroll-lines setting. Accumulate partial touchpad deltas.
        dial->rotateScale = [this](int steps) { stepScale(-steps); };
    }
    auto* heading = new QHBoxLayout; heading->setSpacing(4); heading->addWidget(title, 1); heading->addWidget(action);
    layout->addLayout(heading);
    if (mapping == Mapping::LogTime) {
        number_->setSuffix(QStringLiteral(" s/div"));
        number_->setFixedWidth(number_->width()+number_->fontMetrics().horizontalAdvance(number_->suffix()));
        auto* row = new QHBoxLayout; row->addWidget(dial_); row->addWidget(number_); row->addStretch(); layout->addLayout(row);
    } else {
        layout->addWidget(dial_, 0, Qt::AlignHCenter); layout->addWidget(number_, 0, Qt::AlignHCenter);
    }
    connect(number_, &QDoubleSpinBox::valueChanged, this, [this] { updateDial(); });
    connect(dial_, &QDial::valueChanged, this, [this](int position) {
        if (mapping_ == Mapping::RelativeOffset) {
            int delta = position-previousPosition_;
            if (delta > 500) delta -= 1000;
            if (delta < -500) delta += 1000;
            previousPosition_ = position;
            number_->setValue(number_->value() + delta*offsetStep_);
        } else if (fine_->isChecked()) {
            const double logHigh = std::log10(number_->maximum()), logLow = std::log10(number_->minimum());
            const double exponent = logHigh-(logHigh-logLow)*position/dial_->maximum();
            number_->setValue(position == 0 ? number_->maximum() : position == dial_->maximum() ? number_->minimum() : std::pow(10., exponent));
        } else {
            const auto stops = scaleStops(); number_->setValue(stops[stops.size()-1-position]);
        }
    });
    connect(dial_, &QDial::actionTriggered, this, [this](int actionType) {
        if (mapping_ == Mapping::RelativeOffset) return;
        switch (actionType) {
        case QAbstractSlider::SliderSingleStepAdd: stepScale(-1); break;
        case QAbstractSlider::SliderSingleStepSub: stepScale(1); break;
        case QAbstractSlider::SliderPageStepAdd: stepScale(-3); break;
        case QAbstractSlider::SliderPageStepSub: stepScale(3); break;
        case QAbstractSlider::SliderToMinimum: number_->setValue(number_->maximum()); break;
        case QAbstractSlider::SliderToMaximum: number_->setValue(number_->minimum()); break;
        default: return; // Mouse drags use the absolute slider position above.
        }
        // actionTriggered precedes Qt's valueChanged. Synchronize even if an
        // exact value is already at a bound, so one gesture applies one edit.
        updateDial();
    });
    updateDial();
}
QList<double> RotaryValueControl::scaleStops() const
{
    QList<double> stops{number_->minimum()};
    const int first = static_cast<int>(std::floor(std::log10(number_->minimum())));
    const int last = static_cast<int>(std::ceil(std::log10(number_->maximum())));
    for (int exponent = first; exponent <= last; ++exponent) {
        for (const int mantissa : {1, 2, 5}) {
            const double value = static_cast<double>(mantissa*std::pow(10.L, exponent));
            if (value > stops.back() && value < number_->maximum()) stops.push_back(value);
        }
    }
    if (stops.back() != number_->maximum()) stops.push_back(number_->maximum());
    return stops;
}
void RotaryValueControl::stepScale(int steps)
{
    if (!steps) return;
    if (fine_->isChecked()) {
        const double exponent = std::log10(number_->value())+steps/100.;
        number_->setValue(std::pow(10., std::clamp(exponent, std::log10(number_->minimum()), std::log10(number_->maximum()))));
        return;
    }
    const auto stops = scaleStops();
    const double value = number_->value();
    const auto equal = [value](double stop) { return std::abs(stop-value) <= std::abs(value)*8*std::numeric_limits<double>::epsilon(); };
    qsizetype index = 0;
    if (steps > 0) {
        while (index < stops.size()-1 && (stops[index] < value || equal(stops[index]))) ++index;
        index = std::min(stops.size()-1, index+steps-1);
    } else {
        index = stops.size()-1;
        while (index > 0 && (stops[index] > value || equal(stops[index]))) --index;
        index = std::max(qsizetype(0), index+steps+1);
    }
    number_->setValue(stops[index]);
}
void RotaryValueControl::setValueSilently(double value)
{
    const QSignalBlocker block(number_);
    number_->setValue(value); updateDial();
    static_cast<ExactNumber*>(number_)->refreshReadout();
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
    if (mapping_ == Mapping::RelativeOffset) return;
    if (fine_->isChecked()) {
        const double logHigh = std::log10(number_->maximum()), logLow = std::log10(number_->minimum());
        if (logHigh == logLow) { dial_->setRange(0, 1); dial_->setValue(0); return; }
        dial_->setRange(0, std::max(1, qRound((logHigh-logLow)*100)));
        dial_->setValue(qRound((logHigh-std::log10(number_->value()))/(logHigh-logLow)*dial_->maximum()));
    } else {
        const auto stops = scaleStops(); dial_->setRange(0, static_cast<int>(stops.size()-1));
        qsizetype nearest = 0;
        for (qsizetype i = 1; i < stops.size(); ++i)
            if (std::abs(std::log10(stops[i])-std::log10(number_->value())) < std::abs(std::log10(stops[nearest])-std::log10(number_->value()))) nearest = i;
        dial_->setValue(static_cast<int>(stops.size()-1-nearest));
    }
}
}
