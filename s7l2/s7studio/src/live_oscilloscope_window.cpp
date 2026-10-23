// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-S7-plugin-permission
#include "live_oscilloscope_window.hpp"
#include "viewers/line_plot_view.hpp"
#include "viewers/spectrum_view.hpp"
#include "viewers/pair_analysis_view.hpp"
#include "viewers/window_inspection_panel.hpp"
#include <QCloseEvent>
#include <QJsonArray>
#include <QtGraphs/QLineSeries>
#include <QCheckBox>
#include <QComboBox>
#include <QEvent>
#include <QDoubleSpinBox>
#include <QGridLayout>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTabWidget>
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
    auto* tabs = new QTabWidget(this); tabs->setObjectName("live.views");
    auto* timePage = new QWidget(tabs); auto* timeLayout = new QVBoxLayout(timePage); timeLayout->setContentsMargins(0, 0, 0, 0);
    spectrum_ = new viewers::SpectrumView(tabs);
    constellation_ = new viewers::PairAnalysisView(analysis::PairKind::Constellation, tabs);
    correlation_ = new viewers::PairAnalysisView(analysis::PairKind::Correlation, tabs);
    tabs->addTab(timePage, "Time"); tabs->addTab(spectrum_, "Spectrum");
    tabs->addTab(constellation_, "Constellation / XY"); tabs->addTab(correlation_, "Correlation"); layout->addWidget(tabs, 1);
    auto* triggerBox = new QGroupBox("Time synchronization — sampled edge trigger", timePage);
    auto* tg = new QGridLayout(triggerBox);
    auto combo = [&](const char* name, const QStringList& items) { auto* c = new QComboBox(triggerBox); c->setObjectName(name); c->addItems(items); return c; };
    auto field = [&](int row, int col, const QString& text, QWidget* widget) { tg->addWidget(new QLabel(text, triggerBox), row, col*2); tg->addWidget(widget, row, col*2+1); };
    triggerMode_ = combo("trigger.mode", {"Rolling", "Auto", "Normal", "Single"}); field(0, 0, "Mode", triggerMode_);
    triggerChannel_ = combo("trigger.channel", {"Channel 1"}); field(0, 1, "Source / unit", triggerChannel_);
    triggerEdge_ = combo("trigger.edge", {"Rising", "Falling"}); field(0, 2, "Edge", triggerEdge_);
    triggerLevel_ = new QLineEdit("0", triggerBox); triggerLevel_->setObjectName("trigger.level"); triggerLevel_->setMaximumWidth(100); field(0, 3, "Level", triggerLevel_);
    triggerHysteresis_ = new QLineEdit("0", triggerBox); triggerHysteresis_->setObjectName("trigger.hysteresis"); triggerHysteresis_->setMaximumWidth(100); field(0, 4, "Hysteresis", triggerHysteresis_);
    auto spin = [&](const char* name, int low, int high, int value) { auto* c = new QSpinBox(triggerBox); c->setObjectName(name); c->setRange(low, high); c->setValue(value); return c; };
    triggerPre_ = spin("trigger.pre", 0, 100, 25); triggerPre_->setSuffix(" %"); field(1, 0, "Pre-trigger", triggerPre_);
    triggerHoldoff_ = spin("trigger.holdoff", 0, 1000000000, 0); field(1, 1, "Holdoff (steps)", triggerHoldoff_);
    triggerTimeout_ = spin("trigger.timeout", 1, 1000000000, 4096); field(1, 2, "Auto timeout (steps)", triggerTimeout_);
    auto* rearm = new QPushButton("Rearm", triggerBox); rearm->setObjectName("trigger.rearm"); tg->addWidget(rearm, 1, 6, 1, 2);
    triggerStatus_ = new QLabel("Rolling acquisition; select an edge trigger for stable periodic traces.", triggerBox);
    triggerStatus_->setObjectName("trigger.status"); triggerStatus_->setWordWrap(true); triggerStatus_->setTextFormat(Qt::PlainText); tg->addWidget(triggerStatus_, 2, 0, 1, 10);
    timeLayout->addWidget(triggerBox);
    auto* channelBox = new QGroupBox(QStringLiteral("Channels — display only; raw values remain unchanged"), timePage);
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
    timeLayout->addWidget(channelBox);
    plot_ = new viewers::LinePlotView(timePage); plot_->setObjectName("live.plot");
    plot_->setAxisTitles("Model time (s)", "Value (supplied units; shared numeric axis)"); plot_->setLegendVisible(true);
    plot_->bridge()->inspectionEnabled = true;
    timeLayout->addWidget(plot_, 1);
    readout_ = new QLabel(QStringLiteral("Hover over the plot to inspect stored samples. Wheel zooms; drag pans and freezes this window."), timePage);
    readout_->setObjectName("live.readout"); readout_->setWordWrap(true); readout_->setTextFormat(Qt::PlainText);
    readout_->setTextInteractionFlags(Qt::TextSelectableByMouse); timeLayout->addWidget(readout_);
    inspection_ = new viewers::WindowInspectionPanel([this, tabs]() -> std::optional<analysis::WindowSnapshot> {
        if (tabs->currentIndex() == 1) return spectrum_->inspectionSnapshot();
        if (tabs->currentIndex() == 2) return constellation_->inspectionSnapshot();
        if (tabs->currentIndex() == 3) return correlation_->inspectionSnapshot();
        if (frame_->steps.empty()) return {};
        auto data = analysis::timeWindow(frame_);
        QJsonArray display;
        for (qsizetype i = 0; i < frame_->channels.size(); ++i) display.append(QJsonObject{{"visible", controls_[i].visible->isChecked()},
            {"gain", analysis::exactNumber(controls_[i].gain->value())}, {"offset", analysis::exactNumber(controls_[i].offset->value())}});
        data.settings.insert("display_channels_at_pin", display); return data;
    }, this);
    layout->addWidget(inspection_); inspection_->setPlot(plot_);
    connect(inspection_, &viewers::WindowInspectionPanel::pinned, this, [this] {
        freeze_->setChecked(true);
        // A tab may be analyzing a newer frame while displaying its preceding
        // result. Re-pin that displayed owner even when Freeze was already on.
        std::shared_ptr<const observation::LiveFrame> pinned;
        if (spectrum_->isVisible()) pinned = spectrum_->setFrozen(true);
        else if (constellation_->isVisible()) pinned = constellation_->setFrozen(true);
        else if (correlation_->isVisible()) pinned = correlation_->setFrozen(true);
        if (pinned) frame_ = std::move(pinned);
        updateStatus();
    });
    for (auto* plot : {plot_, spectrum_->plotView(), constellation_->plotView(), correlation_->plotView()})
        connect(plot, &viewers::LinePlotView::pointPicked, this, [this, plot](double x, double y) { if (plot->isVisible()) inspection_->pick(x, y); });
    connect(spectrum_, &viewers::SpectrumView::inspectionInvalidated, this, [this] { if (spectrum_->isVisible()) inspection_->clear(); });
    for (auto* view : {constellation_, correlation_}) connect(view, &viewers::PairAnalysisView::inspectionInvalidated,
        this, [this, view] { if (view->isVisible()) inspection_->clear(); });
    status_ = new QLabel(this); status_->setObjectName("live.status"); status_->setWordWrap(true); status_->setTextFormat(Qt::PlainText); layout->addWidget(status_);
    for (auto* c : {triggerMode_, triggerChannel_, triggerEdge_}) connect(c, &QComboBox::currentIndexChanged, this, [this] { applyTriggerSettings(); });
    for (auto* c : {triggerPre_, triggerHoldoff_, triggerTimeout_}) connect(c, &QSpinBox::valueChanged, this, [this] { applyTriggerSettings(); });
    for (auto* c : {triggerLevel_, triggerHysteresis_}) connect(c, &QLineEdit::editingFinished, this, [this] { applyTriggerSettings(); });
    connect(rearm, &QPushButton::clicked, this, [this] {
        if (live_) live_->rearmTrigger();
        inspection_->clear(); freeze_->setChecked(false); inspecting_ = false; updateStatus();
    });
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
        if (live_) {
            live_->setCapacity(static_cast<std::size_t>(value));
            if (requestedTrigger_.mode != observation::TriggerMode::Rolling && !freeze_->isChecked()) clearSamples();
            updateStatus("Retention applied between complete steps; growth fills forward.");
        }
    });
    connect(freeze_, &QPushButton::toggled, this, [this](bool frozen) {
        freeze_->setText(frozen ? QStringLiteral("Display frozen") : QStringLiteral("Freeze display"));
        const auto pinned = spectrum_->setFrozen(frozen);
        if (frozen && spectrum_->isVisible() && pinned) frame_ = pinned;
        for (auto* view : {constellation_, correlation_}) {
            const auto pairFrame = view->setFrozen(frozen);
            if (frozen && view->isVisible() && pairFrame) frame_ = pairFrame;
        }
        if (!frozen) inspection_->clear();
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
    connect(spectrum_, &viewers::SpectrumView::inspectingChanged, this, [this](bool active) { inspecting_ = active; updateStatus(); });
    connect(spectrum_->plotView(), &viewers::LinePlotView::navigated, this, [this] { freeze_->setChecked(true); });
    for (auto* view : {constellation_, correlation_}) {
        connect(view, &viewers::PairAnalysisView::inspectingChanged, this, [this](bool active) { inspecting_ = active; updateStatus(); });
        connect(view->plotView(), &viewers::LinePlotView::navigated, this, [this] { freeze_->setChecked(true); });
    }
    connect(tabs, &QTabWidget::currentChanged, this, [this](int index) {
        inspection_->setPlot(index == 0 ? plot_ : index == 1 ? spectrum_->plotView() : index == 2 ? constellation_->plotView() : correlation_->plotView());
        inspecting_ = false; autoY_->setVisible(index == 0);
        if (index == 1) spectrum_->setFrame(frame_);
        else if (index == 2) constellation_->setFrame(frame_);
        else if (index == 3) correlation_->setFrame(frame_);
        else {
            if (requestedTrigger_.mode != observation::TriggerMode::Rolling && !freeze_->isChecked()) frame_ = std::make_shared<observation::LiveFrame>();
            cadence_.invalidate(); refreshSource(); render(true);
        }
    });
    updateStatus();
}
LiveOscilloscopeWindow::~LiveOscilloscopeWindow()
{
    inspection_->clear(true);
    delete inspection_;
    // Removing the active spectrum page changes the current tab. Disconnect
    // before tearing down either plot so that signal cannot render a dead view.
    disconnect(findChild<QTabWidget*>("live.views"), nullptr, this, nullptr);
    // QML hover teardown must not call into members after their destruction.
    disconnect(plot_, nullptr, this, nullptr);
    delete plot_;
    disconnect(spectrum_->plotView(), nullptr, this, nullptr);
    disconnect(spectrum_, nullptr, this, nullptr);
    delete spectrum_;
    for (auto* view : {constellation_, correlation_}) {
        disconnect(view->plotView(), nullptr, this, nullptr); disconnect(view, nullptr, this, nullptr); delete view;
    }
}
void LiveOscilloscopeWindow::closeEvent(QCloseEvent* event)
{
    inspection_->cancelExport(); QWidget::closeEvent(event);
}
void LiveOscilloscopeWindow::changeEvent(QEvent* event)
{
    QWidget::changeEvent(event);
    if (event->type() == QEvent::WindowStateChange) {
        spectrum_->setSuspended(isMinimized());
        constellation_->setSuspended(isMinimized());
        correlation_->setSuspended(isMinimized());
        if (!isMinimized()) { cadence_.invalidate(); refreshSource(); }
    }
}
viewers::ViewerType LiveOscilloscopeWindow::viewerType()
{
    return {"LiveOscilloscope", "Live oscilloscope (prototype)", "N→live", "Bounded time window; adjustable retention and exact sample inspection. Does not record the full run.", {},
        [](const viewers::ViewerInstanceSpec& spec) -> viewers::ObservationViewer* { return new LiveOscilloscopeWindow(spec.instanceId); }};
}
void LiveOscilloscopeWindow::applyTriggerSettings()
{
    bool levelOk = false, hysteresisOk = false;
    auto settings = requestedTrigger_;
    settings.level = triggerLevel_->text().toDouble(&levelOk); settings.hysteresis = triggerHysteresis_->text().toDouble(&hysteresisOk);
    if (!levelOk || !hysteresisOk || !std::isfinite(settings.level) || !std::isfinite(settings.hysteresis) || settings.hysteresis < 0 ||
        !std::isfinite(settings.level-settings.hysteresis/2) || !std::isfinite(settings.level+settings.hysteresis/2) ||
        (settings.hysteresis > 0 && !(settings.level-settings.hysteresis/2 < settings.level+settings.hysteresis/2))) {
        triggerError_ = "Invalid trigger level/hysteresis; enter finite numbers and a nonnegative, representable hysteresis. Previous settings remain active.";
        triggerStatus_->setText(triggerError_); return;
    }
    settings.mode = static_cast<observation::TriggerMode>(triggerMode_->currentIndex()); settings.channel = static_cast<std::size_t>(std::max(0, triggerChannel_->currentIndex()));
    settings.rising = triggerEdge_->currentIndex() == 0; settings.pretriggerPercent = triggerPre_->value();
    settings.holdoffSteps = static_cast<std::uint64_t>(triggerHoldoff_->value()); settings.autoTimeoutSteps = static_cast<std::uint64_t>(triggerTimeout_->value());
    QString error;
    if (live_ && !live_->configureTrigger(settings, &error)) { triggerError_ = error; triggerStatus_->setText(error); return; }
    triggerError_.clear();
    if (settings != requestedTrigger_) { requestedTrigger_ = settings; clearSamples(); }
    updateStatus();
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
        {
            const QSignalBlocker blocker(triggerChannel_); triggerChannel_->clear();
            for (const auto& c : live_->channels()) triggerChannel_->addItem(c.name+" ["+(c.unit.isEmpty() ? "unit unspecified" : c.unit)+"]");
            triggerChannel_->setCurrentIndex(static_cast<int>(std::min(requestedTrigger_.channel, static_cast<std::size_t>(count-1))));
            requestedTrigger_.channel = static_cast<std::size_t>(triggerChannel_->currentIndex());
        }
        live_->configureTrigger(requestedTrigger_);
        if (requestedCapacity_) live_->setCapacity(*requestedCapacity_);
        const QSignalBlocker blocker(capacity_); capacity_->setValue(static_cast<int>(live_->capacity())); }
    cadence_.invalidate(); updateStatus();
}
void LiveOscilloscopeWindow::clearSamples()
{
    inspection_->clear(true);
    frame_ = std::make_shared<observation::LiveFrame>(); inspecting_ = false; freeze_->setChecked(false); spectrum_->clear();
    constellation_->clear(); correlation_->clear();
    for (auto* series : plot_->series()) viewers::LinePlotView::setPoints(series, {});
    plot_->bridge()->cursorVisible = false; plot_->bridge()->triggerVisible = false; emit plot_->bridge()->inspectionChanged();
    readout_->setText("Hover over the plot for exact stored sample values."); updateStatus();
}
bool LiveOscilloscopeWindow::refreshSource()
{
    if (!live_ || !isVisible() || isMinimized()) return false;
    if (cadence_.isValid() && cadence_.elapsed() < 33) return false;
    cadence_.restart();
    if (freeze_->isChecked() || inspecting_) { updateStatus(); return false; }
    const bool triggeredTime = plot_->isVisible() && requestedTrigger_.mode != observation::TriggerMode::Rolling;
    if (triggeredTime) {
        const auto state = live_->triggerStatus();
        if (!state.captureId || (frame_->trigger && frame_->trigger->id == state.captureId && frame_->finished == state.finished)) { updateStatus(); return false; }
    }
    auto snapshot = triggeredTime ? live_->triggerSnapshot() : live_->snapshot();
    if (!snapshot) { updateStatus("Snapshot expired during copying; waiting for a coherent window."); return false; }
    frame_ = std::make_shared<observation::LiveFrame>(std::move(*snapshot));
    if (spectrum_->isVisible()) spectrum_->setFrame(frame_);
    else if (constellation_->isVisible()) constellation_->setFrame(frame_);
    else if (correlation_->isVisible()) correlation_->setFrame(frame_);
    else render(true);
    updateStatus(); return false;
}
void LiveOscilloscopeWindow::render(bool fit)
{
    if (rendering_) return;
    rendering_ = true;
    const auto& frame = *frame_;
    const bool triggered = frame.trigger && frame.trigger->triggered;
    plot_->bridge()->triggerVisible = triggered; emit plot_->bridge()->inspectionChanged();
    plot_->setAxisTitles(triggered ? "Time from sampled trigger (model s)" : "Model time (s)", "Value (supplied units; shared numeric axis)");
    if (fit && !frame.times.empty()) {
        if (triggered) {
            const auto pre = static_cast<std::size_t>(frame.trigger->settings.pretriggerPercent)*(frame.capacity-1)/100;
            const double post = static_cast<double>(frame.capacity-1-pre);
            const double end = frame.rateNumerator ? post*static_cast<double>(frame.rateDenominator)/static_cast<double>(frame.rateNumerator) : post*frame.dt;
            plot_->setXRange(frame.plottedTime(0), end);
        } else {
            const double last = frame.times.back(); plot_->setXRange(last-static_cast<double>(frame.capacity-1)*frame.dt, last);
        }
    }
    traceWarning_.clear();
    for (qsizetype channel = 0; channel < plot_->series().size(); ++channel) {
        auto* series = plot_->series()[channel];
        QList<QPointF> points;
        if (channel < frame.channels.size() && controls_[channel].visible->isChecked()) {
            // Suppress discontinuous/nonfinite channels rather than draw false bridges.
            bool continuous = true;
            for (std::size_t i = 0; i < frame.steps.size(); ++i) {
                if ((i && frame.steps[i] != frame.steps[i-1]+1) ||
                    !std::isfinite(frame.values[i*static_cast<std::size_t>(frame.channels.size())+static_cast<std::size_t>(channel)])) { continuous = false; break; }
            }
            if (continuous) points = frame.envelope(static_cast<std::size_t>(channel), static_cast<std::size_t>(std::max(64, plot_->width())), plot_->xMin(), plot_->xMax());
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
    const auto& frame = *frame_;
    inspecting_ = active && !frame.times.empty();
    const auto sample = active ? frame.nearestPlotted(time) : std::nullopt;
    plot_->bridge()->cursorVisible = sample.has_value();
    if (sample) {
        const auto i = *sample;
        plot_->bridge()->cursorX = frame.plottedTime(i);
        QString text = QStringLiteral("Stored sample • run %1 • step %2 • model time %3 s")
            .arg(frame.runId).arg(frame.steps[i]).arg(frame.times[i], 0, 'g', 17);
        for (qsizetype c = 0; c < frame.channels.size(); ++c) if (controls_[c].visible->isChecked()) {
            const auto& channel = frame.channels[c];
            text += QStringLiteral("\n%1 = %2 %3 (source %4 → stored f64)").arg(channel.name)
                .arg(frame.values[i*static_cast<std::size_t>(frame.channels.size())+static_cast<std::size_t>(c)], 0, 'g', 17)
                .arg(channel.unit.isEmpty() ? QStringLiteral("[unit unspecified]") : channel.unit, channel.sourceType);
        }
        if (frame.trigger && frame.trigger->triggered) text += QStringLiteral("\nTrigger-relative time = %1 s; sampled trigger step %2").arg(frame.plottedTime(i), 0, 'g', 17).arg(frame.trigger->step);
        readout_->setText(text);
    } else readout_->setText(active ? "No retained sample at this model time." : "Hover over the plot for exact stored sample values.");
    emit plot_->bridge()->inspectionChanged(); updateStatus(); return readout_->text();
}
void LiveOscilloscopeWindow::updateStatus(const QString& suffix)
{
    if (!live_) {
        status_->setText("No live acquisition attached. Connect signals to this observer and start a run."); duration_->setText("Waiting for run");
        triggerStatus_->setText(triggerError_.isEmpty() ? "Trigger settings apply before acquisition starts on the next run." : triggerError_); return;
    }
    if (!seconds_->hasFocus() && !seconds_->isModified()) seconds_->setText(QString::number(static_cast<double>(live_->capacity()-1)*live_->dt(), 'g', 12));
    duration_->setText(QStringLiteral("%1 s maximum span • %2 MiB reserved")
        .arg(static_cast<double>(live_->capacity()-1)*live_->dt(), 0, 'g', 10)
        .arg(static_cast<double>(live_->allocatedBytes())/(1024*1024), 0, 'f', 2));
    const auto trigger = live_->triggerStatus();
    const char* states[]{"Rolling", "Filling pre-trigger history", "Armed — waiting for edge", "Collecting post-trigger", "Holdoff", "Held single capture — Rearm to capture again"};
    QString triggerText = QString::fromLatin1(states[static_cast<int>(trigger.state)]);
    if (trigger.finished && !trigger.captureId && requestedTrigger_.mode != observation::TriggerMode::Rolling) triggerText += " • Run finished without a completed trigger; rearm needs new samples.";
    if (frame_->trigger) triggerText += QStringLiteral(" • showing capture %1 • %2%3 • sampled step %4").arg(frame_->trigger->id)
        .arg(frame_->trigger->triggered ? "Triggered" : "Auto timeout: UNTRIGGERED").arg(frame_->trigger->partial ? " • PARTIAL post-trigger interval" : "").arg(frame_->trigger->step);
    if (trigger.aborted) triggerText += QStringLiteral(" • interrupted captures %1 (gap/nonfinite source)").arg(trigger.aborted);
    triggerStatus_->setText(triggerError_.isEmpty() ? triggerText : triggerError_+" • "+triggerText);
    const auto acquisitionStatus = live_->status();
    const auto& frame = *frame_;
    status_->setText(QStringLiteral("Run %1 • %2 • %3 • %4/%5 samples/channel • received %6 • evicted %7 • gaps %8%9%10")
        .arg(frame.runId).arg(freeze_->isChecked() ? "Frozen snapshot" : inspecting_ ? "Inspecting displayed frame" : "Following latest")
        .arg(acquisitionStatus.finished ? "Run finished" : "Acquisition active").arg(frame.times.size()).arg(live_->capacity())
        .arg(frame.received).arg(frame.evicted).arg(frame.gaps)
        .arg(acquisitionStatus.error.isEmpty() ? QString() : " • ERROR: " + acquisitionStatus.error)
        .arg((traceWarning_ + settingsError_ + suffix).isEmpty() ? QString() : " • " + traceWarning_ + settingsError_ + suffix));
}
} // namespace s7studio::ui
