// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-S7-plugin-permission
#include "viewers/live_spectrum_window.hpp"
#include "viewers/viewer_print.hpp"

#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QHBoxLayout>
#include <QLabel>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QRadioButton>
#include <QSpinBox>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace s7studio::viewers {
namespace {

constexpr int kRefreshMs = 50;
constexpr double kBarFallDbPerSecond = 40;   // bars jump up, then fall like an analyzer
constexpr double kPeakHoldSeconds = 1.0;     // peak caps wait, then drop
constexpr double kPeakFallDbPerSecond = 20;
constexpr double kCurvePeakFallDbPerSecond = 10;

QString hertz(double value)
{
    const double magnitude = std::abs(value);
    if (magnitude >= 1e6) return QString::number(value / 1e6, 'f', magnitude >= 1e8 ? 2 : 3) + QStringLiteral("M");
    if (magnitude >= 1e3) return QString::number(value / 1e3, 'g', 3) + QStringLiteral("k");
    return QString::number(value, 'g', 3);
}

// The analyzer drawing, on screen (dark instrument look) or on white paper.
struct SpectrumPalette {
    QColor background, grid, text, green, yellow, red, peakTrace, curveLine, fillTop, fillBottom;
    bool paper = false;
};

const SpectrumPalette kScreen{QColor(10, 10, 12), QColor(48, 52, 56), QColor(150, 158, 165), QColor(40, 225, 70),
                              QColor(255, 210, 40), QColor(255, 55, 40), QColor(255, 140, 40, 170), QColor(150, 255, 170),
                              QColor(80, 255, 120, 150), QColor(20, 80, 40, 60), false};
const SpectrumPalette kPaper{Qt::white, QColor(215, 215, 215), QColor(40, 40, 40), QColor(0, 150, 45), QColor(205, 150, 0),
                             QColor(205, 35, 25), QColor(215, 100, 0), QColor(0, 110, 40), QColor(0, 160, 60, 110),
                             QColor(0, 160, 60, 25), true};

class SpectrumPainter {
public:
    SpectrumPainter(const LiveSpectrumDisplay& display, const SpectrumPalette& palette) : display_(display), palette_(palette) {}

    void paint(QPainter& painter, const QRectF& area) const
    {
        painter.save();
        painter.fillRect(area, palette_.background);
        const QRectF plot = area.adjusted(46, 12, -12, -28);
        if (plot.width() >= 40 && plot.height() >= 40) {
            QFont font = painter.font();
            font.setPointSize(8);
            painter.setFont(font);
            drawScale(painter, area, plot);
            if (display_.spectrum.frequencies.empty()) {
                painter.setPen(palette_.text);
                painter.drawText(plot, Qt::AlignCenter, display_.message);
            } else if (display_.style == LiveSpectrumDisplay::Style::Bars) {
                drawBars(painter, plot);
            } else {
                drawCurve(painter, plot);
            }
        }
        painter.restore();
    }

private:
    [[nodiscard]] double fraction(double decibels) const
    {
        return std::clamp((decibels - display_.floorDb) / (display_.ceilingDb - display_.floorDb), 0.0, 1.0);
    }

    void drawScale(QPainter& painter, const QRectF& area, const QRectF& plot) const
    {
        const int step = display_.ceilingDb - display_.floorDb > 80 ? 20 : 10;
        for (int db = static_cast<int>(std::ceil(display_.floorDb / step)) * step; db <= display_.ceilingDb; db += step) {
            const double y = plot.bottom() - fraction(db) * plot.height();
            painter.setPen(palette_.grid);
            painter.drawLine(QPointF(plot.left(), y), QPointF(plot.right(), y));
            painter.setPen(palette_.text);
            painter.drawText(QRectF(area.left(), y - 8, plot.left() - area.left() - 6, 16), Qt::AlignRight | Qt::AlignVCenter, QString::number(db));
        }
        painter.drawText(QRectF(area.left(), plot.bottom() + 6, plot.left() - area.left() - 6, 16), Qt::AlignRight | Qt::AlignVCenter,
                         QStringLiteral("dBFS"));
        if (palette_.paper) {
            painter.setPen(QPen(palette_.grid.darker(140), 0.75));
            painter.setBrush(Qt::NoBrush);
            painter.drawRect(plot);
        }
    }

    void drawBars(QPainter& painter, const QRectF& plot) const
    {
        const auto& spectrum = display_.spectrum;
        const auto count = display_.bars.size();
        if (!count) return;
        const double slot = plot.width() / static_cast<double>(count);
        const double segmentStep = 6.0, segmentHeight = 4.0;
        const int segments = std::max(4, static_cast<int>(plot.height() / segmentStep));
        painter.setPen(Qt::NoPen);
        for (std::size_t bar = 0; bar < count; ++bar) {
            const double x = plot.left() + bar * slot + slot * 0.12;
            const double width = std::max(1.0, slot * 0.76);
            const double lit = fraction(display_.bars[bar]) * segments;
            const int peak = display_.barPeaks.empty() ? -1
                : std::clamp(static_cast<int>(fraction(display_.barPeaks[bar]) * segments) - 1, 0, segments - 1);
            for (int segment = 0; segment < segments; ++segment) {
                const double level = (segment + 0.5) / segments;
                const QColor colour = level < 0.65 ? palette_.green : level < 0.85 ? palette_.yellow : palette_.red;
                const double y = plot.bottom() - (segment + 1) * segmentStep + (segmentStep - segmentHeight);
                const bool on = segment + 0.5 <= lit;
                if (on || segment == peak) {
                    painter.setBrush(segment == peak && !on ? (palette_.paper ? colour.darker(130) : colour.lighter(130)) : colour);
                } else if (palette_.paper) {
                    // An unlit LED: a faint tint, light on paper.
                    painter.setBrush(QColor((colour.red() + 7 * 255) / 8, (colour.green() + 7 * 255) / 8, (colour.blue() + 7 * 255) / 8));
                } else {
                    painter.setBrush(QColor(colour.red() / 7, colour.green() / 7, colour.blue() / 7));
                }
                painter.drawRect(QRectF(x, y, width, segmentHeight));
            }
        }
        // Frequency labels under a subset of bars.
        painter.setPen(palette_.text);
        const std::size_t every = std::max<std::size_t>(1, count / 10);
        for (std::size_t bar = 0; bar < count; bar += every) {
            const double centre = display_.iq || !display_.logFrequency
                ? (spectrum.barLow[bar] + spectrum.barHigh[bar]) / 2
                : std::sqrt(spectrum.barLow[bar] * spectrum.barHigh[bar]);
            const double x = plot.left() + (bar + 0.5) * slot;
            painter.drawText(QRectF(x - 30, plot.bottom() + 6, 60, 16), Qt::AlignCenter, hertz(centre + display_.centreHz));
        }
    }

    void drawCurve(QPainter& painter, const QRectF& plot) const
    {
        const auto& spectrum = display_.spectrum;
        const auto& frequencies = spectrum.frequencies;
        if (frequencies.size() < 2) return;
        const bool log = display_.logFrequency && !display_.iq;
        const double low = log ? std::max(spectrum.barLow.empty() ? 20.0 : spectrum.barLow.front(), frequencies[1])
                               : frequencies.front();
        const double high = frequencies.back();
        const auto xOf = [&](double f) {
            const double t = log ? std::log(f / low) / std::log(high / low) : (f - low) / (high - low);
            return plot.left() + t * plot.width();
        };
        // Frequency grid and labels.
        std::vector<double> ticks;
        if (log) {
            for (double decade = 1; decade <= high; decade *= 10)
                for (const double m : {1.0, 2.0, 5.0}) if (decade * m >= low && decade * m <= high) ticks.push_back(decade * m);
        } else {
            for (int i = 0; i <= 8; ++i) ticks.push_back(low + (high - low) * i / 8);
        }
        for (const double f : ticks) {
            const double x = xOf(f);
            painter.setPen(palette_.grid);
            painter.drawLine(QPointF(x, plot.top()), QPointF(x, plot.bottom()));
            painter.setPen(palette_.text);
            painter.drawText(QRectF(x - 32, plot.bottom() + 6, 64, 16), Qt::AlignCenter, hertz(f + display_.centreHz));
        }
        const auto trace = [&](const std::vector<double>& values) {
            QPainterPath path;
            bool started = false;
            for (std::size_t i = 0; i < frequencies.size() && i < values.size(); ++i) {
                if (frequencies[i] < low) continue;
                const QPointF point(xOf(frequencies[i]), plot.bottom() - fraction(values[i]) * plot.height());
                if (!started) { path.moveTo(point); started = true; }
                else path.lineTo(point);
            }
            return path;
        };
        painter.setRenderHint(QPainter::Antialiasing);
        painter.save();
        painter.setClipRect(plot);
        if (!display_.curvePeak.empty()) {
            painter.setPen(QPen(palette_.peakTrace, 1.0));
            painter.setBrush(Qt::NoBrush);
            painter.drawPath(trace(display_.curvePeak));
        }
        auto line = trace(display_.curve);
        if (!line.isEmpty()) {
            QPainterPath fill = line;
            fill.lineTo(line.currentPosition().x(), plot.bottom());
            fill.lineTo(line.elementAt(0).x, plot.bottom());
            fill.closeSubpath();
            QLinearGradient gradient(plot.topLeft(), plot.bottomLeft());
            gradient.setColorAt(0, palette_.fillTop);
            gradient.setColorAt(1, palette_.fillBottom);
            painter.setPen(Qt::NoPen);
            painter.setBrush(gradient);
            painter.drawPath(fill);
            painter.setPen(QPen(palette_.curveLine, 1.3));
            painter.setBrush(Qt::NoBrush);
            painter.drawPath(line);
        }
        painter.restore();
    }

    const LiveSpectrumDisplay& display_;
    const SpectrumPalette& palette_;
};

// The on-screen analyzer; it only reads the window's display state.
class SpectrumCanvas final : public QWidget {
public:
    explicit SpectrumCanvas(const LiveSpectrumDisplay& display, QWidget* parent) : QWidget(parent), display_(display)
    {
        setMinimumSize(480, 260);
        setAttribute(Qt::WA_OpaquePaintEvent);
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        SpectrumPainter(display_, kScreen).paint(painter, QRectF(rect()));
    }

private:
    const LiveSpectrumDisplay& display_;
};

} // namespace

void paintLiveSpectrum(QPainter& painter, const QRectF& area, const LiveSpectrumDisplay& display, bool paper)
{
    SpectrumPainter(display, paper ? kPaper : kScreen).paint(painter, area);
}

LiveSpectrumWindow::LiveSpectrumWindow(QString id) : id_(std::move(id))
{
    setWindowTitle(QStringLiteral("Live spectrum — %1").arg(id_));
    resize(860, 420);
    auto* layout = new QVBoxLayout(this);
    auto* controls = new QHBoxLayout;
    layout->addLayout(controls);

    controls->addWidget(new QLabel(QStringLiteral("Style:"), this));
    style_ = new QButtonGroup(this);
    barsStyle_ = new QRadioButton(QStringLiteral("Bars"), this); barsStyle_->setObjectName("spectrum.style.bars");
    curveStyle_ = new QRadioButton(QStringLiteral("Curve"), this); curveStyle_->setObjectName("spectrum.style.curve");
    style_->addButton(barsStyle_); style_->addButton(curveStyle_);
    barsStyle_->setChecked(true);
    controls->addWidget(barsStyle_); controls->addWidget(curveStyle_);
    controls->addSpacing(16);

    controls->addWidget(new QLabel(QStringLiteral("Frequency:"), this));
    scale_ = new QButtonGroup(this);
    logScale_ = new QRadioButton(QStringLiteral("Log"), this); logScale_->setObjectName("spectrum.scale.log");
    linearScale_ = new QRadioButton(QStringLiteral("Linear"), this); linearScale_->setObjectName("spectrum.scale.linear");
    scale_->addButton(logScale_); scale_->addButton(linearScale_);
    logScale_->setChecked(true);
    controls->addWidget(logScale_); controls->addWidget(linearScale_);
    controls->addSpacing(16);

    controls->addWidget(new QLabel(QStringLiteral("Bars:"), this));
    bars_ = new QSpinBox(this); bars_->setObjectName("spectrum.bars");
    bars_->setRange(4, 256); bars_->setValue(32);
    controls->addWidget(bars_);
    controls->addWidget(new QLabel(QStringLiteral("FFT:"), this));
    fftSize_ = new QComboBox(this); fftSize_->setObjectName("spectrum.fft");
    for (const int size : {512, 1024, 2048, 4096, 8192, 16384}) fftSize_->addItem(QString::number(size), size);
    fftSize_->setCurrentText(QStringLiteral("4096"));
    controls->addWidget(fftSize_);
    controls->addWidget(new QLabel(QStringLiteral("Floor (dB):"), this));
    floor_ = new QSpinBox(this); floor_->setObjectName("spectrum.floor");
    floor_->setRange(-160, -20); floor_->setSingleStep(10); floor_->setValue(-90);
    controls->addWidget(floor_);
    peakHold_ = new QCheckBox(QStringLiteral("Peak hold"), this); peakHold_->setObjectName("spectrum.peakHold");
    peakHold_->setChecked(true);
    controls->addWidget(peakHold_);
    controls->addStretch();
    // Freeze holds the picture while the source keeps acquiring; print
    // draws a copy of the frozen or current picture on white paper (P06).
    freeze_ = makeFreezeButton(this, QStringLiteral("spectrum"));
    controls->addWidget(freeze_);
    controls->addWidget(makePrintButton(this, QStringLiteral("spectrum"), [this]() -> std::optional<ViewerPrintSnapshot> {
        if (display_.spectrum.frequencies.empty()) return std::nullopt;
        ViewerPrintSnapshot snapshot;
        snapshot.title = windowTitle();
        snapshot.notes << QStringLiteral("Live snapshot at %1%2; acquisition continues.")
            .arg(QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss.zzz")),
                 freeze_->isChecked() ? QStringLiteral(" of the frozen display") : QString());
        snapshot.notes << QStringLiteral("%1, %2 frequency axis, %3-point FFT (Hann), %4 Hz bins, amplitude in dBFS%5.")
            .arg(display_.style == LiveSpectrumDisplay::Style::Bars ? QStringLiteral("%1 bars (strongest bin per band)").arg(display_.bars.size())
                                                                    : QStringLiteral("curve"),
                 display_.iq || !display_.logFrequency ? QStringLiteral("linear") : QStringLiteral("log"))
            .arg(display_.spectrum.frequencies.size() > 1 ? (display_.iq ? display_.spectrum.frequencies.size() : (display_.spectrum.frequencies.size() - 1) * 2) : 0)
            .arg(QString::number(display_.spectrum.binSpacing, 'g', 5),
                 peakHold_->isChecked() ? QStringLiteral(", peak hold on") : QString());
        if (!status_->text().trimmed().isEmpty()) snapshot.notes << status_->text().simplified();
        snapshot.draw = [display = display_](QPainter& painter, const QRectF& area) { paintLiveSpectrum(painter, area, display, true); };
        return snapshot;
    }, [this] { return QStringLiteral("live-spectrum-%1").arg(id_); }));

    canvas_ = new SpectrumCanvas(display_, this);
    layout->addWidget(canvas_, 1);
    status_ = new QLabel(this);
    status_->setObjectName("spectrum.status");
    layout->addWidget(status_);

    connect(style_, &QButtonGroup::buttonClicked, this, [this](QAbstractButton* button) {
        display_.style = button == curveStyle_ ? LiveSpectrumDisplay::Style::Curve : LiveSpectrumDisplay::Style::Bars;
        setProperty("styleChosen", true);
        canvas_->update();
    });
    connect(scale_, &QButtonGroup::buttonClicked, this, [this](QAbstractButton* button) {
        scaleChoice_ = button == logScale_ ? QStringLiteral("log") : QStringLiteral("linear");
        resetDisplay();
    });
    connect(bars_, &QSpinBox::valueChanged, this, [this] { resetDisplay(); });
    connect(fftSize_, &QComboBox::currentIndexChanged, this, [this] {
        if (source_) static_cast<void>(source_->setCapacity(std::max<std::size_t>(source_->capacity(), static_cast<std::size_t>(fftSize_->currentData().toInt()))));
        resetDisplay();
    });
    connect(floor_, &QSpinBox::valueChanged, this, [this](int value) { display_.floorDb = value; canvas_->update(); });
    connect(peakHold_, &QCheckBox::toggled, this, [this] { resetDisplay(); });

    timer_ = new QTimer(this);
    timer_->setInterval(kRefreshMs);
    connect(timer_, &QTimer::timeout, this, &LiveSpectrumWindow::updateSpectrum);
    timer_->start();
}

LiveSpectrumWindow::~LiveSpectrumWindow() = default;

ViewerType LiveSpectrumWindow::viewerType()
{
    return {QStringLiteral("LiveSpectrum"), {}, QStringLiteral("N→spectrum"), {}, {},
            [](const ViewerInstanceSpec& spec) -> ObservationViewer* { return new LiveSpectrumWindow(spec.instanceId); }};
}

bool LiveSpectrumWindow::iqInput() const
{
    if (input_ == QStringLiteral("iq")) return true;
    if (input_ == QStringLiteral("real") || !source_) return false;
    return source_->channels().size() >= 2;
}

analysis::LiveSpectrumSettings LiveSpectrumWindow::settings() const
{
    analysis::LiveSpectrumSettings result;
    result.fftSize = fftSize_->currentData().toInt();
    result.bars = bars_->value();
    result.iq = display_.iq;
    result.logFrequency = display_.logFrequency;
    return result;
}

void LiveSpectrumWindow::applyParameters(const QJsonObject& parameters)
{
    if (const auto value = parameters.value(QStringLiteral("bars")); value.isDouble()) bars_->setValue(value.toInt());
    if (const auto value = parameters.value(QStringLiteral("fft_size")); value.isDouble()) {
        const auto index = fftSize_->findData(value.toInt());
        if (index >= 0) fftSize_->setCurrentIndex(index);
    }
    if (const auto value = parameters.value(QStringLiteral("floor_db")); value.isDouble()) floor_->setValue(value.toInt());
    if (const auto value = parameters.value(QStringLiteral("centre_hz")); value.isDouble()) display_.centreHz = value.toDouble();
    if (const auto value = parameters.value(QStringLiteral("input")).toString(); !value.isEmpty()) input_ = value;
    if (const auto value = parameters.value(QStringLiteral("frequency_scale")).toString(); !value.isEmpty()) scaleChoice_ = value;
    if (const auto value = parameters.value(QStringLiteral("style")).toString(); value == "bars" || value == "curve") {
        (value == "curve" ? curveStyle_ : barsStyle_)->setChecked(true);
        display_.style = value == "curve" ? LiveSpectrumDisplay::Style::Curve : LiveSpectrumDisplay::Style::Bars;
        setProperty("styleChosen", true);
    }
    resetDisplay();
}

void LiveSpectrumWindow::attachLiveSource(std::shared_ptr<observation::LiveAcquisition> source)
{
    source_ = std::move(source);
    if (source_) {
        static_cast<void>(source_->setCapacity(std::max<std::size_t>(source_->capacity(), static_cast<std::size_t>(fftSize_->currentData().toInt()))));
        // One real channel defaults to analyzer bars; I/Q to an SDR-style curve.
        if (style_->checkedButton() == barsStyle_ && iqInput() && !property("styleChosen").toBool()) {
            curveStyle_->setChecked(true);
            display_.style = LiveSpectrumDisplay::Style::Curve;
        }
    }
    resetDisplay();
}

void LiveSpectrumWindow::clearSamples() { resetDisplay(); }

void LiveSpectrumWindow::resetDisplay()
{
    display_.iq = iqInput();
    display_.logFrequency = !display_.iq && scaleChoice_ != QStringLiteral("linear");
    logScale_->setEnabled(!display_.iq);  // a two-sided spectrum has negative offsets
    (display_.logFrequency ? logScale_ : linearScale_)->setChecked(true);
    display_.floorDb = floor_->value();
    display_.spectrum = {};
    display_.bars.clear(); display_.barPeaks.clear(); display_.barPeakAge.clear();
    display_.curve.clear(); display_.curvePeak.clear();
    display_.message = source_ ? QStringLiteral("Waiting for live samples") : QStringLiteral("Run the model to see its live spectrum");
    clock_.invalidate();
    canvas_->update();
}

void LiveSpectrumWindow::updateSpectrum()
{
    if (!source_ || !isVisible() || (freeze_ && freeze_->isChecked())) return;
    const auto frame = source_->snapshot();
    if (!frame) return;
    if (!frame->error.isEmpty()) {
        display_.message = frame->error;
        status_->setText(frame->error);
        canvas_->update();
        return;
    }
    auto result = analyzer_.compute(*frame, settings());
    if (!result) return;
    const double dt = clock_.isValid() ? std::clamp(clock_.restart() / 1000.0, 0.0, 0.5) : (clock_.start(), 0.0);
    const bool hold = peakHold_->isChecked();

    // Bars: instant attack, steady fall; peak caps hold, then drop.
    const auto& levels = result->barDecibels;
    if (display_.bars.size() != levels.size()) {
        display_.bars = levels;
        display_.barPeaks = hold ? levels : std::vector<double>{};
        display_.barPeakAge.assign(levels.size(), 0);
    } else {
        for (std::size_t i = 0; i < levels.size(); ++i) {
            display_.bars[i] = std::max(levels[i], display_.bars[i] - kBarFallDbPerSecond * dt);
            if (!hold) continue;
            if (display_.bars[i] >= display_.barPeaks[i]) { display_.barPeaks[i] = display_.bars[i]; display_.barPeakAge[i] = 0; }
            else if ((display_.barPeakAge[i] += dt) > kPeakHoldSeconds) display_.barPeaks[i] -= kPeakFallDbPerSecond * dt;
        }
    }
    // Curve: light smoothing in dB; the peak trace decays slowly.
    const auto& bins = result->decibels;
    if (display_.curve.size() != bins.size()) {
        display_.curve = bins;
        display_.curvePeak = hold ? bins : std::vector<double>{};
    } else {
        for (std::size_t i = 0; i < bins.size(); ++i) {
            display_.curve[i] = 0.5 * display_.curve[i] + 0.5 * bins[i];
            if (hold) display_.curvePeak[i] = std::max(display_.curve[i], display_.curvePeak[i] - kCurvePeakFallDbPerSecond * dt);
        }
    }
    status_->setText(QStringLiteral("%1 • %2 Hz sample rate • %3 Hz per bin • %4-point FFT%5")
        .arg(display_.iq ? QStringLiteral("I/Q, two-sided") : QStringLiteral("Real, one-sided"))
        .arg(result->sampleRate, 0, 'g', 7).arg(result->binSpacing, 0, 'g', 4).arg(bins.size() * (display_.iq ? 1 : 2) - (display_.iq ? 0 : 2))
        .arg(frame->finished ? QStringLiteral(" • run finished") : QString()));
    display_.spectrum = std::move(*result);
    canvas_->update();
}

} // namespace s7studio::viewers
