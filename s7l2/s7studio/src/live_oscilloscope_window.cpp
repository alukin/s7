// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-S7-plugin-permission
#include "live_oscilloscope_window.hpp"
#include "viewers/line_plot_view.hpp"
#include <QtGraphs/QLineSeries>
#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QGridLayout>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>

namespace s7studio::ui {
LiveOscilloscopeWindow::LiveOscilloscopeWindow(QString id)
{
    setObjectName("liveOscilloscope");
    setWindowTitle(QStringLiteral("Live oscilloscope — %1 (prototype)").arg(id));
    resize(1100, 740);
    auto* layout = new QVBoxLayout(this);
    auto* settings = new QGroupBox(QStringLiteral("Acquisition settings — bounded RAM, no full-run recording"), this);
    auto* row = new QHBoxLayout(settings);
    row->addWidget(new QLabel(QStringLiteral("Retain samples / channel"), settings));
    capacity_ = new QSpinBox(settings); capacity_->setObjectName("live.samples");
    capacity_->setRange(2, static_cast<int>(observation::LiveAcquisition::maxPositions)); capacity_->setValue(4096);
    capacity_->setToolTip(QStringLiteral("Changes retention during this run. Growing fills forward; evicted history cannot return. Does not change model rate."));
    row->addWidget(capacity_);
    row->addWidget(new QLabel(QStringLiteral("or duration (s)"), settings));
    seconds_ = new QLineEdit(settings); seconds_->setObjectName("live.seconds"); seconds_->setMaximumWidth(140);
    seconds_->setToolTip("Positive model seconds, including scientific notation. Rounded up to complete sample intervals.");
    row->addWidget(seconds_);
    duration_ = new QLabel(QStringLiteral("Start a run to see model duration"), settings); duration_->setObjectName("live.duration");
    row->addWidget(duration_, 1);
    layout->addWidget(settings);
    auto* toolbar = new QHBoxLayout;
    freeze_ = new QPushButton(QStringLiteral("Freeze display"), this); freeze_->setObjectName("live.freeze"); freeze_->setCheckable(true);
    toolbar->addWidget(freeze_);
    auto* follow = new QPushButton(QStringLiteral("Resume live / Fit"), this); follow->setObjectName("live.follow"); toolbar->addWidget(follow);
    autoY_ = new QCheckBox(QStringLiteral("Auto Y"), this); autoY_->setObjectName("live.autoY"); autoY_->setChecked(true); toolbar->addWidget(autoY_);
    toolbar->addStretch(); layout->addLayout(toolbar);
    auto* channelBox = new QGroupBox(QStringLiteral("Channels — display only; raw values remain unchanged"), this);
    auto* grid = new QGridLayout(channelBox);
    grid->addWidget(new QLabel("Channel / supplied unit", channelBox), 0, 0);
    grid->addWidget(new QLabel("Display gain", channelBox), 0, 1);
    grid->addWidget(new QLabel("Display offset", channelBox), 0, 2);
    for (int i = 0; i < 8; ++i) {
        auto* visible = new QCheckBox(channelBox); visible->setChecked(true);
        auto* gain = new QDoubleSpinBox(channelBox); gain->setDecimals(6); gain->setRange(.000001, 1e6); gain->setValue(1);
        auto* offset = new QDoubleSpinBox(channelBox); offset->setDecimals(6); offset->setRange(-1e12, 1e12);
        visible->setObjectName(QStringLiteral("live.channel.%1.visible").arg(i));
        gain->setObjectName(QStringLiteral("live.channel.%1.gain").arg(i));
        offset->setObjectName(QStringLiteral("live.channel.%1.offset").arg(i));
        controls_.push_back({visible, gain, offset});
        grid->addWidget(visible, i+1, 0); grid->addWidget(gain, i+1, 1); grid->addWidget(offset, i+1, 2);
        visible->hide(); gain->hide(); offset->hide();
        connect(visible, &QCheckBox::toggled, this, [this] { render(false); });
        connect(gain, &QDoubleSpinBox::valueChanged, this, [this] { render(false); });
        connect(offset, &QDoubleSpinBox::valueChanged, this, [this] { render(false); });
    }
    layout->addWidget(channelBox);
    plot_ = new viewers::LinePlotView(this); plot_->setObjectName("live.plot");
    plot_->setAxisTitles("Model time (s)", "Value (supplied units; shared numeric axis)"); plot_->setLegendVisible(true);
    plot_->bridge()->inspectionEnabled = true;
    layout->addWidget(plot_, 1);
    readout_ = new QLabel(QStringLiteral("Hover over the plot to inspect stored samples. Wheel zooms; drag pans and freezes this window."), this);
    readout_->setObjectName("live.readout"); readout_->setWordWrap(true); readout_->setTextFormat(Qt::PlainText);
    readout_->setTextInteractionFlags(Qt::TextSelectableByMouse); layout->addWidget(readout_);
    status_ = new QLabel(this); status_->setObjectName("live.status"); status_->setWordWrap(true); status_->setTextFormat(Qt::PlainText); layout->addWidget(status_);
    connect(seconds_, &QLineEdit::editingFinished, this, [this] {
        if (!seconds_->isModified()) return;
        if (!live_) { updateStatus("Start a run before entering a model-time duration."); return; }
        QString error;
        if (!live_->setDuration(seconds_->text(), &error)) { settingsError_ = error; updateStatus(); return; }
        seconds_->setModified(false); settingsError_.clear();
        capacity_->setValue(static_cast<int>(live_->capacity()));
        requestedCapacity_ = live_->capacity(); updateStatus();
    });
    connect(capacity_, &QSpinBox::valueChanged, this, [this](int value) {
        settingsError_.clear(); seconds_->setModified(false);
        requestedCapacity_ = static_cast<std::size_t>(value);
        if (live_) { live_->setCapacity(static_cast<std::size_t>(value)); updateStatus("Retention applied between complete steps; growth fills forward."); }
    });
    connect(freeze_, &QPushButton::toggled, this, [this](bool frozen) {
        freeze_->setText(frozen ? QStringLiteral("Display frozen") : QStringLiteral("Freeze display"));
        updateStatus();
    });
    connect(follow, &QPushButton::clicked, this, [this] {
        inspecting_ = false; freeze_->setChecked(false); plot_->bridge()->cursorVisible = false;
        emit plot_->bridge()->inspectionChanged(); cadence_.invalidate(); refreshSource(); render(true);
    });
    connect(autoY_, &QCheckBox::toggled, this, [this] { render(false); });
    connect(plot_, &viewers::LinePlotView::navigated, this, [this] { freeze_->setChecked(true); });
    connect(plot_, &viewers::LinePlotView::xRangeChanged, this, [this] { render(false); });
    connect(plot_, &viewers::LinePlotView::hovered, this, [this](double time, bool active) { inspect(time, active); });
    updateStatus();
}
LiveOscilloscopeWindow::~LiveOscilloscopeWindow()
{
    // QML hover teardown must not call into members after their destruction.
    disconnect(plot_, nullptr, this, nullptr);
    delete plot_;
}
viewers::ViewerType LiveOscilloscopeWindow::viewerType()
{
    return {"LiveOscilloscope", "Live oscilloscope (prototype)", "N→live", "Bounded time window; adjustable retention and exact sample inspection. Does not record the full run.", {},
        [](const viewers::ViewerInstanceSpec& spec) -> viewers::ObservationViewer* { return new LiveOscilloscopeWindow(spec.instanceId); }};
}
void LiveOscilloscopeWindow::attachLiveSource(std::shared_ptr<observation::LiveAcquisition> source)
{
    if (live_ == source) return;
    live_ = std::move(source); clearSamples();
    const int count = live_ ? static_cast<int>(live_->channels().size()) : 0;
    static const QColor colors[]{QColor("#1565c0"), QColor("#c62828"), QColor("#00875a"), QColor("#8e24aa"), QColor("#c25e00"), QColor("#007b83"), QColor("#544d00"), QColor("#555555")};
    for (int i = 0; i < 8; ++i) {
        const bool available = i < count;
        controls_[i].visible->setVisible(available); controls_[i].gain->setVisible(available); controls_[i].offset->setVisible(available);
        if (!available) continue;
        const auto& channel = live_->channels()[i];
        controls_[i].visible->setText(channel.name + " [" + (channel.unit.isEmpty() ? "unit unspecified" : channel.unit) + "]");
        if (plot_->series().size() <= i) plot_->addSeries(channel.name, {colors[i]});
        plot_->setSeriesName(plot_->series()[i], controls_[i].visible->text());
    }
    if (live_) {
        if (requestedCapacity_) live_->setCapacity(*requestedCapacity_);
        const QSignalBlocker blocker(capacity_); capacity_->setValue(static_cast<int>(live_->capacity())); }
    cadence_.invalidate(); updateStatus();
}
void LiveOscilloscopeWindow::clearSamples()
{
    frame_ = {}; inspecting_ = false; freeze_->setChecked(false);
    for (auto* series : plot_->series()) viewers::LinePlotView::setPoints(series, {});
    plot_->bridge()->cursorVisible = false; emit plot_->bridge()->inspectionChanged();
    readout_->setText("Hover over the plot for exact stored sample values."); updateStatus();
}
bool LiveOscilloscopeWindow::refreshSource()
{
    if (!live_ || !isVisible()) return false;
    if (cadence_.isValid() && cadence_.elapsed() < 33) return false;
    cadence_.restart();
    if (freeze_->isChecked() || inspecting_) { updateStatus(); return false; }
    auto snapshot = live_->snapshot();
    if (!snapshot) { updateStatus("Snapshot expired during copying; waiting for a coherent window."); return false; }
    frame_ = std::move(*snapshot); render(true); updateStatus(); return false;
}
void LiveOscilloscopeWindow::render(bool fit)
{
    if (rendering_) return;
    rendering_ = true;
    if (fit && !frame_.times.empty()) {
        const double last = frame_.times.back();
        plot_->setXRange(last - static_cast<double>(frame_.capacity-1)*frame_.dt, last);
    }
    traceWarning_.clear();
    for (qsizetype channel = 0; channel < plot_->series().size(); ++channel) {
        auto* series = plot_->series()[channel];
        QList<QPointF> points;
        if (channel < frame_.channels.size() && controls_[channel].visible->isChecked()) {
            // Suppress discontinuous/nonfinite channels rather than draw false bridges.
            bool continuous = true;
            for (std::size_t i = 0; i < frame_.steps.size(); ++i) {
                if ((i && frame_.steps[i] != frame_.steps[i-1]+1) ||
                    !std::isfinite(frame_.values[i*static_cast<std::size_t>(frame_.channels.size())+static_cast<std::size_t>(channel)])) { continuous = false; break; }
            }
            if (continuous) points = frame_.envelope(static_cast<std::size_t>(channel), static_cast<std::size_t>(std::max(64, plot_->width())), plot_->xMin(), plot_->xMax());
            else traceWarning_ = "Discontinuous/nonfinite trace hidden; raw inspection remains available.";
            for (auto& point : points) point.setY(point.y()*controls_[channel].gain->value()+controls_[channel].offset->value());
            if (std::ranges::any_of(points, [](const auto& point) { return !std::isfinite(point.y()); })) {
                points.clear();
                traceWarning_ = "Display gain/offset overflow; trace hidden. Reduce gain/offset; raw inspection remains available.";
            }
        }
        viewers::LinePlotView::setPoints(series, points);
    }
    if (autoY_->isChecked()) plot_->fitYToVisibleData();
    rendering_ = false;
    updateStatus();
}
QString LiveOscilloscopeWindow::inspect(double time, bool active)
{
    inspecting_ = active && !frame_.times.empty();
    const auto sample = active ? frame_.nearest(time) : std::nullopt;
    plot_->bridge()->cursorVisible = sample.has_value();
    if (sample) {
        const auto i = *sample;
        plot_->bridge()->cursorX = frame_.times[i];
        QString text = QStringLiteral("Stored sample • run %1 • step %2 • model time %3 s")
            .arg(frame_.runId).arg(frame_.steps[i]).arg(frame_.times[i], 0, 'g', 17);
        for (qsizetype c = 0; c < frame_.channels.size(); ++c) if (controls_[c].visible->isChecked()) {
            const auto& channel = frame_.channels[c];
            text += QStringLiteral("\n%1 = %2 %3 (source %4 → stored f64)").arg(channel.name)
                .arg(frame_.values[i*static_cast<std::size_t>(frame_.channels.size())+static_cast<std::size_t>(c)], 0, 'g', 17)
                .arg(channel.unit.isEmpty() ? QStringLiteral("[unit unspecified]") : channel.unit, channel.sourceType);
        }
        readout_->setText(text);
    } else readout_->setText(active ? "No retained sample at this model time." : "Hover over the plot for exact stored sample values.");
    emit plot_->bridge()->inspectionChanged(); updateStatus(); return readout_->text();
}
void LiveOscilloscopeWindow::updateStatus(const QString& suffix)
{
    if (!live_) { status_->setText("No live acquisition attached. Connect signals to this observer and start a run."); duration_->setText("Waiting for run"); return; }
    if (!seconds_->hasFocus() && !seconds_->isModified()) seconds_->setText(QString::number(static_cast<double>(live_->capacity()-1)*live_->dt(), 'g', 12));
    duration_->setText(QStringLiteral("%1 s maximum span • %2 MiB reserved")
        .arg(static_cast<double>(live_->capacity()-1)*live_->dt(), 0, 'g', 10)
        .arg(static_cast<double>(live_->allocatedBytes())/(1024*1024), 0, 'f', 2));
    const auto acquisitionStatus = live_->status();
    status_->setText(QStringLiteral("Run %1 • %2 • %3 • %4/%5 samples/channel • received %6 • evicted %7 • gaps %8%9%10")
        .arg(frame_.runId).arg(freeze_->isChecked() ? "Frozen snapshot" : inspecting_ ? "Inspecting displayed frame" : "Following latest")
        .arg(acquisitionStatus.finished ? "Run finished" : "Acquisition active").arg(frame_.times.size()).arg(live_->capacity())
        .arg(frame_.received).arg(frame_.evicted).arg(frame_.gaps)
        .arg(acquisitionStatus.error.isEmpty() ? QString() : " • ERROR: " + acquisitionStatus.error)
        .arg((traceWarning_ + settingsError_ + suffix).isEmpty() ? QString() : " • " + traceWarning_ + settingsError_ + suffix));
}
} // namespace s7studio::ui
