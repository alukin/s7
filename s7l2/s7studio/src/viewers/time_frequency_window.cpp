// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-S7-plugin-permission
#include "time_frequency_window.hpp"
#include "analysis/display_curve.hpp"
#include "analysis/display_worker.hpp"
#include "analysis/worker_pool.hpp"
#include "analysis_action.hpp"
#include "line_plot_view.hpp"
#include "surface_plot.hpp"
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QScopedValueRollback>
#include <QSemaphore>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStandardItemModel>
#include <QTabWidget>
#include <QThreadPool>
#include <QTimer>
#include <QVBoxLayout>
#include <atomic>
#include <mutex>
#include <s7spectral/analysis.hpp>
#include <s7wavelet/analysis.hpp>
namespace s7studio::viewers {
namespace {
double quantityValue(const analysis::ResultFile &result, std::size_t frequency, std::size_t time,
                     int quantity) {
    const auto w = result.values[frequency * result.x.size() + time];
    if (quantity == 1) {
        const auto magnitude = std::abs(w);
        return magnitude * magnitude;
    }
    if (quantity == 2)
        return std::arg(w);
    if (quantity >= 3)
        return s7spectral::spectrogramPower(w, frequency, std::size_t(result.metadata["window_size"].toInt()),
                                            result.metadata["rate_hz"].toDouble(), quantity == 4);
    return std::abs(w);
}
QSemaphore &heavyBudget() {
    static QSemaphore budget(1);
    return budget;
}
std::size_t nearestIndex(const std::vector<double> &axis, double value) {
    const auto it = std::lower_bound(axis.begin(), axis.end(), value);
    auto index = std::min<std::size_t>(std::size_t(it - axis.begin()), axis.size() - 1);
    if (index && value - axis[index - 1] < axis[index] - value)
        --index;
    return index;
}
double initialInspectionTime(const analysis::ResultFile &result) {
    const auto middle = result.x[result.x.size() / 2];
    double peak = 0;
    for (auto value : result.input)
        peak = std::max(peak, std::abs(value));
    if (peak == 0)
        return middle;
    double energy = 0;
    for (auto value : result.input)
        energy += (value / peak) * (value / peak);
    double accumulated = 0;
    for (std::size_t i = 0; i < result.input.size(); ++i) {
        const auto value = result.input[i] / peak;
        accumulated += value * value;
        if (accumulated >= energy / 2)
            return result.metadata["origin_time"].toDouble() + double(i) / result.metadata["rate_hz"].toDouble();
    }
    return middle;
}
} // namespace
struct TimeFrequencyWindow::Work {
    std::atomic<std::uint64_t> epoch{0};
    std::atomic<bool> running{false};
    std::mutex mutex;
    std::shared_ptr<const analysis::ResultFile> ready;
    QString error;
};
struct TimeFrequencyDisplay {
    std::shared_ptr<const analysis::ResultFile> source;
    std::shared_ptr<const analysis::SurfaceGrid> grid;
    QList<QPointF> envelope;
    std::size_t bandRows = 0;
    int quantity = 0;
    bool logarithmic = false;
};
struct TimeFrequencySlice {
    std::shared_ptr<const analysis::ResultFile> source;
    int column, quantity;
    QList<QList<QPointF>> segments;
};
struct TimeFrequencyWindow::DisplayWork {
    analysis::DisplayWorker<TimeFrequencyDisplay> worker;
    analysis::DisplayWorker<TimeFrequencySlice> sliceWorker;
    std::shared_ptr<const analysis::ResultFile> sliceSource;
    int sliceColumn = -1, sliceQuantity = -1;
    std::size_t sliceRows = 0;
    bool scheduled = false, awaiting = false, sliceAwaiting = false, suspended = false, dirty = false;
    QString error;
};
ViewerType TimeFrequencyWindow::viewerType() {
    return {
        "TimeFrequency", {}, "1→map", {}, {"in"}, [](const ViewerInstanceSpec &spec) -> ObservationViewer * {
            if (spec.probeNames.size() != 1 || spec.probeNames[0].isEmpty())
                return nullptr;
            return new TimeFrequencyWindow(spec.instanceId);
        }};
}
TimeFrequencyWindow::TimeFrequencyWindow(const QString &name, QWidget *parent)
    : QWidget(parent, Qt::Window), displayWork_(std::make_unique<DisplayWork>()),
      work_(std::make_shared<Work>()) {
    setAttribute(Qt::WA_QuitOnClose, false);
    resize(1100, 850);
    setWindowTitle("Time–frequency analysis — " + name);
    auto *root = new QVBoxLayout(this);
    auto *controls = new QHBoxLayout;
    method_ = new QComboBox(this);
    method_->addItems({"STFT spectrogram", "Morlet CWT scalogram"});
    method_->setObjectName("timeFrequency.method");
    controls->addWidget(method_);
    window_ = new QComboBox(this);
    window_->setObjectName("timeFrequency.fftSize");
    for (int n = 8; n <= 65536; n *= 2)
        window_->addItem(QString::number(n), n);
    window_->setCurrentIndex(window_->findData(256));
    controls->addWidget(new QLabel("FFT size", this));
    controls->addWidget(window_);
    scales_ = new QSpinBox(this);
    scales_->setRange(2, 128);
    scales_->setValue(32);
    scales_->setEnabled(false);
    controls->addWidget(new QLabel("CWT scales", this));
    controls->addWidget(scales_);
    minimum_ = new QDoubleSpinBox(this);
    minimum_->setDecimals(3);
    minimum_->setRange(.001, 1e9);
    minimum_->setValue(2);
    maximum_ = new QDoubleSpinBox(this);
    maximum_->setDecimals(3);
    maximum_->setRange(.001, 1e9);
    maximum_->setValue(100);
    minimum_->setEnabled(false);
    maximum_->setEnabled(false);
    controls->addWidget(new QLabel("CWT Hz", this));
    controls->addWidget(minimum_);
    controls->addWidget(maximum_);
    analyze_ = new QPushButton("Analyze / freeze", this);
    analyze_->setObjectName("timeFrequency.analyze");
    emphasizeAnalysisAction(*analyze_);
    analyze_->setToolTip("Compute and freeze a time–frequency map from the captured samples.");
    connect(analyze_, &QPushButton::clicked, this, [this] {
        automaticAttempted_ = true;
        if (auto *tabs = findChild<QTabWidget *>("timeFrequency.views"))
            tabs->setCurrentIndex(0);
        follow_->setChecked(false);
        startAnalysis(!imported_);
    });
    root->addLayout(controls);
    auto *actions = new QHBoxLayout;
    actions->addWidget(analyze_);
    follow_ = new QCheckBox("Follow latest retained interval", this);
    actions->addWidget(follow_);
    auto *display = new QHBoxLayout;
    quantity_ = new QComboBox(this);
    quantity_->addItems(
        {"Magnitude", "Squared magnitude", "Phase (rad; |W| > 1e-12)", "STFT power per bin", "STFT PSD"});
    quantity_->setObjectName("timeFrequency.quantity");
    display->addWidget(quantity_);
    logFrequency_ = new QCheckBox("Log frequency axis", this);
    logFrequency_->setEnabled(false);
    display->addWidget(logFrequency_);
    connect(logFrequency_, &QCheckBox::toggled, this, [this] { present(); });
    auto *view = new QComboBox(this);
    view->addItems({"Heatmap", "3D surface", "Wireframe"});
    display->addWidget(view);
    displayMaximum_ = new QDoubleSpinBox(this);
    displayMaximum_->setObjectName("timeFrequency.displayMaximum");
    displayMaximum_->setRange(.001, 1e9);
    displayMaximum_->setDecimals(3);
    displayMaximum_->setValue(100);
    displayMaximum_->setToolTip("Display band only. The saved transform and numeric inspector retain all frequencies.");
    display->addWidget(new QLabel("Display up to (Hz)", this));
    display->addWidget(displayMaximum_);
    connect(displayMaximum_, &QDoubleSpinBox::valueChanged, this, [this] { present(); });
    for (int action = 0; action < 3; ++action) {
        auto *b =
            new QPushButton(QStringList{"Save result…", "Open result…", "Export map CSV…"}[action], this);
        actions->addWidget(b);
        connect(b, &QPushButton::clicked, this, [this, action] {
            const auto path =
                action == 1
                    ? QFileDialog::getOpenFileName(this, "Open analysis", {}, "S7 analysis (*.s7analysis)")
                    : QFileDialog::getSaveFileName(
                          this, "Save analysis",
                          action == 2 ? "time_frequency.csv" : "time_frequency.s7analysis",
                          action == 2 ? "CSV (*.csv)" : "S7 analysis (*.s7analysis)");
            if (path.isEmpty())
                return;
            try {
                if (action == 0)
                    saveResult(path);
                else if (action == 1)
                    openResult(path);
                else if (result_)
                    result_->csv(path);
            } catch (const std::exception &e) {
                QMessageBox::critical(this, "Analysis file", e.what());
            }
        });
    }
    root->addLayout(actions);
    root->addLayout(display);
    status_ = new QLabel(this);
    status_->setObjectName("viewer.status");
    status_->setWordWrap(true);
    status_->setTextFormat(Qt::PlainText);
    root->addWidget(status_);
    auto *inspectControls = new QHBoxLayout;
    row_ = new QSpinBox(this);
    column_ = new QSpinBox(this);
    row_->setObjectName("timeFrequency.row");
    column_->setObjectName("timeFrequency.column");
    row_->setRange(0, 0);
    column_->setRange(0, 0);
    inspectControls->addWidget(new QLabel("Frequency row", this));
    inspectControls->addWidget(row_);
    inspectControls->addWidget(new QLabel("Time column", this));
    inspectControls->addWidget(column_);
    inspection_ = new QLabel(this);
    inspection_->setWordWrap(true);
    inspection_->setTextFormat(Qt::PlainText);
    inspectControls->addWidget(inspection_, 1);
    root->addLayout(inspectControls);
    auto *tabs = new QTabWidget(this);
    tabs->setObjectName("timeFrequency.views");
    auto *mapPage = new QWidget(tabs);
    auto *mapLayout = new QVBoxLayout(mapPage);
    mapLayout->setContentsMargins(0, 0, 0, 0);
    empty_ = new QLabel(mapPage);
    empty_->setObjectName("timeFrequency.empty");
    empty_->setAlignment(Qt::AlignCenter);
    empty_->setWordWrap(true);
    mapLayout->addWidget(empty_, 1);
    plot_ = new SurfacePlotView(false, mapPage);
    plot_->setPresentation(2);
    plot_->hide();
    mapLayout->addWidget(plot_, 1);
    tabs->addTab(mapPage, "Time–frequency map");
    trace_ = new LinePlotView(tabs);
    trace_->setObjectName("timeFrequency.inputTrace");
    trace_->setAxisTitles("Model time (s)", "Source value");
    tabs->addTab(trace_, "Input trace");
    slice_ = new LinePlotView(tabs);
    slice_->setObjectName("timeFrequency.frequencySlice");
    slice_->setAxisTitles("Frequency (Hz)", "Coefficient magnitude");
    tabs->addTab(slice_, "Frequency slice");
    root->addWidget(tabs, 1);
    connect(view, &QComboBox::currentIndexChanged, this, [this](int i) {
        plot_->setPresentation(i == 0 ? 2 : i == 1 ? 0 : 1);
    });
    connect(quantity_, &QComboBox::currentIndexChanged, this, [this] { present(); });
    connect(row_, &QSpinBox::valueChanged, this, [this] { inspect(row_->value(), column_->value()); });
    connect(column_, &QSpinBox::valueChanged, this, [this] { inspect(row_->value(), column_->value()); });
    auto settingsChanged = [this] {
        ++work_->epoch;
        {
            std::lock_guard lock(work_->mutex);
            work_->ready.reset();
            work_->error.clear();
        }
        if (!error_.startsWith("Input gap") && !error_.startsWith("Incomplete acquisition"))
            error_.clear();
        lastSubmitted_ = 0;
        if (work_->running && requestActive_)
            pending_ = true;
        else if (result_ && !follow_->isChecked())
            startAnalysis(pending_ && currentCaptureRequested_);
    };
    connect(plot_, &SurfacePlotView::cellPicked, this, [this](int row, int column) {
        if (!result_)
            return;
        const auto nx = std::min<std::size_t>(513, result_->x.size()),
                   ny = std::min<std::size_t>(513, bandRows_);
        row_->setValue(int(ny <= 1 ? 0 : std::size_t(row) * (bandRows_ - 1) / (ny - 1)));
        column_->setValue(int(nx <= 1 ? 0 : std::size_t(column) * (result_->x.size() - 1) / (nx - 1)));
    });
    connect(trace_, &LinePlotView::pointPicked, this, [this](double time, double) {
        if (!result_)
            return;
        column_->setValue(int(nearestIndex(result_->x, time)));
    });
    connect(method_, &QComboBox::currentIndexChanged, this, [this, settingsChanged](int method) {
        window_->setEnabled(method == 0);
        scales_->setEnabled(method == 1);
        minimum_->setEnabled(method == 1);
        maximum_->setEnabled(method == 1);
        if (auto *model = qobject_cast<QStandardItemModel *>(quantity_->model()))
            for (int i = 3; i < 5; ++i)
                model->item(i)->setEnabled(method == 0);
        if (method == 1 && quantity_->currentIndex() >= 3)
            quantity_->setCurrentIndex(0);
        logFrequency_->setEnabled(method == 1);
        logFrequency_->setChecked(method == 1);
        settingsChanged();
    });
    connect(window_, &QComboBox::currentIndexChanged, this, settingsChanged);
    connect(scales_, &QSpinBox::valueChanged, this, settingsChanged);
    connect(minimum_, &QDoubleSpinBox::valueChanged, this, settingsChanged);
    connect(maximum_, &QDoubleSpinBox::valueChanged, this, settingsChanged);
    auto *timer = new QTimer(this);
    timer->setInterval(100);
    connect(timer, &QTimer::timeout, this, &TimeFrequencyWindow::poll);
    timer->start();
    auto *displayTimer = new QTimer(this);
    displayTimer->setInterval(25);
    connect(displayTimer, &QTimer::timeout, this, &TimeFrequencyWindow::applyDisplay);
    displayTimer->start();
    connect(plot_, &SurfacePlotView::presentationReady, this,
            [this] { inspect(row_->value(), column_->value()); });
    updateStatus();
}
TimeFrequencyWindow::~TimeFrequencyWindow() { ++work_->epoch; }
void TimeFrequencyWindow::setWavelet(bool enabled) { method_->setCurrentIndex(enabled ? 1 : 0); }
void TimeFrequencyWindow::appendObservedSample(const ObservedSample &s) {
    if (imported_ || s.role != "in")
        return;
    if (previous_ && s.step != previous_ + 1)
        error_ = "Input gap: clear/reopen a continuous interval before analysis";
    previous_ = s.step;
    unit_ = s.unit;
    samples_.push_back({s.step, s.timeSeconds, s.value});
    if (samples_.size() > 65536)
        samples_.pop_front();
}
void TimeFrequencyWindow::setDroppedSampleCount(std::uint64_t count) {
    if (count)
        error_ = "Incomplete acquisition: transform unavailable";
}
bool TimeFrequencyWindow::refreshSource() {
    if (imported_)
        return false;
    if (automaticSource_.lock() != source()) {
        automaticSource_ = source();
        automaticAttempted_ = false;
    }
    const auto more = ObservationViewer::refreshSource();
    loaded_ = !more && !sourceStatusText().startsWith("INCOMPLETE");
    return more;
}
bool TimeFrequencyWindow::isPreparingDisplay() const {
    return displayWork_->scheduled || displayWork_->awaiting || displayWork_->dirty ||
           (plot_->isVisible() && plot_->isPreparing()) || (slice_->isVisible() && displayWork_->sliceAwaiting);
}
void TimeFrequencyWindow::hideEvent(QHideEvent *event) {
    displayWork_->suspended = true;
    displayWork_->dirty = displayWork_->dirty || displayWork_->awaiting || displayWork_->scheduled;
    displayWork_->worker.cancel();
    displayWork_->sliceWorker.cancel();
    displayWork_->sliceAwaiting = false;
    displayWork_->sliceSource.reset();
    displayWork_->awaiting = false;
    QWidget::hideEvent(event);
}
void TimeFrequencyWindow::showEvent(QShowEvent *event) {
    displayWork_->suspended = false;
    if (displayWork_->dirty && result_)
        present();
    else if (result_)
        inspect(row_->value(), column_->value());
    QWidget::showEvent(event);
}
void TimeFrequencyWindow::clearSamples() {
    displayWork_->worker.cancel();
    displayWork_->sliceWorker.cancel();
    displayWork_->sliceAwaiting = false;
    displayWork_->sliceSource.reset();
    displayWork_->awaiting = false;
    displayWork_->dirty = false;
    ++work_->epoch;
    {
        std::lock_guard lock(work_->mutex);
        work_->ready.reset();
        work_->error.clear();
    }
    discardPresentationHistory();
    samples_.clear();
    previous_ = 0;
    loaded_ = false;
    pending_ = false;
    requestActive_ = false;
    currentCaptureRequested_ = false;
    automaticAttempted_ = true; // Clearing a presentation must not immediately recreate it.
    imported_ = false;
    error_.clear();
    result_.reset();
    lastSubmitted_ = 0;
    plot_->setGrid({});
    trace_->clearSeries();
    traceMarker_ = nullptr;
    slice_->clearSeries();
    updateStatus();
}
void TimeFrequencyWindow::analyze() {
    startAnalysis(false);
}
void TimeFrequencyWindow::startAnalysis(bool currentCapture) {
    automaticAttempted_ = true;
    currentCaptureRequested_ = currentCapture;
    if (imported_ && (!result_ || result_->input.empty())) {
        pending_ = false;
        requestActive_ = false;
        error_ = "This imported result has no saved input to reanalyze";
        updateStatus();
        return;
    }
    if (work_->running) {
        pending_ = true;
        requestActive_ = true;
        ++work_->epoch;
        updateStatus();
        return;
    }
    pending_ = false;
    requestActive_ = false;
    if (!error_.isEmpty())
        return;
    const bool frozen = !currentCapture && result_ && !follow_->isChecked() && !result_->input.empty();
    if (!frozen && source() && source()->status().incomplete()) {
        error_ = "Incomplete source capture: transform unavailable";
        return;
    }
    if (!frozen && samples_.size() < 2)
        return;
    const double dt = frozen ? 1 / result_->metadata["rate_hz"].toDouble()
                             : (samples_.back().time - samples_.front().time) / double(samples_.size() - 1);
    if (!std::isfinite(dt) || dt <= 0) {
        error_ = "Invalid model-time rate";
        return;
    }
    if (!frozen)
        for (std::size_t i = 1; i < samples_.size(); ++i)
            if (std::abs(samples_[i].time - samples_[i - 1].time - dt) > dt * 1e-7) {
                error_ = "Nonuniform model-time samples";
                return;
            }
    const auto n = window_->currentData().toInt();
    if (method_->currentIndex() == 0 && (n & (n - 1))) {
        error_ = "FFT size must be a power of two";
        return;
    }
    const double low = minimum_->value(), high = maximum_->value();
    if (method_->currentIndex() == 1 && (!(low < high) || high >= .5 / dt)) {
        error_ = "CWT requires 0 < minimum < maximum < Nyquist";
        return;
    }
    if (!heavyBudget().tryAcquire()) {
        pending_ = true;
        requestActive_ = true;
        updateStatus();
        status_->setText("Another time–frequency job holds the shared memory reservation; retry shortly");
        return;
    }
    const auto epoch = ++work_->epoch;
    {
        std::lock_guard lock(work_->mutex);
        work_->ready.reset();
        work_->error.clear();
    }
    requestActive_ = true;
    work_->running = true;
    lastSubmitted_ = samples_.empty() ? 0 : std::uint64_t(samples_.back().step);
    std::vector<double> input;
    if (frozen)
        input = result_->input;
    else {
        input.reserve(samples_.size());
        for (auto s : samples_)
            input.push_back(s.value);
    }
    const auto origin = frozen ? result_->metadata["origin_time"].toDouble() : samples_.front().time;
    const auto first =
                   frozen ? result_->metadata["first_step"].toString().toLongLong() : samples_.front().step,
               last = frozen ? result_->metadata["last_step"].toString().toLongLong() : samples_.back().step;
    const int method = method_->currentIndex(), scales = scales_->value();
    const auto provenance = frozen     ? result_->metadata["provenance"].toObject()
                            : source() ? source()->provenance()
                                       : QJsonObject{};
    const auto unit = frozen ? result_->metadata["unit"].toString() : unit_;
    QJsonObject identity = frozen ? result_->metadata : QJsonObject{};
    if (!frozen) {
        identity["run_id"] = source() ? QString::number(source()->runId()) : QString("unavailable");
        QJsonArray ids, names;
        for (auto id : selectedChannels({"in"})) {
            ids.append(qint64(id));
            if (source())
                names.append(source()->channels()[id].name);
        }
        identity["channel_ids"] = ids;
        identity["channel_names"] = names;
    }

    analysis::analysisPool().start([state = work_, epoch, input = std::move(input), dt, n, low, high, origin,
                                    first, last, method, scales, provenance, unit, identity] {
        std::shared_ptr<analysis::ResultFile> result;
        QString error;
        try {
            const auto cancel = [&] { return state->epoch != epoch; };
            s7spectral::TimeFrequency map;
            if (method == 0)
                map = s7spectral::spectrogram(input, 1 / dt, std::size_t(n), std::size_t(n / 2), cancel);
            else {
                std::vector<double> frequencies;
                for (int s = 0; s < scales; ++s)
                    frequencies.push_back(low * std::pow(high / low, double(s) / double(scales - 1)));
                map = s7wavelet::cwt(input, 1 / dt, frequencies, 6, cancel);
            }
            result = std::make_shared<analysis::ResultFile>();
            result->kind = "time_frequency";
            result->x = std::move(map.times);
            for (auto &t : result->x)
                t += origin;
            result->y = std::move(map.frequencies);
            const auto nx = result->x.size(), ny = result->y.size();
            result->values = std::move(map.coefficients);
            result->valid = std::move(map.valid);
            // Transpose in place so the full coefficient matrix is never duplicated.
            std::vector<bool> visited(nx * ny, false);
            for (std::size_t start = 0; start < nx * ny; ++start) {
                if (visited[start])
                    continue;
                auto current = start;
                auto value = result->values[current];
                bool valid = result->valid[current];
                do {
                    visited[current] = true;
                    const auto next = (current % ny) * nx + current / ny;
                    std::swap(value, result->values[next]);
                    const bool old = result->valid[next];
                    result->valid[next] = valid;
                    valid = old;
                    current = next;
                } while (current != start);
            }
            result->metadata = {{"method", method == 0 ? "STFT periodic Hann, peak-tone amplitude"
                                                       : "Morlet omega0=6, discrete L2, support ±4 sigma"},
                                {"rate_hz", 1 / dt},
                                {"window_size", n},
                                {"hop_size", n / 2},
                                {"boundary", "zero extension; validity requires full support"},
                                {"unit", unit},
                                {"first_step", QString::number(first)},
                                {"last_step", QString::number(last)},
                                {"provenance", provenance},
                                {"origin_time", origin}};
            result->metadata["run_id"] = identity["run_id"];
            result->metadata["channel_ids"] = identity["channel_ids"];
            result->metadata["channel_names"] = identity["channel_names"];
            result->input = std::move(input);
            result->validate();
        } catch (const std::exception &e) {
            error = QString::fromUtf8(e.what());
        }
        {
            std::lock_guard lock(state->mutex);
            if (state->epoch == epoch) {
                state->ready = std::move(result);
                state->error = error;
            }
            state->running = false;
        }
        heavyBudget().release();
    });
    updateStatus();
}
void TimeFrequencyWindow::poll() {
    if (polling_)
        return;
    const QScopedValueRollback guard(polling_, true);
    if (!isVisible() || isMinimized() || trace_->isVisible()) {
        if (work_->running && requestActive_) {
            pending_ = true;
            ++work_->epoch;
        }
        updateStatus();
        return;
    }
    std::shared_ptr<const analysis::ResultFile> ready;
    QString error;
    {
        std::lock_guard lock(work_->mutex);
        ready = std::move(work_->ready);
        error = std::move(work_->error);
        work_->error.clear();
    }
    if (ready) {
        requestActive_ = false;
        const double time = result_ ? result_->x[std::size_t(column_->value())]
                                    : initialInspectionTime(*ready);
        const auto frequency = result_ ? std::optional(result_->y[std::size_t(row_->value())]) : std::nullopt;
        result_ = std::move(ready);
        error_.clear();
        chooseInspection(time, frequency);
        present();
    }
    if (!error.isEmpty()) {
        requestActive_ = false;
        pending_ = false;
        error_ = std::move(error);
    }
    if (pending_ && requestActive_ && !work_->running) {
        pending_ = false;
        startAnalysis(currentCaptureRequested_);
    }
    if (follow_->isChecked() && !samples_.empty() && !work_->running &&
        std::uint64_t(samples_.back().step) >= lastSubmitted_ + 512)
        analyze();
    if (!automaticAttempted_ && !result_ && !work_->running && !pending_ && !imported_ &&
        !follow_->isChecked() && error_.isEmpty() && loaded_ && source() &&
        source()->status().finished && !source()->status().incomplete() && smallAutomaticJob())
        analyze();
    updateStatus();
}
bool TimeFrequencyWindow::smallAutomaticJob() const {
    if (samples_.size() < 2 || samples_.size() > 8192)
        return false;
    if (method_->currentIndex() == 0)
        return samples_.size() >= std::size_t(window_->currentData().toInt());
    const double dt = (samples_.back().time - samples_.front().time) / double(samples_.size() - 1);
    const auto low = minimum_->value(), high = maximum_->value();
    if (!(dt > 0) || !std::isfinite(dt) || !(low < high) || high >= .5 / dt || scales_->value() > 32)
        return false;
    // Keep automatic CWT jobs within the default 8k-sample / 16k-FFT workload.
    const double half = std::ceil(24 / (2 * std::numbers::pi * low * dt));
    return std::isfinite(half) && half < double(samples_.size()) / 2 &&
           double(samples_.size()) + 2 * half <= 16384;
}
void TimeFrequencyWindow::updateStatus() {
    if (!status_)
        return;
    const bool ready = error_.isEmpty() &&
        ((result_ && !result_->input.empty()) || (!imported_ && loaded_ &&
          samples_.size() >= (method_->currentIndex() == 0 ? std::size_t(window_->currentData().toInt()) : 2)));
    const bool computing = work_->running && requestActive_, waiting = pending_ && requestActive_;
    const bool canRecompute = !imported_ || (result_ && !result_->input.empty());
    method_->setEnabled(canRecompute);
    window_->setEnabled(canRecompute && method_->currentIndex() == 0);
    for (auto *control : std::array<QWidget *, 3>{scales_, minimum_, maximum_})
        control->setEnabled(canRecompute && method_->currentIndex() == 1);
    analyze_->setEnabled(ready && !computing && (!waiting || trace_->isVisible()));
    analyze_->setText(computing ? "Analyzing…" : waiting && !trace_->isVisible() ? "Waiting to analyze…" : "Analyze / freeze");
    empty_->setVisible(!result_);
    plot_->setVisible(bool(result_));
    empty_->setText(computing || waiting ? "Preparing the time–frequency map…"
        : !error_.isEmpty() ? "Analysis unavailable\n" + error_
        : ready ? "Ready to create the map\nChoose the blue Analyze / freeze button above."
        : loaded_ && !samples_.empty() ? "More samples needed for these settings\nChoose a smaller FFT size or capture a longer interval."
                : "No captured data yet\nRun or open a recording. Small completed captures are analyzed automatically.");
    status_->setText(QString("%1 retained samples (limit 65536) | %2 | display ≤513×513; full-resolution "
                             "inspector/export | %3%4")
                         .arg(samples_.size())
                         .arg(computing ? "computing"
                              : waiting ? "waiting to analyze"
                              : result_      ? "frozen result; gray = edge/phase/overflow unavailable"
                                             : "choose Analyze / freeze to create the map")
                         .arg(imported_ ? "Imported result; file provenance retained" : sourceStatusText())
                         .arg(error_.isEmpty() ? "" : " | " + error_));
    if (isPreparingDisplay())
        status_->setText(status_->text() + " | preparing display");
    if (!displayWork_->error.isEmpty())
        status_->setText(status_->text() + " | Display unavailable: " + displayWork_->error);
}
void TimeFrequencyWindow::chooseInspection(double time, std::optional<double> frequency) {
    const auto &r = *result_;
    const QSignalBlocker blockRow(row_), blockColumn(column_);
    column_->setMaximum(int(r.x.size() - 1));
    row_->setMaximum(int(r.y.size() - 1));
    const auto column = nearestIndex(r.x, time);
    column_->setValue(int(column));
    std::size_t row = 0;
    if (frequency)
        row = nearestIndex(r.y, *frequency);
    else {
        double peak = -1;
        for (std::size_t f = 0; f < r.y.size() && r.y[f] <= displayMaximum_->value(); ++f) {
            const auto i = f * r.x.size() + column;
            const auto magnitude = std::abs(r.values[i]);
            if (r.valid[i] && magnitude > peak) {
                peak = magnitude;
                row = f;
            }
        }
    }
    row_->setValue(int(row));
}
void TimeFrequencyWindow::present() {
    displayWork_->worker.cancel();
    displayWork_->awaiting = false;
    if (!result_)
        return;
    displayWork_->dirty = true;
    if (displayWork_->suspended)
        return;
    {
        const QSignalBlocker block(displayMaximum_);
        displayMaximum_->setRange(result_->y[std::min<std::size_t>(1, result_->y.size() - 1)],
                                  result_->y.back());
    }
    if (!result_->metadata["method"].toString().startsWith("STFT") && quantity_->currentIndex() >= 3) {
        const QSignalBlocker block(quantity_);
        quantity_->setCurrentIndex(0);
    }
    if (!displayWork_->scheduled) {
        displayWork_->scheduled = true;
        QTimer::singleShot(0, this, [this] {
            displayWork_->scheduled = false;
            submitDisplay();
        });
    }
}
void TimeFrequencyWindow::submitDisplay() {
    if (!result_ || displayWork_->suspended)
        return;
    displayWork_->dirty = false;
    displayWork_->awaiting = true;
    const auto source = result_;
    const int quantity = quantity_->currentIndex();
    const double maximum = displayMaximum_->value();
    const bool logarithmicFrequency = logFrequency_->isChecked();
    displayWork_->worker.submit([source, quantity, maximum,
                                 logarithmicFrequency](const auto &canceled) -> TimeFrequencyDisplay {
        TimeFrequencyDisplay frame;
        frame.source = source;
        frame.quantity = quantity;
        const auto &r = *source;
        frame.bandRows = std::clamp<std::size_t>(
            std::size_t(std::upper_bound(r.y.begin(), r.y.end(), maximum) - r.y.begin()),
            std::min<std::size_t>(2, r.y.size()), r.y.size());
        const auto nx = std::min<std::size_t>(513, r.x.size()),
                   ny = std::min<std::size_t>(513, frame.bandRows);
        auto grid = std::make_shared<analysis::SurfaceGrid>();
        grid->x.resize(nx);
        grid->y.resize(ny);
        grid->values.resize(nx * ny);
        grid->present.resize(nx * ny);
        grid->received = nx * ny;
        for (std::size_t t = 0; t < nx; ++t)
            grid->x[t] = r.x[nx == 1 ? 0 : t * (r.x.size() - 1) / (nx - 1)];
        for (std::size_t f = 0; f < ny; ++f)
            grid->y[f] = r.y[ny == 1 ? 0 : f * (frame.bandRows - 1) / (ny - 1)];
        for (std::size_t f = 0; f < ny; ++f)
            for (std::size_t t = 0; t < nx; ++t) {
                if (canceled())
                    return {};
                const auto tf = ny == 1 ? 0 : f * (frame.bandRows - 1) / (ny - 1),
                           tt = nx == 1 ? 0 : t * (r.x.size() - 1) / (nx - 1), i = tf * r.x.size() + tt;
                const auto w = r.values[i];
                const double value = quantityValue(r, tf, tt, quantity);
                grid->values[f * nx + t] = {value, 0};
                grid->present[f * nx + t] = r.valid[i] && (quantity != 2 || std::abs(w) > 1e-12);
            }
        const bool logarithmic = logarithmicFrequency && grid->y.front() > 0;
        frame.logarithmic = logarithmic;
        frame.grid = std::move(grid);
        auto &points = frame.envelope;
        const auto buckets = std::min<std::size_t>(1024, r.input.size());
        for (std::size_t i = 0; i < buckets; ++i) {
            if (canceled())
                return {};
            const auto first = i * r.input.size() / buckets, last = (i + 1) * r.input.size() / buckets;
            const auto extrema = std::minmax_element(r.input.begin() + first, r.input.begin() + last);
            std::array<std::size_t, 4> indices{first, std::size_t(extrema.first - r.input.begin()),
                                               std::size_t(extrema.second - r.input.begin()), last - 1};
            std::sort(indices.begin(), indices.end());
            const auto end = std::unique(indices.begin(), indices.end());
            for (auto it = indices.begin(); it != end; ++it)
                points.append(
                    {r.metadata["origin_time"].toDouble() + double(*it) / r.metadata["rate_hz"].toDouble(),
                     r.input[*it]});
        }
        return frame;
    });
}
void TimeFrequencyWindow::applyDisplay() {
    if (auto slice = displayWork_->sliceWorker.take()) {
        displayWork_->sliceAwaiting = false;
        if (!slice->error.isEmpty())
            inspection_->setText("Slice unavailable: " + slice->error);
        else if (slice->value->source == result_ && slice->value->column == column_->value() &&
                 slice->value->quantity == quantity_->currentIndex()) {
            slice_->clearSeries();
            slice_->setAxisTitles("Frequency (Hz)", quantity_->currentText());
            for (const auto &points : slice->value->segments) {
                auto *line = slice_->addSeries("Selected time", {QColor("#1565c0")});
                LinePlotView::setPoints(line, points);
            }
            (void)slice_->fitAll();
        }
    }
    auto ready = displayWork_->worker.take();
    if (!ready)
        return;
    displayWork_->awaiting = false;
    if (!ready->error.isEmpty()) {
        displayWork_->error = ready->error;
        updateStatus();
        return;
    }
    displayWork_->error.clear();
    const auto &frame = *ready->value;
    if (frame.source != result_)
        return;
    const auto &r = *frame.source;
    bandRows_ = frame.bandRows;
    plot_->setCyclicColors(frame.quantity == 2);
    plot_->setLogY(frame.logarithmic);
    const auto unit =
        r.metadata["unit"].toString().isEmpty() ? QString("U (unspecified)") : r.metadata["unit"].toString();
    const auto valueUnit = quantity_->currentIndex() == 2                                     ? QString("rad")
                           : quantity_->currentIndex() == 4                                   ? unit + "²/Hz"
                           : quantity_->currentIndex() == 1 || quantity_->currentIndex() == 3 ? unit + "²"
                                                                                              : unit;
    plot_->setAxes({"Model time (s)", "Frequency (Hz)", quantity_->currentText() + " (" + valueUnit + ")"});
    plot_->setGrid(frame.grid);
    row_->setMaximum(int(r.y.size() - 1));
    column_->setMaximum(int(r.x.size() - 1));
    trace_->setAxisTitles("Model time (s)",
                          "Input (" +
                              (r.metadata["unit"].toString().isEmpty() ? QString("unit unspecified")
                                                                       : r.metadata["unit"].toString()) +
                              ")");
    trace_->clearSeries();
    traceMarker_ = trace_->addScatterSeries("Selected analysis time", QColor("#d62728"));
    auto *line = trace_->addSeries("Input envelope (extrema retained)", {QColor("#1565c0")});
    LinePlotView::setPoints(line, frame.envelope);
    (void)trace_->fitAll();
    inspect(row_->value(), column_->value());
    inspection_->setToolTip("Map drawing uses at most 513×513 selected cells. Numeric inspection and export "
                            "retain the full transform.");
}
void TimeFrequencyWindow::inspect(int row, int column) {
    if (displayWork_->awaiting || displayWork_->scheduled || displayWork_->dirty)
        return;
    if (!result_ || row < 0 || column < 0 || std::size_t(row) >= result_->y.size() ||
        std::size_t(column) >= result_->x.size())
        return;
    const auto &r = *result_;
    const auto i = std::size_t(row) * r.x.size() + std::size_t(column);
    const auto w = r.values[i];
    inspection_->setText(QString("%1 s, %2 Hz | coefficient (%3, %4), magnitude %5 | %6")
                             .arg(r.x[std::size_t(column)], 0, 'g', 10)
                             .arg(r.y[std::size_t(row)], 0, 'g', 10)
                             .arg(w.real(), 0, 'g', 10)
                             .arg(w.imag(), 0, 'g', 10)
                             .arg(std::abs(w), 0, 'g', 10)
                             .arg(r.valid[i] ? "full support" : "EDGE AFFECTED"));
    inspection_->setText(
        inspection_->text() +
        (quantity_->currentIndex() == 2 && std::abs(w) <= 1e-12
             ? QString(" | phase undefined below threshold")
             : QString(" | %1=%2")
                   .arg(quantity_->currentText())
                   .arg(quantityValue(r, std::size_t(row), std::size_t(column), quantity_->currentIndex()), 0,
                        'g', 12)));
    if (traceMarker_ && !r.input.empty()) {
        const auto raw = std::clamp<long long>(
            std::llround((r.x[std::size_t(column)] - r.metadata["origin_time"].toDouble()) *
                         r.metadata["rate_hz"].toDouble()),
            0, static_cast<long long>(r.input.size() - 1));
        LinePlotView::setScatterPoints(traceMarker_, {{r.metadata["origin_time"].toDouble() +
                                                           double(raw) / r.metadata["rate_hz"].toDouble(),
                                                       r.input[std::size_t(raw)]}});
    }
    const auto nx = std::min<std::size_t>(513, r.x.size()), ny = std::min<std::size_t>(513, bandRows_);
    if (!plot_->isPreparing() && std::size_t(row) < bandRows_)
        plot_->selectCell(int(bandRows_ <= 1 ? 0 : std::size_t(row) * (ny - 1) / (bandRows_ - 1)),
                          int(r.x.size() <= 1 ? 0 : std::size_t(column) * (nx - 1) / (r.x.size() - 1)));
    else
        plot_->bridge()->setHead(false, {});
    if (displayWork_->sliceSource == result_ && displayWork_->sliceColumn == column &&
        displayWork_->sliceQuantity == quantity_->currentIndex() && displayWork_->sliceRows == bandRows_)
        return;
    displayWork_->sliceSource = result_;
    displayWork_->sliceColumn = column;
    displayWork_->sliceQuantity = quantity_->currentIndex();
    displayWork_->sliceRows = bandRows_;
    const auto source = result_;
    const auto rows = bandRows_;
    const int quantity = quantity_->currentIndex();
    displayWork_->sliceAwaiting = true;
    displayWork_->sliceWorker.submit([source, rows, quantity,
                                      column](const auto &canceled) -> TimeFrequencySlice {
        TimeFrequencySlice frame{source, column, quantity, {}};
        const auto &r = *source;
        QList<QPointF> points;
        int segments = 0;
        auto flush = [&] {
            if (points.isEmpty())
                return;
            if (segments++ < 64) {
                frame.segments.append(analysis::displayCurve(points));
            }
            points.clear();
        };
        for (std::size_t f = 0; f < rows; ++f) {
            if ((f % 256) == 0 && canceled())
                return {};
            const auto index = f * r.x.size() + std::size_t(column);
            if (!r.valid[index] || (quantity == 2 && std::abs(r.values[index]) <= 1e-12)) {
                flush();
                continue;
            }
            const auto value = quantityValue(r, f, std::size_t(column), quantity);
            if (!std::isfinite(value)) {
                flush();
                continue;
            }
            if (quantity == 2 && !points.empty() && std::abs(value - points.back().y()) > std::numbers::pi)
                flush();
            points.append({r.y[f], value});
        }
        flush();
        return frame;
    });
}
void TimeFrequencyWindow::saveResult(const QString &path) const {
    if (!result_)
        throw std::invalid_argument("No completed transform");
    result_->save(path);
}
void TimeFrequencyWindow::openResult(const QString &path) {
    auto result = analysis::ResultFile::open(path);
    if (result.kind != "time_frequency")
        throw std::invalid_argument("Expected a time–frequency result");
    const double rate = result.metadata["rate_hz"].toDouble(-1),
                 origin = result.metadata["origin_time"].toDouble(NAN);
    const auto method = result.metadata["method"].toString();
    const bool wavelet = method.startsWith("Morlet");
    if (!std::isfinite(rate) || rate <= 0 || !std::isfinite(origin) || result.y.front() < 0 ||
        result.y.back() > rate / 2 || (!wavelet && !method.startsWith("STFT")) ||
        (wavelet && (result.y.front() <= 0 || result.y.back() >= rate / 2)))
        throw std::invalid_argument("Invalid transform rate, frequency coordinates or method");
    const int fftSize = result.metadata["window_size"].toInt(-1);
    if (!wavelet && (fftSize < 8 || fftSize > 65536 || (fftSize & (fftSize - 1)) ||
                     result.y.size() != std::size_t(fftSize / 2 + 1)))
        throw std::invalid_argument("Invalid STFT window/frequency dimensions");
    if (wavelet && (result.y.size() < 2 || result.y.size() > 128))
        throw std::invalid_argument("Invalid CWT scale count");
    if (!result.input.empty()) {
        bool a = false, b = false;
        const auto first = result.metadata["first_step"].toString().toLongLong(&a),
                   last = result.metadata["last_step"].toString().toLongLong(&b);
        if (!a || !b || first < 0 || last < first || quint64(last - first) + 1 != result.input.size())
            throw std::invalid_argument("Frozen input step interval does not match its frame");
    }
    {
        QSignalBlocker methodBlock(method_), windowBlock(window_), scalesBlock(scales_),
            minimumBlock(minimum_), maximumBlock(maximum_), logBlock(logFrequency_);
        method_->setCurrentIndex(wavelet ? 1 : 0);
        if (auto *model = qobject_cast<QStandardItemModel *>(quantity_->model()))
            for (int i = 3; i < 5; ++i)
                model->item(i)->setEnabled(!wavelet);
        window_->setCurrentIndex(window_->findData(result.metadata["window_size"].toInt(256)));
        window_->setEnabled(!wavelet);
        scales_->setEnabled(wavelet);
        minimum_->setEnabled(wavelet);
        maximum_->setEnabled(wavelet);
        if (wavelet) {
            scales_->setValue(int(result.y.size()));
            minimum_->setValue(result.y.front());
            maximum_->setValue(result.y.back());
        }
        logFrequency_->setEnabled(wavelet);
        logFrequency_->setChecked(wavelet);
    }

    ++work_->epoch;
    {
        std::lock_guard lock(work_->mutex);
        work_->ready.reset();
        work_->error.clear();
    }
    pending_ = false;
    requestActive_ = false;
    currentCaptureRequested_ = false;
    displayWork_->worker.cancel();
    displayWork_->sliceWorker.cancel();
    displayWork_->sliceAwaiting = false;
    displayWork_->sliceSource.reset();
    displayWork_->awaiting = false;
    result_ = std::make_shared<const analysis::ResultFile>(std::move(result));
    chooseInspection(initialInspectionTime(*result_));
    imported_ = true;
    follow_->setChecked(false);
    error_.clear();
    present();
    updateStatus();
}
} // namespace s7studio::viewers
