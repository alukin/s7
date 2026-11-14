// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-S7-plugin-permission
#include "response_window.hpp"
#include "analysis/display_curve.hpp"
#include "analysis/display_worker.hpp"
#include "analysis/result_file.hpp"
#include "analysis/worker_pool.hpp"
#include "analysis_action.hpp"
#include "line_plot_view.hpp"
#include "surface_plot.hpp"
#include "trajectory_plot.hpp"
#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QSaveFile>
#include <QTabWidget>
#include <QTextStream>
#include <QThreadPool>
#include <QTimer>
#include <QVBoxLayout>
#include <atomic>
#include <mutex>
#include <s7spectral/analysis.hpp>

namespace s7studio::viewers {
namespace {
std::vector<double> phaseDegrees(const std::vector<std::complex<double>> &values, bool unwrap) {
    std::vector<double> phases;
    phases.reserve(values.size());
    bool previous = false;
    for (const auto value : values) {
        double phase = std::arg(value) * 180 / std::numbers::pi;
        if (unwrap && previous)
            phase += 360 * std::round((phases.back() - phase) / 360);
        phases.push_back(phase);
        previous = std::abs(value) > 0;
    }
    return phases;
}
}
struct ResponseWindow::Work {
    std::atomic<std::uint64_t> epoch{0};
    std::atomic<bool> running{false};
    std::mutex mutex;
    std::shared_ptr<const s7spectral::Response> ready;
    QString error;
};
struct ResponseDisplay {
    std::shared_ptr<const s7spectral::Response> source;
    std::shared_ptr<const analysis::SurfaceGrid> landscape;
    std::array<QList<QList<QPointF>>, 4> curves, references;
    std::vector<double> phases;
    TrajectoryBounds bounds;
    TrajectoryData trajectory;
    bool truncated = false, unwrapped = true;
};
struct ResponseWindow::DisplayWork {
    analysis::DisplayWorker<ResponseDisplay> worker;
    std::shared_ptr<const s7spectral::Response> displayedSource;
    bool scheduled = false, awaiting = false, suspended = false, dirty = false;
    QString error;
};
ViewerType ResponseWindow::viewerType() {
    return {"FrequencyResponse",
            {},
            "2→response",
            {},
            {"input", "output"},
            [](const ViewerInstanceSpec &spec) -> ObservationViewer * {
                if (spec.probeNames.size() != 2 || spec.probeNames[0].isEmpty() ||
                    spec.probeNames[1].isEmpty() || spec.probeNames[0] == spec.probeNames[1])
                    return nullptr;
                return new ResponseWindow(spec.instanceId);
            }};
}
ResponseWindow::ResponseWindow(const QString &name, QWidget *parent)
    : QWidget(parent, Qt::Window), displayWork_(std::make_unique<DisplayWork>()),
      work_(std::make_shared<Work>()), status_(new QLabel(this)), inspection_(new QLabel(this)),
      analyze_(new QPushButton("Analyze captured impulse", this)), reference_(new QDoubleSpinBox(this)),
      frequency_(new QDoubleSpinBox(this)), trajectory_(new ui::TrajectoryPlotView(this)) {
    trajectory_->bridge()->setFrequencyColors(true);
    setAttribute(Qt::WA_QuitOnClose, false);
    resize(980, 800);
    setWindowTitle("Frequency response — " + name);
    auto *root = new QVBoxLayout(this);
    auto *controls = new QHBoxLayout;
    analyze_->setObjectName("response.analyze");
    emphasizeAnalysisAction(*analyze_);
    controls->addWidget(analyze_);
    connect(analyze_, &QPushButton::clicked, this, &ResponseWindow::analyze);
    reference_->setDecimals(9);
    reference_->setRange(1e-9, 1e9);
    reference_->setValue(1);
    reference_->setObjectName("response.reference");
    controls->addWidget(new QLabel("Gain dB reference", this));
    controls->addWidget(reference_);
    connect(reference_, &QDoubleSpinBox::valueChanged, this, [this] { draw(); });
    auto *save = new QPushButton("Export response CSV", this);
    controls->addWidget(save);
    connect(save, &QPushButton::clicked, this, &ResponseWindow::exportResult);
    for (bool save : {true, false}) {
        auto *button = new QPushButton(save ? "Save result…" : "Open result…", this);
        controls->addWidget(button);
        connect(button, &QPushButton::clicked, this, [this, save] {
            const auto path =
                save ? QFileDialog::getSaveFileName(this, "Save response", "response.s7analysis",
                                                    "S7 analysis (*.s7analysis)")
                     : QFileDialog::getOpenFileName(this, "Open response", {}, "S7 analysis (*.s7analysis)");
            if (path.isEmpty())
                return;
            try {
                if (save)
                    saveResult(path);
                else
                    openResult(path);
            } catch (const std::exception &e) {
                QMessageBox::critical(this, "Analysis result", e.what());
            }
        });
    }
    root->addLayout(controls);
    auto *referenceControls = new QHBoxLayout;
    referenceEnabled_ = new QCheckBox("Analytical second-order reference", this);
    referenceEnabled_->setObjectName("response.referenceModel");
    referenceControls->addWidget(referenceEnabled_);
    omega_ = new QDoubleSpinBox(this);
    omega_->setObjectName("response.omega");
    omega_->setRange(.001, 1e6);
    omega_->setDecimals(6);
    omega_->setValue(2 * std::numbers::pi * 5);
    damping_ = new QDoubleSpinBox(this);
    damping_->setObjectName("response.damping");
    damping_->setRange(.001, 10);
    damping_->setDecimals(4);
    damping_->setValue(.3);
    referenceControls->addWidget(new QLabel("ω0 (rad/s)", this));
    referenceControls->addWidget(omega_);
    referenceControls->addWidget(new QLabel("Damping ratio", this));
    referenceControls->addWidget(damping_);
    modelGain_ = new QDoubleSpinBox(this);
    modelGain_->setObjectName("response.modelGain");
    modelGain_->setDecimals(9);
    modelGain_->setRange(1e-9, 1e9);
    modelGain_->setValue(1);
    referenceControls->addWidget(new QLabel("DC gain", this));
    referenceControls->addWidget(modelGain_);
    root->addLayout(referenceControls);
    root->addWidget(new QLabel("Blue: measured response; orange: continuous reference. 3D color: blue (low "
                               "frequency) → red (high frequency).",
                               this));
    for (auto *control : {omega_, damping_, modelGain_})
        control->setEnabled(false);
    connect(referenceEnabled_, &QCheckBox::toggled, this, [this](bool enabled) {
        for (auto *control : {omega_, damping_, modelGain_})
            control->setEnabled(enabled);
        draw();
    });
    connect(omega_, &QDoubleSpinBox::valueChanged, this, [this] { draw(); });
    connect(damping_, &QDoubleSpinBox::valueChanged, this, [this] { draw(); });
    connect(modelGain_, &QDoubleSpinBox::valueChanged, this, [this] { draw(); });
    status_->setWordWrap(true);
    status_->setTextFormat(Qt::PlainText);
    status_->setObjectName("viewer.status");
    root->addWidget(status_);
    auto *cursor = new QHBoxLayout;
    cursor->addWidget(new QLabel("Inspect frequency (Hz)", this));
    frequency_->setObjectName("response.frequency");
    frequency_->setDecimals(6);
    frequency_->setRange(0, 1e9);
    cursor->addWidget(frequency_);
    unwrap_ = new QCheckBox("Unwrap phase", this);
    unwrap_->setObjectName("response.unwrap");
    unwrap_->setChecked(true);
    cursor->addWidget(unwrap_);
    connect(unwrap_, &QCheckBox::toggled, this, [this] { draw(); });
    inspection_->setWordWrap(true);
    cursor->addWidget(inspection_, 1);
    root->addLayout(cursor);
    connect(frequency_, &QDoubleSpinBox::valueChanged, this, &ResponseWindow::inspect);
    auto *tabs = new QTabWidget(this);
    tabs->setObjectName("response.views");
    auto *bode = new QWidget(tabs);
    auto *layout = new QVBoxLayout(bode);
    for (std::size_t i = 0; i < 4; ++i) {
        plots_[i] = new LinePlotView(i < 2 ? bode : tabs);
        plots_[i]->setLegendVisible(false);
        markers_[i] = plots_[i]->addScatterSeries("Selected frequency", QColor("#d62728"));
        if (i < 2)
            layout->addWidget(plots_[i]);
    }
    plots_[0]->setAxisTitles("log10(f / 1 Hz)", "Gain (dB, explicit reference)");
    plots_[1]->setAxisTitles("log10(f / 1 Hz)", "Phase (degrees; wrapped)");
    plots_[0]->setObjectName("response.magnitude");
    plots_[1]->setObjectName("response.phase");
    plots_[2]->setAxisTitles("Re(H)", "Im(H)");
    plots_[3]->setAxisTitles("Phase (degrees; wrapped)", "Gain (dB, explicit reference)");
    tabs->addTab(bode, "Bode");
    tabs->addTab(plots_[2], "Nyquist");
    tabs->addTab(plots_[3], "Nichols");
    tabs->addTab(trajectory_, "3D response");
    landscape_ = new SurfacePlotView(true, tabs);
    landscape_->setHeightMode(SurfaceHeight::LogMagnitude);
    landscape_->setAxes({"Re(s), σ (1/s)", "Im(s), ω (rad/s)", "|G(s)|"});
    landscape_->setSliceColumn(32);
    tabs->addTab(landscape_, "Analytical landscape");
    root->addWidget(tabs, 1);
    auto *timer = new QTimer(this);
    timer->setInterval(50);
    connect(timer, &QTimer::timeout, this, &ResponseWindow::poll);
    timer->start();
    auto *displayTimer = new QTimer(this);
    displayTimer->setInterval(25);
    connect(displayTimer, &QTimer::timeout, this, &ResponseWindow::applyDisplay);
    displayTimer->start();
    updateStatus();
}
ResponseWindow::~ResponseWindow() { ++work_->epoch; }
bool ResponseWindow::hasResult() const { return bool(result_); }
bool ResponseWindow::isPreparingDisplay() const {
    return displayWork_->scheduled || displayWork_->awaiting || displayWork_->dirty ||
           (landscape_->isVisible() && landscape_->isPreparing());
}
void ResponseWindow::hideEvent(QHideEvent *event) {
    displayWork_->suspended = true;
    displayWork_->dirty = displayWork_->dirty || displayWork_->scheduled || displayWork_->awaiting;
    displayWork_->worker.cancel();
    displayWork_->awaiting = false;
    QWidget::hideEvent(event);
}
void ResponseWindow::showEvent(QShowEvent *event) {
    displayWork_->suspended = false;
    if (displayWork_->dirty)
        draw();
    QWidget::showEvent(event);
}
void ResponseWindow::appendObservedSample(const ObservedSample &sample) {
    if (imported_ || !error_.isEmpty())
        return;
    const int axis = sample.role == "input" ? 0 : sample.role == "output" ? 1 : -1;
    if (axis < 0)
        return;
    (axis == 0 ? inputUnit_ : outputUnit_) = sample.unit;
    (void)aligner_.add(sample.step, 2, sample.timeSeconds);
    if (auto pair = aligner_.add(sample.step, std::size_t(axis), sample.value)) {
        if (previousStep_ && sample.step != previousStep_ + 1) {
            error_ = "Gap in paired input/output capture";
            return;
        }
        previousStep_ = sample.step;
        if (samples_.size() < 65536)
            samples_.push_back(*pair);
        else
            error_ = "Capture exceeds 65536 paired samples; use a shorter experiment";
    }
}
bool ResponseWindow::refreshSource() {
    if (imported_)
        return false;
    if (automaticSource_.lock() != source()) {
        automaticSource_ = source();
        automaticAttempted_ = false;
    }
    const bool more = ObservationViewer::refreshSource();
    loaded_ = !more && !sourceStatusText().startsWith("INCOMPLETE");
    updateStatus();
    return more;
}
void ResponseWindow::setDroppedSampleCount(std::uint64_t count) {
    if (count)
        error_ = "Incomplete capture cannot produce a frequency response";
}
void ResponseWindow::clearSamples() {
    displayWork_->worker.cancel();
    automaticAttempted_ = true;
    ++work_->epoch;
    {
        std::lock_guard lock(work_->mutex);
        work_->ready.reset();
        work_->error.clear();
    }
    imported_ = false;
    importedMetadata_ = {};
    discardPresentationHistory();
    aligner_.clear();
    samples_.clear();
    previousStep_ = 0;
    loaded_ = false;
    error_.clear();
    result_.reset();
    displayWork_->displayedSource.reset();
    phases_.clear();
    for (std::size_t p = 0; p < plots_.size(); ++p) {
        plots_[p]->clearSeries();
        markers_[p] = plots_[p]->addScatterSeries("Selected frequency", QColor("#d62728"));
    }
    trajectory_->bridge()->trajectoryGeometry().apply({});
    trajectory_->bridge()->setHead(false, {});
    landscape_->setGrid({});
    inputUnit_.clear();
    outputUnit_.clear();
    draw();
    updateStatus();
}
void ResponseWindow::updateStatus() {
    const bool complete =
        source() && source()->status().finished && !source()->status().incomplete() && loaded_;
    if (phaseTruncated_ && error_.isEmpty())
        inspection_->setToolTip(
            "Display capped at 64 continuous segments; inspection/export retains all bins");
    analyze_->setEnabled(complete && error_.isEmpty() && samples_.size() > 1 && !work_->running);
    analyze_->setText(work_->running ? "Analyzing…" : "Analyze captured impulse");
    status_->setText(QString("%1 paired samples | %2 | Assumes LTI, zero initial state and a sufficiently "
                             "decayed impulse tail. %3%4")
                         .arg(samples_.size())
                         .arg(work_->running ? "computing"
                              : result_      ? "finite-record response estimate"
                                             : "waiting for analysis")
                         .arg(imported_ ? "Imported response; file provenance retained" : sourceStatusText())
                         .arg(error_.isEmpty() ? "" : " | " + error_));
    if (isPreparingDisplay())
        status_->setText(status_->text() + " | preparing display");
    if (!displayWork_->error.isEmpty())
        status_->setText(status_->text() + " | Display unavailable: " + displayWork_->error);
}
void ResponseWindow::analyze() {
    if (!analyze_->isEnabled())
        return;
    automaticAttempted_ = true;
    const double dt = (samples_.back()[2] - samples_.front()[2]) / double(samples_.size() - 1);
    if (!std::isfinite(dt) || dt <= 0) {
        error_ = "Invalid model-time sampling interval";
        updateStatus();
        return;
    }
    for (std::size_t i = 1; i < samples_.size(); ++i)
        if (std::abs((samples_[i][2] - samples_[i - 1][2]) - dt) > dt * 1e-7) {
            error_ = "Input interval is not uniformly sampled";
            updateStatus();
            return;
        }
    std::vector<double> input, output;
    for (const auto &s : samples_) {
        input.push_back(s[0]);
        output.push_back(s[1]);
    }
    const auto epoch = ++work_->epoch;
    work_->running = true;
    updateStatus();
    analysis::analysisPool().start(
        [state = work_, epoch, rate = 1 / dt, input = std::move(input), output = std::move(output)] {
            std::shared_ptr<const s7spectral::Response> result;
            QString error;
            try {
                result = std::make_shared<const s7spectral::Response>(
                    s7spectral::impulseResponse(input, output, rate, [&] { return state->epoch != epoch; }));
            } catch (const std::exception &e) {
                error = QString::fromUtf8(e.what());
            }
            std::lock_guard lock(state->mutex);
            if (state->epoch == epoch) {
                state->ready = std::move(result);
                state->error = error;
            }
            state->running = false;
        });
}
void ResponseWindow::poll() {
    std::shared_ptr<const s7spectral::Response> result;
    QString error;
    {
        std::lock_guard lock(work_->mutex);
        result = std::move(work_->ready);
        error = std::move(work_->error);
        work_->error.clear();
    }
    if (result) {
        result_ = std::move(result);
        draw();
    }
    if (!error.isEmpty())
        error_ = error;
    updateStatus();
    if (!automaticAttempted_ && !result_ && isVisible() && !isMinimized() &&
        samples_.size() <= 8192 && analyze_->isEnabled())
        analyze();
}
void ResponseWindow::draw() {
    displayWork_->worker.cancel();
    displayWork_->awaiting = false;
    displayWork_->dirty = true;
    if (displayWork_->suspended)
        return;
    if (!displayWork_->scheduled) {
        displayWork_->scheduled = true;
        QTimer::singleShot(0, this, [this] {
            displayWork_->scheduled = false;
            submitDisplay();
        });
    }
}
void ResponseWindow::submitDisplay() {
    if (displayWork_->suspended)
        return;
    displayWork_->dirty = false;
    displayWork_->awaiting = true;
    const auto source = result_;
    const bool referenceEnabled = referenceEnabled_->isChecked(), unwrapped = unwrap_->isChecked();
    const double omega = omega_->value(), damping = damping_->value(), modelGain = modelGain_->value(),
                 reference = reference_->value();
    displayWork_->worker.submit([source, referenceEnabled, unwrapped, omega, damping, modelGain,
                                 reference](const auto &canceled) -> ResponseDisplay {
        ResponseDisplay frame;
        frame.source = source;
        frame.unwrapped = unwrapped;
        QList<TrajectoryPoint> trajectory;
        if (referenceEnabled) {
            auto grid = std::make_shared<analysis::SurfaceGrid>();
            const auto zeta = damping;
            for (std::size_t row = 0; row < 65; ++row)
                for (std::size_t col = 0; col < 65; ++col) {
                    const double x = -2 * omega + 4 * omega * double(col) / 64,
                                 y = -2 * omega + 4 * omega * double(row) / 64;
                    const std::complex<double> s{x, y}, den = s * s + 2 * zeta * omega * s + omega * omega;
                    const auto value = modelGain * omega * omega / den;
                    grid->append({x, y, double(row * 65 + col), 65, 65, 1, value.real(), value.imag()});
                }
            frame.landscape = std::move(grid);
        }
        if (!source)
            return frame;
        const auto &r = *source;
        frame.phases = phaseDegrees(r.values, unwrapped);
        std::array<QList<QPointF>, 4> segments;
        std::array<int, 4> segmentCounts{};
        frame.truncated = false;
        auto flush = [&](std::size_t p) {
            if (!segments[p].isEmpty()) {
                if (segmentCounts[p]++ >= 64) {
                    frame.truncated = true;
                    segments[p].clear();
                    return;
                }
                frame.curves[p].append(analysis::displayCurve(segments[p]));
                segments[p].clear();
            }
        };
        double previousPhase = 0;
        bool hadPhase = false;
        for (std::size_t k = 0; k < r.values.size(); ++k) {
            if ((k % 256) == 0 && canceled())
                return {};
            const auto h = r.values[k];
            const double magnitude = std::abs(h), phase = frame.phases[k];
            if (magnitude == 0) {
                for (std::size_t p = 0; p < 4; ++p)
                    flush(p);
                hadPhase = false;
                continue;
            }
            if (!unwrapped && hadPhase && std::abs(phase - previousPhase) > 180) {
                flush(1);
                flush(3);
            }
            previousPhase = phase;
            hadPhase = true;
            const auto db = 20 * (std::log10(magnitude) - std::log10(reference));
            if (r.frequency[k] > 0) {
                segments[0].append({std::log10(r.frequency[k]), db});
                segments[1].append({std::log10(r.frequency[k]), phase});
            }
            segments[2].append({h.real(), h.imag()});
            segments[3].append({phase, db});
            trajectory.append({r.frequency[k], h.real(), h.imag()});
        }
        for (std::size_t p = 0; p < 4; ++p)
            flush(p);
        if (referenceEnabled) {
            std::array<QList<QPointF>, 4> referenceSegments;
            double previous = 0;
            bool have = false;
            auto flushReference = [&](std::size_t p) {
                if (referenceSegments[p].isEmpty())
                    return;
                frame.references[p].append(analysis::displayCurve(referenceSegments[p]));
                referenceSegments[p].clear();
            };
            for (auto frequency : r.frequency) {
                if (canceled())
                    return {};
                const std::complex<double> s{0, 2 * std::numbers::pi * frequency};
                const auto h = modelGain * omega * omega / (s * s + 2 * damping * omega * s + omega * omega);
                auto phase = std::arg(h) * 180 / std::numbers::pi;
                const auto db = 20 * (std::log10(std::abs(h)) - std::log10(reference));
                if (unwrapped && have)
                    phase += 360 * std::round((previous - phase) / 360);
                if (!unwrapped && have && std::abs(phase - previous) > 180) {
                    flushReference(1);
                    flushReference(3);
                }
                previous = phase;
                have = true;
                if (frequency > 0) {
                    referenceSegments[0].append({std::log10(frequency), db});
                    referenceSegments[1].append({std::log10(frequency), phase});
                }
                referenceSegments[2].append({h.real(), h.imag()});
                referenceSegments[3].append({phase, db});
            }
            for (std::size_t p = 0; p < 4; ++p)
                flushReference(p);
        }
        frame.bounds = TrajectoryBounds::of(trajectory);
        frame.trajectory = prepareTrajectory(trajectory, frame.bounds, 65536, true, canceled);
        return frame;
    });
}
void ResponseWindow::applyDisplay() {
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
    landscape_->setGrid(frame.landscape);
    for (std::size_t p = 0; p < plots_.size(); ++p) {
        plots_[p]->clearSeries();
        markers_[p] = plots_[p]->addScatterSeries("Selected frequency", QColor("#d62728"));
        for (const auto &points : frame.curves[p]) {
            auto *line = plots_[p]->addSeries("Response", {QColor("#1565c0")});
            LinePlotView::setPoints(line, points);
        }
        for (const auto &points : frame.references[p]) {
            auto *line = plots_[p]->addSeries("Analytical reference", {QColor("#d97706"), 1.4, true});
            LinePlotView::setPoints(line, points);
        }
        (void)plots_[p]->fitAll();
    }
    displayWork_->displayedSource = frame.source;
    phases_ = frame.phases;
    phaseTruncated_ = frame.truncated;
    const auto phaseTitle = frame.unwrapped ? "Phase (degrees; unwrapped)" : "Phase (degrees; wrapped)";
    plots_[1]->setAxisTitles("log10(f / 1 Hz)", phaseTitle);
    plots_[3]->setAxisTitles(phaseTitle, "Gain (dB, explicit reference)");
    trajectoryBounds_ = frame.bounds;
    trajectory_->setAxisLabels(frame.bounds, {"Frequency (Hz)", "Re(H)", "Im(H)"});
    trajectory_->bridge()->trajectoryGeometry().apply(frame.trajectory);
    if (result_) {
        frequency_->setMaximum(result_->frequency.back());
        inspect(frequency_->value());
    } else
        trajectory_->bridge()->setHead(false, {});
    updateStatus();
}
void ResponseWindow::inspect(double frequency) {
    if (result_ != displayWork_->displayedSource)
        return;
    if (!result_)
        return;
    const auto &r = *result_;
    const auto it = std::lower_bound(r.frequency.begin(), r.frequency.end(), frequency);
    auto k = std::size_t(it - r.frequency.begin());
    if (k == r.frequency.size())
        --k;
    if (k && frequency - r.frequency[k - 1] < r.frequency[k] - frequency)
        --k;
    if (referenceEnabled_->isChecked()) {
        const auto row = int(std::lround((2 * std::numbers::pi * r.frequency[k] + 2 * omega_->value()) /
                                         (4 * omega_->value()) * 64));
        if (row >= 0 && row <= 64)
            landscape_->selectCell(row, 32);
        else
            landscape_->bridge()->setHead(false, {});
    }
    const auto h = r.values[k];
    const double m = std::abs(h), phase = phases_[k];
    const double db = m > 0 ? 20 * (std::log10(m) - std::log10(reference_->value())) : 0;
    inspection_->setText(QString("%1 Hz | H=(%2, %3) | |H|=%4 | %5 / %6%7")
                             .arg(r.frequency[k], 0, 'g', 9)
                             .arg(h.real(), 0, 'g', 9)
                             .arg(h.imag(), 0, 'g', 9)
                             .arg(m, 0, 'g', 9)
                             .arg(outputUnit_.isEmpty() ? "unspecified output unit" : outputUnit_,
                                  inputUnit_.isEmpty() ? "unspecified input unit" : inputUnit_)
                             .arg(m == 0 ? " | phase undefined" : ""));
    if (m > 0)
        inspection_->setText(inspection_->text() + QString(" | phase %1°").arg(phase, 0, 'g', 6));
    const std::array<QPointF, 4> points{{{r.frequency[k] > 0 ? std::log10(r.frequency[k]) : 0, db},
                                         {r.frequency[k] > 0 ? std::log10(r.frequency[k]) : 0, phase},
                                         {h.real(), h.imag()},
                                         {phase, db}}};
    for (std::size_t p = 0; p < 4; ++p)
        LinePlotView::setScatterPoints(markers_[p], m > 0 && (p > 1 || r.frequency[k] > 0)
                                                        ? QList<QPointF>{points[p]}
                                                        : QList<QPointF>{});
    trajectory_->bridge()->setHead(true, toScene({r.frequency[k], h.real(), h.imag()}, trajectoryBounds_));
}
void ResponseWindow::saveResult(const QString &path) const {
    if (!result_)
        throw std::invalid_argument("No completed response");
    analysis::ResultFile file;
    file.kind = "frequency_response";
    file.x = result_->frequency;
    file.y = {0};
    file.values = result_->values;
    file.valid.assign(file.values.size(), true);
    file.metadata = imported_
                        ? importedMetadata_
                        : QJsonObject{{"provenance", source() ? source()->provenance() : QJsonObject{}}};
    if (!imported_ && source()) {
        file.metadata["run_id"] = QString::number(source()->runId());
        QJsonArray ids, names;
        for (auto id : selectedChannels({"input", "output"})) {
            ids.append(qint64(id));
            names.append(source()->channels()[id].name);
        }
        file.metadata["channel_ids"] = ids;
        file.metadata["channel_names"] = names;
        file.metadata["first_step"] = QString::number(previousStep_ - std::int64_t(samples_.size()) + 1);
        file.metadata["last_step"] = QString::number(previousStep_);
    }
    file.metadata["pulse_height"] = result_->pulseHeight;
    file.metadata["pulse_index"] = qint64(result_->pulseIndex);
    file.metadata["response_samples"] = qint64(result_->responseSamples);
    file.metadata["input_unit"] = inputUnit_;
    file.metadata["output_unit"] = outputUnit_;
    file.save(path);
}
void ResponseWindow::openResult(const QString &path) {
    auto file = analysis::ResultFile::open(path);
    if (file.kind != "frequency_response" || file.y.size() != 1)
        throw std::invalid_argument("Expected a frequency-response result");
    auto result = std::make_shared<s7spectral::Response>();
    result->frequency = std::move(file.x);
    result->values = std::move(file.values);
    result->pulseHeight = file.metadata["pulse_height"].toDouble();
    const auto index = file.metadata["pulse_index"].toDouble(-1),
               count = file.metadata["response_samples"].toDouble(-1);
    if (!std::isfinite(index) || index < 0 || index > 65535 || std::floor(index) != index ||
        !std::isfinite(count) || count < 2 || count > 65536 || std::floor(count) != count ||
        result->frequency.front() < 0 ||
        std::find(file.valid.begin(), file.valid.end(), false) != file.valid.end())
        throw std::invalid_argument("Invalid frequency-response metadata/mask");
    result->pulseIndex = std::size_t(index);
    result->responseSamples = std::size_t(count);
    if (!std::isfinite(result->pulseHeight) || result->pulseHeight == 0)
        throw std::invalid_argument("Invalid response normalization metadata");
    ++work_->epoch;
    {
        std::lock_guard lock(work_->mutex);
        work_->ready.reset();
        work_->error.clear();
    }
    result_ = std::move(result);
    importedMetadata_ = file.metadata;
    inputUnit_ = file.metadata["input_unit"].toString();
    outputUnit_ = file.metadata["output_unit"].toString();
    imported_ = true;
    error_.clear();
    draw();
    updateStatus();
}
void ResponseWindow::exportResult() {
    if (!result_)
        return;
    const auto path =
        QFileDialog::getSaveFileName(this, "Export frequency response", "response.csv", "CSV (*.csv)");
    if (path.isEmpty())
        return;
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        QMessageBox::critical(this, "Export failed", file.errorString());
        return;
    }
    QJsonObject meta{{"format", "s7.frequency_response.csv"},
                     {"version", 1},
                     {"method", "single-pulse unnormalized DFT"},
                     {"pulse_height", result_->pulseHeight},
                     {"pulse_index", qint64(result_->pulseIndex)},
                     {"response_samples", qint64(result_->responseSamples)},
                     {"input_unit", inputUnit_},
                     {"output_unit", outputUnit_},
                     {"db_reference", reference_->value()},
                     {"provenance", source() ? source()->provenance() : QJsonObject{}}};
    meta["run_id"] = imported_
                         ? importedMetadata_["run_id"]
                         : QJsonValue(source() ? QString::number(source()->runId()) : QString("unavailable"));
    QJsonArray channels;
    if (imported_)
        channels = importedMetadata_["channel_ids"].toArray();
    else
        for (auto id : selectedChannels({"input", "output"}))
            channels.append(qint64(id));
    meta["channel_ids"] = channels;
    QTextStream out(&file);
    out.setRealNumberPrecision(17);
    out << "# " << QJsonDocument(meta).toJson(QJsonDocument::Compact) << "\nfrequency_hz,re,im\n";
    for (std::size_t i = 0; i < result_->values.size(); ++i)
        out << result_->frequency[i] << ',' << result_->values[i].real() << ',' << result_->values[i].imag()
            << '\n';
    out.flush();
    if (out.status() != QTextStream::Ok || !file.commit())
        QMessageBox::critical(this, "Export failed", file.errorString());
}
} // namespace s7studio::viewers
