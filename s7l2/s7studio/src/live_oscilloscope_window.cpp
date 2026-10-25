// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-S7-plugin-permission
#include "live_oscilloscope_window.hpp"
#include "viewers/line_plot_view.hpp"
#include "viewers/spectrum_view.hpp"
#include "viewers/pair_analysis_view.hpp"
#include "viewers/window_inspection_panel.hpp"
#include <QDebug>
#include <QCloseEvent>
#include <QJsonArray>
#include <QtGraphs/QLineSeries>
#include <QCheckBox>
#include <QComboBox>
#include <QEvent>
#include <QFormLayout>
#include <QDoubleSpinBox>
#include <QGridLayout>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QSplitter>
#include <QTabWidget>
#include <QTimer>
#include <QToolButton>
#include <QVariantMap>
#include <QVBoxLayout>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>

namespace s7studio::ui {
LiveOscilloscopeWindow::LiveOscilloscopeWindow(QString id)
{
    setObjectName("liveOscilloscope");
    setWindowTitle(QStringLiteral("Live oscilloscope — %1 (prototype)").arg(id));
    resize(1100, 740);
    // Plot on the left at full height; every control lives in a resizable,
    // hideable column on the right so settings never steal plot height.
    auto* root = new QHBoxLayout(this); root->setContentsMargins(4, 4, 4, 4);
    auto* splitter = new QSplitter(Qt::Horizontal, this); splitter->setObjectName("live.splitter"); splitter->setChildrenCollapsible(false);
    root->addWidget(splitter);
    auto* tabs = new QTabWidget(splitter); tabs->setObjectName("live.views"); tabs->setDocumentMode(true);
    auto* timePage = new QWidget(tabs); auto* timeLayout = new QVBoxLayout(timePage); timeLayout->setContentsMargins(0, 0, 0, 0);
    spectrum_ = new viewers::SpectrumView(tabs);
    constellation_ = new viewers::PairAnalysisView(analysis::PairKind::Constellation, tabs);
    correlation_ = new viewers::PairAnalysisView(analysis::PairKind::Correlation, tabs);
    tabs->addTab(timePage, "Time"); tabs->addTab(spectrum_, "Spectrum");
    tabs->addTab(constellation_, "Constellation / XY"); tabs->addTab(correlation_, "Correlation");
    auto* side = new QWidget(splitter); side->setObjectName("live.controls");
    auto* sideLayout = new QVBoxLayout(side); sideLayout->setContentsMargins(0, 0, 0, 0);
    auto* buttons = new QGridLayout;
    freeze_ = new QPushButton(QStringLiteral("Freeze display"), side); freeze_->setObjectName("live.freeze"); freeze_->setCheckable(true);
    buttons->addWidget(freeze_, 0, 0);
    auto* follow = new QPushButton(QStringLiteral("Resume live / Fit"), side); follow->setObjectName("live.follow"); buttons->addWidget(follow, 0, 1);
    autoY_ = new QCheckBox(QStringLiteral("Auto Y"), side); autoY_->setObjectName("live.autoY"); autoY_->setChecked(true); buttons->addWidget(autoY_, 1, 0, 1, 2);
    sideLayout->addLayout(buttons);
    auto* scroll = new QScrollArea(side); scroll->setObjectName("live.controlsScroll"); scroll->setWidgetResizable(true); scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto* panel = new QWidget(scroll); auto* panelLayout = new QVBoxLayout(panel); panelLayout->setContentsMargins(0, 0, 4, 0);
    scroll->setWidget(panel); sideLayout->addWidget(scroll, 1);
    auto form = [](QGroupBox* box) { auto* f = new QFormLayout(box); f->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow); return f; };
    auto* settings = new QGroupBox(QStringLiteral("Acquisition"), panel);
    settings->setToolTip(QStringLiteral("Bounded RAM, no full-run recording."));
    auto* acquisition = form(settings);
    capacity_ = new QSpinBox(settings); capacity_->setObjectName("live.samples");
    capacity_->setRange(2, static_cast<int>(observation::LiveAcquisition::maxPositions)); capacity_->setValue(4096);
    capacity_->setToolTip(QStringLiteral("Changes retention during this run. Growing fills forward; evicted history cannot return. Does not change model rate."));
    acquisition->addRow(QStringLiteral("Samples / channel"), capacity_);
    seconds_ = new QLineEdit(settings); seconds_->setObjectName("live.seconds");
    seconds_->setToolTip("Positive model seconds, including scientific notation. Rounded up to complete sample intervals.");
    acquisition->addRow(QStringLiteral("or duration (s)"), seconds_);
    duration_ = new QLabel(QStringLiteral("Start a run to see model duration"), settings); duration_->setObjectName("live.duration"); duration_->setWordWrap(true);
    acquisition->addRow(duration_);
    panelLayout->addWidget(settings);
    auto* triggerBox = new QGroupBox("Trigger — sampled edge", panel);
    triggerBox->setToolTip("Time synchronization by a sampled edge trigger.");
    auto* tg = form(triggerBox);
    auto combo = [&](const char* name, const QStringList& items) {
        auto* c = new QComboBox(triggerBox); c->setObjectName(name); c->addItems(items);
        c->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon); c->setMinimumContentsLength(8); return c;
    };
    triggerMode_ = combo("trigger.mode", {"Rolling", "Auto", "Normal", "Single"}); tg->addRow("Mode", triggerMode_);
    triggerChannel_ = combo("trigger.channel", {"Channel 1"}); tg->addRow("Trigger input", triggerChannel_);
    triggerEdge_ = combo("trigger.edge", {"Rising", "Falling"}); tg->addRow("Edge", triggerEdge_);
    triggerLevel_ = new QLineEdit("0", triggerBox); triggerLevel_->setObjectName("trigger.level"); tg->addRow("Level", triggerLevel_);
    triggerHysteresis_ = new QLineEdit("0", triggerBox); triggerHysteresis_->setObjectName("trigger.hysteresis"); tg->addRow("Hysteresis", triggerHysteresis_);
    auto spin = [&](const char* name, int low, int high, int value) { auto* c = new QSpinBox(triggerBox); c->setObjectName(name); c->setRange(low, high); c->setValue(value); return c; };
    triggerPre_ = spin("trigger.pre", 0, 100, 25); triggerPre_->setSuffix(" %"); tg->addRow("Pre-trigger", triggerPre_);
    triggerHoldoff_ = spin("trigger.holdoff", 0, 1000000000, 0); tg->addRow("Holdoff (steps)", triggerHoldoff_);
    triggerTimeout_ = spin("trigger.timeout", 1, 1000000000, 4096); tg->addRow("Auto timeout (steps)", triggerTimeout_);
    auto* rearm = new QPushButton("Rearm", triggerBox); rearm->setObjectName("trigger.rearm"); tg->addRow(rearm);
    triggerStatus_ = new QLabel("Rolling acquisition; select an edge trigger for stable periodic traces.", triggerBox);
    triggerStatus_->setObjectName("trigger.status"); triggerStatus_->setWordWrap(true); triggerStatus_->setTextFormat(Qt::PlainText); tg->addRow(triggerStatus_);
    panelLayout->addWidget(triggerBox);
    auto* channelBox = new QGroupBox(QStringLiteral("Channels — display only"), panel);
    channelBox->setToolTip(QStringLiteral("Display gain and offset only; raw values remain unchanged."));
    auto* grid = new QGridLayout(channelBox);
    grid->addWidget(new QLabel("Display gain", channelBox), 0, 0);
    grid->addWidget(new QLabel("Display offset", channelBox), 0, 1);
    for (int i = 0; i < 8; ++i) {
        auto* visible = new QCheckBox(channelBox); visible->setChecked(true);
        auto* gain = new QDoubleSpinBox(channelBox); gain->setDecimals(6); gain->setRange(.000001, 1e6); gain->setValue(1);
        auto* offset = new QDoubleSpinBox(channelBox); offset->setDecimals(6); offset->setRange(-1e12, 1e12);
        // The wide ranges would otherwise size each spin box for 1e12 and widen the column.
        gain->setMinimumWidth(90); offset->setMinimumWidth(90);
        visible->setObjectName(QStringLiteral("live.channel.%1.visible").arg(i));
        gain->setObjectName(QStringLiteral("live.channel.%1.gain").arg(i));
        offset->setObjectName(QStringLiteral("live.channel.%1.offset").arg(i));
        controls_.push_back({visible, gain, offset});
        grid->addWidget(visible, 2*i+1, 0, 1, 2); grid->addWidget(gain, 2*i+2, 0); grid->addWidget(offset, 2*i+2, 1);
        visible->hide(); gain->hide(); offset->hide();
        connect(visible, &QCheckBox::toggled, this, [this] { render(false); });
        connect(gain, &QDoubleSpinBox::valueChanged, this, [this] { render(false); });
        connect(offset, &QDoubleSpinBox::valueChanged, this, [this] { render(false); });
    }
    panelLayout->addWidget(channelBox);
    plot_ = new viewers::LinePlotView(timePage); plot_->setObjectName("live.plot");
    plot_->setAxisTitles("Model time (s)", "Value (supplied units; shared numeric axis)"); plot_->setLegendVisible(true);
    plot_->bridge()->inspectionEnabled = true;
    timeLayout->addWidget(plot_, 1);
    readout_ = new QLabel(QStringLiteral("Hover over the plot to inspect stored samples. Wheel zooms; drag pans and freezes this window."), side);
    readout_->setObjectName("live.readout"); readout_->setWordWrap(true); readout_->setTextFormat(Qt::PlainText);
    readout_->setFrameShape(QFrame::StyledPanel); readout_->setMargin(4);
    readout_->setTextInteractionFlags(Qt::TextSelectableByMouse);
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
    auto* inspectionBox = new QGroupBox(QStringLiteral("Window inspection"), panel);
    (new QVBoxLayout(inspectionBox))->addWidget(inspection_); panelLayout->addWidget(inspectionBox); panelLayout->addStretch();
    // Size the column to its content so no field is clipped beside the plot.
    side->setMinimumWidth(panel->sizeHint().width() + scroll->verticalScrollBar()->sizeHint().width());
    inspection_->setPlot(plot_);
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
    sideLayout->addWidget(readout_);
    status_ = new QLabel(side); status_->setObjectName("live.status"); status_->setMargin(4); status_->setWordWrap(true); status_->setTextFormat(Qt::PlainText); sideLayout->addWidget(status_);
    splitter->setStretchFactor(0, 1); splitter->setStretchFactor(1, 0); splitter->setSizes({780, 320});
    auto* toggle = new QToolButton(tabs); toggle->setObjectName("live.controlsToggle"); toggle->setCheckable(true); toggle->setChecked(true);
    toggle->setText(QStringLiteral("Hide controls ▸")); toggle->setToolTip(QStringLiteral("Show or hide the control column to give the plot the whole window."));
    tabs->setCornerWidget(toggle, Qt::TopRightCorner);
    connect(toggle, &QToolButton::toggled, side, [toggle, side](bool shown) {
        side->setVisible(shown); toggle->setText(shown ? QStringLiteral("Hide controls ▸") : QStringLiteral("◂ Show controls"));
    });
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
        timeWorker_.cancel(); ++timeRequestRevision_; requestedCaptureId_ = 0; cadence_.invalidate();
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
        emit plot_->bridge()->inspectionChanged(); render(true); cadence_.invalidate(); refreshSource();
    });
    connect(autoY_, &QCheckBox::toggled, this, [this] { render(false); });
    connect(plot_, &viewers::LinePlotView::navigated, this, [this] { freeze_->setChecked(true); });
    connect(plot_, &viewers::LinePlotView::xRangeChanged, this, [this] { render(false); });
    connect(plot_, &viewers::LinePlotView::hovered, this, [this](double time, bool active) { inspect(time, active); });
    connect(plot_, &viewers::LinePlotView::hoveredPoint, this, [this](double x, double y, bool active) { hoverTip(x, y, active); });
    connect(spectrum_, &viewers::SpectrumView::inspectingChanged, this, [this](bool active) { inspecting_ = active; updateStatus(); });
    connect(spectrum_->plotView(), &viewers::LinePlotView::navigated, this, [this] { freeze_->setChecked(true); });
    for (auto* view : {constellation_, correlation_}) {
        connect(view, &viewers::PairAnalysisView::inspectingChanged, this, [this](bool active) { inspecting_ = active; updateStatus(); });
        connect(view->plotView(), &viewers::LinePlotView::navigated, this, [this] { freeze_->setChecked(true); });
    }
    connect(tabs, &QTabWidget::currentChanged, this, [this, timeOnly = QList<QWidget*>{triggerBox, channelBox, readout_}](int index) {
        inspection_->setPlot(index == 0 ? plot_ : index == 1 ? spectrum_->plotView() : index == 2 ? constellation_->plotView() : correlation_->plotView());
        inspecting_ = false; autoY_->setVisible(index == 0);
        for (auto* widget : timeOnly) widget->setVisible(index == 0);
        if (index == 1) spectrum_->setFrame(frame_);
        else if (index == 2) constellation_->setFrame(frame_);
        else if (index == 3) correlation_->setFrame(frame_);
        else {
            if (requestedTrigger_.mode != observation::TriggerMode::Rolling && !freeze_->isChecked()) frame_ = std::make_shared<observation::LiveFrame>();
            render(true); cadence_.invalidate(); refreshSource();
        }
    });
    // ViewerManager refreshes managed windows, while tests, embedded use and
    // standalone windows may have no manager poller. Keep worker results moving
    // to the GUI thread in either case; cadence_ coalesces duplicate polls.
    auto* refreshTimer = new QTimer(this);
    refreshTimer->setInterval(16);
    connect(refreshTimer, &QTimer::timeout, this, [this] { refreshSource(); });
    refreshTimer->start();
    updateStatus();
}
LiveOscilloscopeWindow::~LiveOscilloscopeWindow()
{
    timeWorker_.cancel();
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
    return {"LiveOscilloscope", {}, "N→live", {}, {},
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
    if (settings != requestedTrigger_) { requestedTrigger_ = settings; clearSamples(); emit triggerSettingsEdited(); }
    updateStatus();
}
void LiveOscilloscopeWindow::setTriggerSettings(const observation::TriggerSettings& settings)
{
    if (settings == requestedTrigger_) return;
    {
        const QSignalBlocker blockMode(triggerMode_), blockChannel(triggerChannel_), blockEdge(triggerEdge_);
        const QSignalBlocker blockPre(triggerPre_), blockHoldoff(triggerHoldoff_), blockTimeout(triggerTimeout_);
        triggerMode_->setCurrentIndex(static_cast<int>(settings.mode)); triggerEdge_->setCurrentIndex(settings.rising ? 0 : 1);
        // Before a run attaches, the source list is a placeholder; attachLiveSource() selects the channel.
        if (settings.channel < static_cast<std::size_t>(triggerChannel_->count())) triggerChannel_->setCurrentIndex(static_cast<int>(settings.channel));
        // Shortest text that round-trips exactly: 0.2, not 0.20000000000000001.
        triggerLevel_->setText(QString::number(settings.level, 'g', QLocale::FloatingPointShortest));
        triggerHysteresis_->setText(QString::number(settings.hysteresis, 'g', QLocale::FloatingPointShortest));
        triggerPre_->setValue(settings.pretriggerPercent);
        triggerHoldoff_->setValue(static_cast<int>(settings.holdoffSteps)); triggerTimeout_->setValue(static_cast<int>(settings.autoTimeoutSteps));
    }
    triggerError_.clear();
    QString error;
    if (live_ && !live_->configureTrigger(settings, &error)) { triggerError_ = error; updateStatus(); return; }
    requestedTrigger_ = settings; clearSamples();
}
void LiveOscilloscopeWindow::attachLiveSource(std::shared_ptr<observation::LiveAcquisition> source)
{
    if (live_ == source) return;
    timeWorker_.cancel(); ++timeRequestRevision_; requestedCaptureId_ = 0;
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
    timeWorker_.cancel(); ++timeRequestRevision_; requestedCaptureId_ = 0;
    inspection_->clear(true);
    frame_ = std::make_shared<observation::LiveFrame>(); inspecting_ = false; freeze_->setChecked(false); spectrum_->clear();
    constellation_->clear(); correlation_->clear();
    for (auto* series : plot_->series()) viewers::LinePlotView::setPoints(series, {});
    plot_->bridge()->cursorVisible = false; plot_->bridge()->triggerVisible = false; emit plot_->bridge()->inspectionChanged();
    hoverTip(0, 0, false);
    readout_->setText("Hover over the plot for exact stored sample values."); updateStatus();
}
bool LiveOscilloscopeWindow::refreshSource()
{
    if (!live_ || !isVisible() || isMinimized()) return false;
    if (auto result = timeWorker_.takeResult()) {
        if (result->sourceSnapshot && result->revision == timeRequestRevision_) requestedCaptureId_ = 0;
        if (result->revision == timeRequestRevision_ &&
            (!result->sourceSnapshot || (!freeze_->isChecked() && !inspecting_))) {
            if (result->frame && !(result->preparedTime && !plot_->isVisible())) {
                frame_ = result->frame;
                traceWarning_ = result->warning;
                if (result->preparedTime && plot_->isVisible()) presentTimeResult(*result);
                else if (spectrum_->isVisible()) spectrum_->setFrame(frame_);
                else if (constellation_->isVisible()) constellation_->setFrame(frame_);
                else if (correlation_->isVisible()) correlation_->setFrame(frame_);
            }
            if (!result->warning.isEmpty()) traceWarning_ = result->warning;
            if (result->snapshotMs > 25 || result->prepareMs > 25)
                qWarning().noquote() << "Live oscilloscope worker slow stage: snapshot" << result->snapshotMs
                                     << "ms, reduction" << result->prepareMs << "ms";
            updateStatus();
        }
    }
    if (cadence_.isValid() && cadence_.elapsed() < 33) return false;
    cadence_.restart();
    if (freeze_->isChecked() || inspecting_) { updateStatus(); return false; }
    const bool triggeredTime = plot_->isVisible() && requestedTrigger_.mode != observation::TriggerMode::Rolling;
    std::uint64_t captureId = 0;
    bool finished = false;
    if (triggeredTime) {
        const auto state = live_->triggerStatus();
        captureId = state.captureId; finished = state.finished;
        if (!captureId || (frame_->trigger && frame_->trigger->id == captureId && frame_->finished == finished) ||
            (requestedCaptureId_ == captureId && requestedCaptureFinished_ == finished)) { updateStatus(); return false; }
    }
    const bool prepareTime = plot_->isVisible();
    auto settings = timeViewSettings(prepareTime, prepareTime);
    ++timeRequestRevision_;
    timeWorker_.submitSource(live_, triggeredTime, settings, timeRequestRevision_);
    if (triggeredTime) { requestedCaptureId_ = captureId; requestedCaptureFinished_ = finished; }
    return false;
}
void LiveOscilloscopeWindow::render(bool fit)
{
    if (rendering_) return;
    rendering_ = true;
    const auto& frame = *frame_;
    const bool triggered = frame.trigger && frame.trigger->triggered;
    if (plot_->bridge()->triggerVisible != triggered) { plot_->bridge()->triggerVisible = triggered; emit plot_->bridge()->inspectionChanged(); }
    const QString xTitle = triggered ? "Time from sampled trigger (model s)" : "Model time (s)";
    if (plot_->xTitle() != xTitle) plot_->setAxisTitles(xTitle, "Value (supplied units; shared numeric axis)");
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
    rendering_ = false;
    if (!frame.steps.empty() && plot_->isVisible()) {
        ++timeRequestRevision_; requestedCaptureId_ = 0;
        timeWorker_.submitFrame(frame_, timeViewSettings(true, false), timeRequestRevision_);
    } else {
        for (auto* series : plot_->series()) viewers::LinePlotView::setPoints(series, {});
        traceWarning_.clear(); updateStatus();
    }
}

analysis::TimeViewSettings LiveOscilloscopeWindow::timeViewSettings(bool prepare, bool fit) const
{
    analysis::TimeViewSettings settings;
    settings.buckets = static_cast<std::size_t>(std::max(64, plot_->width()));
    settings.lower = fit ? -std::numeric_limits<double>::infinity() : plot_->xMin();
    settings.upper = fit ? std::numeric_limits<double>::infinity() : plot_->xMax();
    settings.prepareTime = prepare; settings.autoY = autoY_->isChecked(); settings.fitX = fit;
    for (qsizetype i = 0; i < controls_.size(); ++i) {
        const auto channel = static_cast<std::size_t>(i);
        settings.visible[channel] = controls_[i].visible->isChecked();
        settings.gain[channel] = controls_[i].gain->value(); settings.offset[channel] = controls_[i].offset->value();
    }
    return settings;
}

void LiveOscilloscopeWindow::presentTimeResult(const analysis::TimeViewResult& result)
{
    if (!result.frame) return;
    const auto started = std::chrono::steady_clock::now();
    rendering_ = true;
    const bool triggered = result.frame->trigger && result.frame->trigger->triggered;
    if (plot_->bridge()->triggerVisible != triggered) { plot_->bridge()->triggerVisible = triggered; emit plot_->bridge()->inspectionChanged(); }
    const QString xTitle = triggered ? "Time from sampled trigger (model s)" : "Model time (s)";
    if (plot_->xTitle() != xTitle) plot_->setAxisTitles(xTitle, "Value (supplied units; shared numeric axis)");
    if (result.hasXRange) plot_->setXRange(result.xMin, result.xMax);
    for (qsizetype i = 0; i < plot_->series().size(); ++i)
        viewers::LinePlotView::setPoints(plot_->series()[i], static_cast<std::size_t>(i) < result.points.size() ? result.points[static_cast<std::size_t>(i)] : QList<QPointF>{});
    traceWarning_ = result.warning;
    if (autoY_->isChecked() && result.hasYRange) {
        const double pad = result.yMax == result.yMin ? std::max(0.5, std::abs(result.yMin)*.05) : (result.yMax-result.yMin)*.04;
        plot_->setYRange(result.yMin-pad, result.yMax+pad);
    }
    rendering_ = false;
    const double uiMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now()-started).count();
    if (uiMs > 50) qWarning().noquote() << "Live oscilloscope GUI series update took" << uiMs
                                        << "ms after worker snapshot" << result.snapshotMs
                                        << "ms and reduction" << result.prepareMs << "ms";
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
QVariantList LiveOscilloscopeWindow::hoverTip(double x, double y, bool active)
{
    constexpr double tolerance = 10; // pixels from pointer to a drawn trace
    const auto& frame = *frame_;
    auto* bridge = plot_->bridge();
    const auto area = bridge->plotAreaSize;
    const double sx = area.width()/(plot_->xMax()-plot_->xMin()), sy = area.height()/(plot_->yMax()-plot_->yMin());
    QVariantList rows, markers;
    if (active && !frame.times.empty() && area.width() > 0 && area.height() > 0 && std::isfinite(sx) && std::isfinite(sy)) {
        // Samples within the tolerance in x, plus one neighbour per side so
        // segments crossing the pointer column count when zoomed in.
        auto bound = [&](double value) {
            std::size_t lo = 0, hi = frame.times.size();
            while (lo < hi) { const auto mid = (lo+hi)/2; if (frame.plottedTime(mid) < value) lo = mid+1; else hi = mid; }
            return lo;
        };
        auto first = bound(x-tolerance/sx), last = bound(x+tolerance/sx);
        if (first > 0) --first;
        last = std::min(last+1, frame.times.size());
        const auto stride = static_cast<std::size_t>(frame.channels.size());
        struct Hit { double distance; qsizetype channel; std::size_t index; };
        std::vector<Hit> hits;
        for (qsizetype c = 0; c < frame.channels.size() && c < plot_->series().size(); ++c) {
            // Only traces actually drawn: hidden, overflowed or gapped ones have no points.
            if (!controls_[c].visible->isChecked() || plot_->series()[c]->count() == 0) continue;
            const double gain = controls_[c].gain->value(), offset = controls_[c].offset->value();
            auto pixel = [&](std::size_t i) {
                return QPointF((frame.plottedTime(i)-x)*sx, (y-(frame.values[i*stride+static_cast<std::size_t>(c)]*gain+offset))*sy);
            };
            Hit best{std::numeric_limits<double>::infinity(), c, 0};
            for (auto i = first; i < last; ++i) {
                const auto p = pixel(i);
                if (!std::isfinite(p.x()) || !std::isfinite(p.y())) continue;
                if (const auto d = std::hypot(p.x(), p.y()); d < best.distance) best = {d, c, i};
                if (i+1 >= last || frame.steps[i+1] != frame.steps[i]+1) continue;
                const auto q = pixel(i+1);
                if (!std::isfinite(q.x()) || !std::isfinite(q.y())) continue;
                // Distance from the pointer (origin) to segment p–q; report its nearer sample.
                const auto dq = q-p; const double length = QPointF::dotProduct(dq, dq);
                const double t = length > 0 ? std::clamp(-QPointF::dotProduct(p, dq)/length, 0., 1.) : 0.;
                if (const auto d = std::hypot(p.x()+t*dq.x(), p.y()+t*dq.y()); d < best.distance) best = {d, c, t < .5 ? i : i+1};
            }
            if (best.distance <= tolerance) hits.push_back(best);
        }
        std::sort(hits.begin(), hits.end(), [](const Hit& a, const Hit& b) { return a.distance < b.distance; });
        const bool triggered = frame.trigger && frame.trigger->triggered;
        for (const auto& hit : hits) {
            const auto i = hit.index;
            if (rows.isEmpty() || i != hits.front().index)
                rows.append(QVariantMap{{"text", QStringLiteral("%1 s%2 • step %3").arg(frame.plottedTime(i), 0, 'g', 10)
                    .arg(triggered ? " from trigger" : "").arg(frame.steps[i])}});
            const auto& channel = frame.channels[hit.channel];
            const double raw = frame.values[i*stride+static_cast<std::size_t>(hit.channel)];
            const double gain = controls_[hit.channel].gain->value(), offset = controls_[hit.channel].offset->value();
            QString text = QStringLiteral("%1 = %2%3").arg(channel.name).arg(raw, 0, 'g', 10)
                .arg(channel.unit.isEmpty() ? QString() : " " + channel.unit);
            if (gain != 1 || offset != 0) text += QStringLiteral(" (shown %1)").arg(raw*gain+offset, 0, 'g', 10);
            const auto color = plot_->series()[hit.channel]->color();
            rows.append(QVariantMap{{"text", text}, {"color", color}});
            markers.append(QVariantMap{{"x", frame.plottedTime(i)}, {"y", raw*gain+offset}, {"color", color}});
        }
    }
    if (rows != bridge->hoverTip || markers != bridge->hoverMarkers) {
        bridge->hoverTip = rows; bridge->hoverMarkers = markers; emit bridge->hoverTipChanged();
    }
    return rows;
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
    const QString timeWork = plot_->isVisible() && timeWorker_.busy() ? " • Preparing latest Time frame" : QString();
    status_->setText(QStringLiteral("Run %1 • %2 • %3 • %4/%5 samples/channel • received %6 • evicted %7 • gaps %8%9%10")
        .arg(frame.runId).arg(freeze_->isChecked() ? "Frozen snapshot" : inspecting_ ? "Inspecting displayed frame" : "Following latest")
        .arg(acquisitionStatus.finished ? "Run finished" : "Acquisition active").arg(frame.times.size()).arg(live_->capacity())
        .arg(frame.received).arg(frame.evicted).arg(frame.gaps)
        .arg(acquisitionStatus.error.isEmpty() ? QString() : " • ERROR: " + acquisitionStatus.error)
        .arg((traceWarning_ + settingsError_ + suffix + timeWork).isEmpty() ? QString() : " • " + traceWarning_ + settingsError_ + suffix + timeWork));
}
} // namespace s7studio::ui
