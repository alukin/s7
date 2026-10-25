// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-S7-plugin-permission
#include "viewers/spectrum_view.hpp"
#include "viewers/line_plot_view.hpp"
#include "instrument_controls.hpp"
#include <QtGraphs/QLineSeries>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QScrollArea>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QProgressBar>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTimer>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>

namespace s7studio::viewers {
using namespace analysis;
SpectrumView::SpectrumView(QWidget* parent) : QWidget(parent)
{
    setObjectName("live.spectrum");
    auto* layout = new QVBoxLayout(this); layout->setContentsMargins(0, 0, 0, 0);
    auto* box = new QWidget(this);
    auto* boxLayout = new QVBoxLayout(box);
    auto* fields = new ui::WrappingSettings(box); boxLayout->addWidget(fields);
    auto combo = [&](const char* name, const QStringList& items) {
        auto* control = new QComboBox(box); control->setObjectName(name); control->addItems(items);
        control->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon); control->setMinimumContentsLength(8);
        return control;
    };
    auto number = [&](const char* name, double low, double high, double value, int decimals = 6) {
        auto* control = new QDoubleSpinBox(box); control->setObjectName(name); control->setDecimals(decimals);
        control->setRange(low, high); control->setValue(value); control->setMaximumWidth(180); return control;
    };
    auto field = [&](const QString& label, QWidget* control) { fields->addField(label, control); };
    mode_ = combo("spectrum.mode", {"Real (one-sided)", "I/Q (two-sided)"});
    channel_ = combo("spectrum.channel", {}); quadrature_ = combo("spectrum.quadrature", {});
    declaredUnit_ = new QLineEdit(box); declaredUnit_->setObjectName("spectrum.unit");
    declaredUnit_->setPlaceholderText("Optional explicit I/Q unit"); declaredUnit_->setMaximumWidth(210);
    declaredUnit_->setToolTip("Declares a shared unit for the two raw channels. Does not convert or calibrate their values.");
    field("Input", mode_); field("Source / I", channel_); field("Q", quadrature_); field("I/Q unit", declaredUnit_);
    size_ = combo("spectrum.size", {});
    for (int n = 64; n <= 65536; n *= 2) size_->addItem(QString::number(n), n);
    size_->setCurrentIndex(size_->findData(4096));
    window_ = combo("spectrum.window", {"Rectangular", "Periodic Hann", "Periodic Hamming", "Periodic Blackman", "Periodic Blackman-Harris"}); window_->setCurrentIndex(1);
    removeMean_ = new QCheckBox("Remove mean", box); removeMean_->setObjectName("spectrum.removeMean");
    frequencyOffset_ = number("spectrum.frequencyOffset", -1e12, 1e12, 0); frequencyOffset_->setSuffix(" Hz");
    frequencyOffset_->setToolTip("Frequency-axis offset only; does not change the model rate or FFT.");
    field("FFT N", size_); field("Window", window_); fields->addControl(removeMean_); field("Axis offset", frequencyOffset_);
    quantity_ = combo("spectrum.quantity", {"Peak amplitude", "Power / bin", "PSD"});
    quantity_->setToolTip("Power / bin is window-normalized mean-square power; PSD is power per Hz. Tone amplitude has a separate coherent-gain normalization.");
    average_ = combo("spectrum.average", {"None", "Block mean (K spectra)", "Exponential power mean"});
    count_ = new QSpinBox(box); count_->setObjectName("spectrum.averageCount"); count_->setRange(1, 128); count_->setValue(8);
    alpha_ = number("spectrum.alpha", .000001, 1, .2);
    field("Quantity", quantity_); field("Averaging", average_); field("K", count_); field("Alpha", alpha_);
    scale_ = combo("spectrum.scale", {"Linear", "dB re explicit reference"});
    reference_ = number("spectrum.reference", 1e-12, 1e12, 1, 12);
    reference_->setToolTip("Positive reference in the chosen linear quantity's units. No implicit dBFS or dBm calibration.");
    dbOffset_ = number("spectrum.dbOffset", -300, 300, 0, 2); dbOffset_->setSuffix(" dB");
    floor_ = number("spectrum.floor", -600, 0, -180, 1); floor_->setSuffix(" dB");
    field("Scale", scale_); field("Reference", reference_); field("Display offset", dbOffset_); field("Display floor", floor_);
    peakHold_ = new QCheckBox("Peak hold", box); peakHold_->setObjectName("spectrum.peakHold"); fields->addControl(peakHold_);
    auto* reset = new QPushButton("Reset averages / hold", box); reset->setObjectName("spectrum.reset"); fields->addControl(reset);
    marker_ = combo("spectrum.marker", {"Peak bin", "Fixed bin"});
    fixedBin_ = new QSpinBox(box); fixedBin_->setObjectName("spectrum.fixedBin"); fixedBin_->setRange(0, 65535);
    field("Marker", marker_); field("Bin", fixedBin_);
    auto* settingsScroll = new QScrollArea(this); settingsScroll->setObjectName("spectrum.settingsScroll"); settingsScroll->setWidgetResizable(true);
    settingsScroll->setFrameShape(QFrame::NoFrame); settingsScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    settingsScroll->setMaximumHeight(220); settingsScroll->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Maximum); settingsScroll->setWidget(box);
    auto* section = new ui::CollapsibleSection("Spectrum settings", "spectrum.settings", settingsScroll, true, this);
    section->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Maximum); layout->addWidget(section);
    busy_ = new QProgressBar(this); busy_->setObjectName("spectrum.processing"); busy_->setRange(0, 0); busy_->setTextVisible(false);
    busy_->setMaximumHeight(12); busy_->setAccessibleName("Processing spectrum"); busy_->hide(); layout->addWidget(busy_);
    auto busyPolicy = busy_->sizePolicy(); busyPolicy.setRetainSizeWhenHidden(true); busy_->setSizePolicy(busyPolicy);
    plot_ = new LinePlotView(this); plot_->setObjectName("spectrum.plot"); plot_->setLegendVisible(true);
    plot_->bridge()->inspectionEnabled = true;
    plot_->addSeries("Spectrum", {QColor("#1565c0")}); plot_->addSeries("Peak hold", {QColor("#c25e00")});
    layout->addWidget(plot_, 1);
    markerText_ = new QLabel(this); markerText_->setObjectName("spectrum.markerText"); markerText_->setTextFormat(Qt::PlainText); markerText_->setWordWrap(true); layout->addWidget(markerText_);
    readout_ = new QLabel("Hover over a spectrum bin to inspect its computed value.", this);
    readout_->setObjectName("spectrum.readout"); readout_->setTextFormat(Qt::PlainText); readout_->setWordWrap(true);
    readout_->setTextInteractionFlags(Qt::TextSelectableByMouse); layout->addWidget(readout_);
    status_ = new QLabel(this); status_->setObjectName("spectrum.status"); status_->setTextFormat(Qt::PlainText); status_->setWordWrap(true); layout->addWidget(status_);
    for (auto* control : {mode_, channel_, quadrature_, size_, window_, quantity_, average_})
        connect(control, &QComboBox::currentIndexChanged, this, [this] { reconfigure(); });
    connect(removeMean_, &QCheckBox::toggled, this, [this] { reconfigure(); });
    connect(peakHold_, &QCheckBox::toggled, this, [this] { reconfigure(); });
    connect(count_, &QSpinBox::valueChanged, this, [this] { reconfigure(); });
    connect(alpha_, &QDoubleSpinBox::valueChanged, this, [this] { reconfigure(); });
    connect(declaredUnit_, &QLineEdit::editingFinished, this, [this] { reconfigure(); });
    connect(reset, &QPushButton::clicked, this, [this] { reconfigure(); });
    for (auto* control : {scale_, marker_}) connect(control, &QComboBox::currentIndexChanged, this, [this] { render(true); });
    for (auto* control : {reference_, dbOffset_, floor_, frequencyOffset_})
        connect(control, &QDoubleSpinBox::valueChanged, this, [this] { render(true); });
    connect(fixedBin_, &QSpinBox::valueChanged, this, [this] { render(false); });
    connect(plot_, &LinePlotView::xRangeChanged, this, [this] { render(false); });
    connect(plot_, &LinePlotView::hovered, this, [this](double frequency, bool active) { inspect(frequency, active); });
    connect(scale_, &QComboBox::currentIndexChanged, this, &SpectrumView::inspectionInvalidated);
    for (auto* display : {reference_, dbOffset_, floor_, frequencyOffset_})
        connect(display, &QDoubleSpinBox::valueChanged, this, &SpectrumView::inspectionInvalidated);
    pollTimer_ = new QTimer(this); pollTimer_->setInterval(16); connect(pollTimer_, &QTimer::timeout, this, &SpectrumView::poll);
    reconfigure();
}
SpectrumView::~SpectrumView()
{
    worker_.cancel(); disconnect(plot_, nullptr, this, nullptr); delete plot_;
}
SpectrumSettings SpectrumView::settings() const
{
    SpectrumSettings s;
    s.size = static_cast<std::size_t>(size_->currentData().toInt());
    s.channel = static_cast<std::size_t>(std::max(0, channel_->currentIndex()));
    s.quadrature = static_cast<std::size_t>(std::max(0, quadrature_->currentIndex())); s.iq = mode_->currentIndex() == 1;
    s.window = static_cast<SpectrumWindow>(window_->currentIndex()); s.quantity = static_cast<SpectrumQuantity>(quantity_->currentIndex());
    s.average = static_cast<SpectrumAverage>(average_->currentIndex()); s.averageCount = static_cast<std::size_t>(count_->value()); s.alpha = alpha_->value();
    s.removeMean = removeMean_->isChecked(); s.peakHold = peakHold_->isChecked(); s.declaredUnit = declaredUnit_->text(); return s;
}
void SpectrumView::reconfigure()
{
    const bool iq = mode_->currentIndex() == 1, power = quantity_->currentIndex() != 0;
    quadrature_->setEnabled(iq); declaredUnit_->setEnabled(iq); average_->setEnabled(power);
    if (!power) { const QSignalBlocker blocker(average_); average_->setCurrentIndex(0); }
    count_->setEnabled(power && average_->currentIndex() == 1); alpha_->setEnabled(power && average_->currentIndex() == 2);
    worker_.cancel(); ++revision_; result_.reset();
    plot_->bridge()->cursorVisible = false; emit plot_->bridge()->inspectionChanged();
    render(true); submit();
    emit inspectionInvalidated();
}
void SpectrumView::setFrame(std::shared_ptr<const observation::LiveFrame> frame)
{
    if (!frame) return; // The host supplies its pinned display frame while frozen.
    const bool same = frame_ && frame_->runId == frame->runId && frame_->generation == frame->generation && frame_->received == frame->received;
    const bool newRun = frame_ && (frame_->runId != frame->runId || frame_->generation != frame->generation);
    QStringList names;
    for (const auto& channel : frame->channels) names.push_back(channel.name + " [" + (channel.unit.isEmpty() ? "unit unspecified" : channel.unit) + "]");
    QStringList existing; for (int i = 0; i < channel_->count(); ++i) existing.push_back(channel_->itemText(i));
    if (names != existing) {
        const QSignalBlocker iBlock(channel_), qBlock(quadrature_);
        const int i = channel_->currentIndex(), q = quadrature_->currentIndex();
        channel_->clear(); channel_->addItems(names); quadrature_->clear(); quadrature_->addItems(names);
        if (!names.isEmpty()) {
            channel_->setCurrentIndex(std::clamp(i, 0, static_cast<int>(names.size())-1));
            quadrature_->setCurrentIndex(std::clamp(q < 0 ? 1 : q, 0, static_cast<int>(names.size())-1));
        }
    }
    frame_ = std::move(frame);
    if (newRun) reconfigure(); else if (!same) submit();
}
void SpectrumView::submit()
{
    if (suspended_ || !isVisible() || !frame_) { updateStatus(); return; }
    ++submitted_; worker_.submit(frame_, settings(), revision_); updateStatus();
}
void SpectrumView::poll()
{
    if (suspended_ || !isVisible() || inspecting_) return;
    if (auto result = worker_.takeResult()) {
        if (!frame_ || result->revision != revision_ || result->runId != frame_->runId || result->generation != frame_->generation) return;
        result_ = std::move(*result); render(true);
    }
    updateStatus();
}
void SpectrumView::clear()
{
    worker_.cancel(); ++revision_; frame_.reset(); result_.reset(); frozen_ = false; inspecting_ = false;
    emit inspectingChanged(false); render(true);
}
std::shared_ptr<const observation::LiveFrame> SpectrumView::setFrozen(bool frozen)
{
    frozen_ = frozen;
    if (frozen) {
        worker_.cancel();
        if (result_ && result_->frame) frame_ = result_->frame;
        else submit();
    }
    updateStatus(); return frame_;
}
void SpectrumView::showEvent(QShowEvent* event) { QWidget::showEvent(event); if (!suspended_) pollTimer_->start(); submit(); }
void SpectrumView::hideEvent(QHideEvent* event)
{
    pollTimer_->stop(); worker_.cancel(); inspecting_ = false; emit inspectingChanged(false); QWidget::hideEvent(event);
}
void SpectrumView::setSuspended(bool suspended)
{
    if (suspended_ == suspended) return;
    suspended_ = suspended;
    if (suspended) { pollTimer_->stop(); worker_.cancel(); }
    else if (isVisible()) { pollTimer_->start(); submit(); }
}
QString SpectrumView::quantityUnit() const
{
    const auto unit = !result_ || result_->unit.isEmpty() ? QStringLiteral("[unit unspecified]") : result_->unit;
    switch (settings().quantity) {
    case SpectrumQuantity::Amplitude: return unit;
    case SpectrumQuantity::Power: return unit + "²";
    case SpectrumQuantity::Psd: return unit + "²/Hz";
    }
    return {};
}
QString SpectrumView::binText(std::size_t i) const
{
    if (!result_ || i >= result_->values.size()) return {};
    QString text = QStringLiteral("Bin %1 • %2 Hz baseband").arg(i).arg(result_->frequencies[i], 0, 'g', 17);
    if (frequencyOffset_->value() != 0) text += QStringLiteral(" • axis %1 Hz").arg(result_->frequencies[i]+frequencyOffset_->value(), 0, 'g', 17);
    text += QStringLiteral(" • %1 %2").arg(result_->values[i], 0, 'g', 17).arg(quantityUnit());
    if (scale_->currentIndex() == 1) {
        const double db = spectrumDecibels(result_->values[i], result_->settings.quantity, reference_->value());
        text += QStringLiteral(" • %1 dB re %2 %3 • display offset %4 dB").arg(db, 0, 'g', 17)
            .arg(reference_->value(), 0, 'g', 17).arg(quantityUnit()).arg(dbOffset_->value());
    }
    return text;
}
void SpectrumView::render(bool fit)
{
    if (rendering_) return;
    rendering_ = true;
    const bool db = scale_->currentIndex() == 1;
    for (auto* control : {reference_, dbOffset_, floor_}) control->setEnabled(db);
    fixedBin_->setEnabled(marker_->currentIndex() == 1);
    plot_->setAxisTitles("Frequency (Hz)", db ? QStringLiteral("dB re %1 %2")
        .arg(reference_->value()).arg(quantityUnit()) : quantity_->currentText() + " (" + quantityUnit() + ")");
    plot_->setSeriesVisible(plot_->series()[1], result_ && !result_->peakHold.empty());
    for (auto* series : plot_->series()) LinePlotView::setPoints(series, {});
    if (result_ && result_->state == SpectrumResult::State::Ready) {
        const auto& result = *result_;
        if (fit) plot_->setXRange(result.frequencies.front()+frequencyOffset_->value(), result.frequencies.back()+frequencyOffset_->value());
        fixedBin_->setMaximum(static_cast<int>(result.values.size()-1));
        auto points = [&](const std::vector<double>& values) {
            QList<QPointF> reduced;
            const auto buckets = static_cast<std::size_t>(std::max(64, plot_->width()));
            const auto bucket = std::max<std::size_t>(1, (values.size()+buckets-1)/buckets);
            for (std::size_t first = 0; first < values.size(); first += bucket) {
                const auto end = std::min(values.size(), first+bucket);
                auto lo = first, hi = first;
                for (auto i = first+1; i < end; ++i) { if (values[i] < values[lo]) lo = i; if (values[i] > values[hi]) hi = i; }
                std::size_t indices[]{first, lo, hi, end-1}; std::sort(std::begin(indices), std::end(indices));
                auto previous = values.size();
                for (const auto i : indices) if (i != previous) {
                    const double y = db ? std::max(floor_->value(), spectrumDecibels(values[i], result.settings.quantity, reference_->value())+dbOffset_->value()) : values[i];
                    reduced.push_back({result.frequencies[i]+frequencyOffset_->value(), y}); previous = i;
                }
            }
            return reduced;
        };
        LinePlotView::setPoints(plot_->series()[0], points(result.values));
        LinePlotView::setPoints(plot_->series()[1], points(result.peakHold));
        plot_->setSeriesName(plot_->series()[0], channel_->currentText() + (result.settings.iq ? " + iQ" : ""));
        if (fit) plot_->fitYToVisibleData();
        const auto index = marker_->currentIndex() == 1 ? static_cast<std::size_t>(fixedBin_->value()) :
            static_cast<std::size_t>(std::max_element(result.values.begin(), result.values.end())-result.values.begin());
        markerText_->setText((marker_->currentIndex() == 1 ? "Fixed marker • " : "Peak marker • ") + binText(index));
    } else markerText_->clear();
    if (!inspecting_) { readout_->setText("Hover over a spectrum bin to inspect its computed value."); plot_->bridge()->cursorVisible = false; emit plot_->bridge()->inspectionChanged(); }
    rendering_ = false; updateStatus();
}
QString SpectrumView::inspect(double frequency, bool active)
{
    inspecting_ = active && result_ && result_->state == SpectrumResult::State::Ready;
    plot_->bridge()->cursorVisible = false;
    if (inspecting_) {
        const auto baseband = frequency-frequencyOffset_->value();
        const auto& frequencies = result_->frequencies;
        if (baseband >= frequencies.front() && baseband <= frequencies.back()) {
            auto index = static_cast<std::size_t>(std::lower_bound(frequencies.begin(), frequencies.end(), baseband)-frequencies.begin());
            if (index && baseband-frequencies[index-1] <= frequencies[index]-baseband) --index;
            plot_->bridge()->cursorVisible = true; plot_->bridge()->cursorX = frequencies[index]+frequencyOffset_->value();
            readout_->setText(QStringLiteral("Computed spectrum • run %1 • steps %2–%3 • ").arg(result_->runId).arg(result_->firstStep).arg(result_->lastStep) + binText(index));
        } else readout_->setText("No FFT bin at this frequency.");
    } else readout_->setText("Hover over a spectrum bin to inspect its computed value.");
    emit plot_->bridge()->inspectionChanged(); emit inspectingChanged(inspecting_); updateStatus(); return readout_->text();
}
std::optional<analysis::WindowSnapshot> SpectrumView::inspectionSnapshot() const
{
    if (!result_ || result_->state != SpectrumResult::State::Ready) return {};
    auto data = analysis::spectrumWindowSnapshot(*result_);
    for (auto& x : data.displayX) x += frequencyOffset_->value();
    data.displayAxisName = "frequency_axis"; data.displayAxisUnit = "Hz";
    data.settings.insert("display", QJsonObject{{"db", scale_->currentIndex() == 1}, {"reference", analysis::exactNumber(reference_->value())},
        {"db_offset", analysis::exactNumber(dbOffset_->value())}, {"floor_db", analysis::exactNumber(floor_->value())},
        {"frequency_offset_hz", analysis::exactNumber(frequencyOffset_->value())}});
    return data;
}
void SpectrumView::updateStatus()
{
    busy_->setVisible(worker_.busy());
    QString text;
    if (!frame_) text = "Waiting for live acquisition.";
    else if (!result_) text = worker_.busy() ? "Computing spectrum…" : "Spectrum inactive.";
    else if (result_->state != SpectrumResult::State::Ready) text = result_->message;
    else text = QStringLiteral("Run %1 • steps %2–%3 • N=%4 • Fs=%5 Hz • Δf=%6 Hz • coherent gain=%7 • ENBW=%8 Hz • averaged %9 • skipped hops %10%11%12")
        .arg(result_->runId).arg(result_->firstStep).arg(result_->lastStep).arg(result_->size)
        .arg(result_->sampleRate, 0, 'g', 12).arg(result_->binSpacing, 0, 'g', 12).arg(result_->coherentGain, 0, 'g', 10).arg(result_->enbw, 0, 'g', 10)
        .arg(result_->averaged).arg(result_->skippedHops).arg(inspecting_ ? " • Inspecting displayed spectrum" : frozen_ ? " • Frozen spectrum" : "")
        .arg(result_->message.isEmpty() ? QString() : " • " + result_->message);
    if (worker_.busy() && result_) text += " • Updating (showing preceding result)";
    if (result_ && result_->state == SpectrumResult::State::Ready && result_->settings.average != SpectrumAverage::None)
        text += QStringLiteral(" • average coverage steps %1–%2").arg(result_->averageFirstStep).arg(result_->lastStep);
    if (result_ && !result_->peakHold.empty()) text += QStringLiteral(" • hold coverage steps %1–%2").arg(result_->holdFirstStep).arg(result_->lastStep);
    if (worker_.replacedRequests()) text += QStringLiteral(" • replaced pending requests %1").arg(worker_.replacedRequests());
    status_->setText(text);
}
}
