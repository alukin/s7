// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-S7-plugin-permission
#include "viewers/pair_analysis_view.hpp"
#include "viewers/line_plot_view.hpp"
#include "instrument_controls.hpp"
#include <QtGraphs/QLineSeries>
#include <QtGraphs/QScatterSeries>
#include <QCheckBox>
#include <QComboBox>
#include <QScrollArea>
#include <QLabel>
#include <QPushButton>
#include <QProgressBar>
#include <QSpinBox>
#include <QSignalBlocker>
#include <QTimer>
#include <QVBoxLayout>
#include <QQuickItem>
#include <QQuickWidget>
#include <algorithm>
#include <cmath>
#include <numeric>

namespace s7studio::viewers {
using namespace analysis;
namespace {
QString unit(const QString& text) { return text.isEmpty() ? QStringLiteral("[unit unspecified]") : text; }
QRectF plotArea(LinePlotView* plot)
{
    if (plot->quickWidget() && plot->quickWidget()->rootObject())
        if (auto* graphs = plot->quickWidget()->rootObject()->findChild<QObject*>("viewer.graphs")) return graphs->property("plotArea").toRectF();
    return QRectF(0, 0, std::max(1, plot->width()-90), std::max(1, plot->height()-60));
}
QList<QPointF> envelope(const std::vector<double>& x, const std::vector<double>& y, std::size_t buckets)
{
    QList<QPointF> points;
    const auto count = std::max<std::size_t>(1, (y.size()+buckets-1)/buckets);
    for (std::size_t begin = 0; begin < y.size(); begin += count) {
        const auto end = std::min(y.size(), begin+count);
        auto lo = begin, hi = begin;
        for (auto i = begin+1; i < end; ++i) { if (y[i] < y[lo]) lo = i; if (y[i] > y[hi]) hi = i; }
        std::size_t indices[]{begin, lo, hi, end-1}; std::sort(std::begin(indices), std::end(indices));
        auto previous = y.size();
        for (auto i : indices) if (i != previous) { points.push_back({x[i], y[i]}); previous = i; }
    }
    return points;
}
}
PairAnalysisView::PairAnalysisView(PairKind kind, QWidget* parent, QWidget* controlsParent) : QWidget(parent), kind_(kind), worker_(kind)
{
    const bool xy = kind == PairKind::Constellation;
    setObjectName(xy ? "live.constellation" : "live.correlation");
    auto* layout = new QHBoxLayout(this); layout->setContentsMargins(0, 0, 0, 0);
    controls_ = new ui::InstrumentSidebar("pair", controlsParent ? controlsParent : this);
    auto* box = controls_;
    auto* fields = new ui::WrappingSettings(box);
    controls_->addSection(new ui::CollapsibleSection("Channels", "pair.channels", fields, true, box));
    auto combo = [&](const char* name, const QStringList& items) {
        auto* c = new QComboBox(box); c->setObjectName(name); c->addItems(items);
        c->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon); c->setMinimumContentsLength(8); return c;
    };
    auto field = [&](const QString& label, QWidget* widget) { fields->addField(label, widget); };
    first_ = combo("pair.first", {}); second_ = combo("pair.second", {});
    field(xy ? "X / I" : "A", first_); field(xy ? "Y / Q" : "B", second_);
    mode_ = combo("pair.mode", xy ? QStringList{"Dots", "Lines + dots", "Density"} : QStringList{"Cross-correlation", "Auto-correlation"});
    field("Mode", mode_);
    size_ = combo("pair.size", {}); for (int n = 64; n <= 65536; n *= 2) size_->addItem(QString::number(n), n); size_->setCurrentIndex(size_->findData(4096));
    normalization_ = combo("pair.normalization", {"Raw sum", "Biased (/N)", "Unbiased (/overlap)", "Coefficient"}); normalization_->setCurrentIndex(3);
    density_ = combo("pair.densitySize", {"64", "128", "256"}); density_->setCurrentIndex(1);
    lagUnits_ = combo("pair.lagUnits", {"Model seconds", "Samples"});
    persistence_ = new QSpinBox(box); persistence_->setObjectName("pair.persistence"); persistence_->setRange(0, 1000000000); persistence_->setSpecialValueText("Current window"); persistence_->setFixedWidth(140);
    persistence_->setToolTip("Age in model steps. Zero uses the current raw window. Storage is capped at 65,536 finite pairs; hidden panels restart persistence.");
    maxLag_ = new QSpinBox(box); maxLag_->setObjectName("pair.maxLag"); maxLag_->setRange(0, 4095); maxLag_->setValue(1024); maxLag_->setFixedWidth(110);
    equalAspect_ = new QCheckBox("Equal numeric aspect", box); equalAspect_->setObjectName("pair.equalAspect"); equalAspect_->setChecked(true);
    equalAspect_->setToolTip("Equal numeric units per pixel; no physical-unit conversion is implied.");
    removeMean_ = new QCheckBox("Remove means", box); removeMean_->setObjectName("pair.removeMean");
    absolutePeak_ = new QCheckBox("Absolute peak", box); absolutePeak_->setObjectName("pair.absolutePeak");
    auto* fit = new QPushButton("Fit", box); fit->setObjectName("pair.fit");
    fields = new ui::WrappingSettings(box);
    controls_->addSection(new ui::CollapsibleSection(xy ? "Display" : "Correlation", "pair.settings", fields, true, box));
    if (xy) {
        fields->addControl(equalAspect_); field("Persistence (steps)", persistence_); field("Density grid", density_);
        fields->addControl(new QLabel("Cap: 65,536 raw pairs", box)); fields->addControl(fit);
    } else {
        field("Samples N", size_); field("Normalization", normalization_); field("Maximum |lag|", maxLag_);
        fields->addControl(removeMean_); fields->addControl(absolutePeak_); fields->addControl(fit);
        field("Lag axis", lagUnits_);
    }
    for (auto* widget : QList<QWidget*>{size_, normalization_, lagUnits_, maxLag_, removeMean_, absolutePeak_}) if (xy) widget->hide();
    for (auto* widget : QList<QWidget*>{density_, persistence_, equalAspect_}) if (!xy) widget->hide();
    auto* convention = new QLabel(xy ? "Same-step raw X/Y or I/Q. No symbol timing, carrier recovery or equalization." :
        "rAB[l] = Σ A[n] B[n+l]; positive lag means B occurs later than A. Edge lags have less overlap.", box);
    convention->setWordWrap(true); controls_->addSection(convention);
    busy_ = controls_->busy(); readout_ = controls_->readout(); status_ = controls_->status();
    plot_ = new LinePlotView(this); plot_->setObjectName("pair.plot"); plot_->setLegendVisible(true); plot_->bridge()->inspectionEnabled = true; plot_->bridge()->instrumentTheme = true;
    plot_->addSeries(xy ? "Trajectory" : "Correlation", {QColor("#54b8ff")});
    dots_ = plot_->addScatterSeries("Raw pairs", QColor("#54b8ff"));
    newest_ = plot_->addScatterSeries(xy ? "Newest pair" : "Peak lag", QColor("#ff7080"));
    selected_ = plot_->addScatterSeries("Inspected pair", QColor("#ffc36b"));
    plot_->setLegendEntryVisible(selected_, false);
    if (!xy) plot_->setLegendEntryVisible(dots_, false);
    layout->addWidget(plot_, 1);
    marker_ = new QLabel(box); marker_->setObjectName("pair.marker"); marker_->setTextFormat(Qt::PlainText);
    marker_->setWordWrap(true); controls_->addSection(marker_);
    if (!controlsParent) { controls_->setMinimumWidth(300); layout->addWidget(controls_); }
    for (auto* c : {first_, second_, size_, normalization_, density_}) connect(c, &QComboBox::currentIndexChanged, this, [this] { reconfigure(); });
    connect(mode_, &QComboBox::currentIndexChanged, this, [this, xy] { if (xy) render(true); else reconfigure(); });
    connect(persistence_, &QSpinBox::valueChanged, this, [this] { reconfigure(); });
    connect(maxLag_, &QSpinBox::valueChanged, this, [this] { reconfigure(); });
    connect(removeMean_, &QCheckBox::toggled, this, [this] { reconfigure(); });
    connect(absolutePeak_, &QCheckBox::toggled, this, [this] { reconfigure(); });
    connect(equalAspect_, &QCheckBox::toggled, this, [this] { render(true); });
    connect(lagUnits_, &QComboBox::currentIndexChanged, this, [this] { render(true); });
    connect(fit, &QPushButton::clicked, this, [this] { render(true); });
    connect(plot_, &LinePlotView::hoveredPoint, this, [this](double x, double y, bool active) { inspect(x, y, active); });
    connect(mode_, &QComboBox::currentIndexChanged, this, &PairAnalysisView::inspectionInvalidated);
    connect(lagUnits_, &QComboBox::currentIndexChanged, this, &PairAnalysisView::inspectionInvalidated);
    connect(equalAspect_, &QCheckBox::toggled, this, &PairAnalysisView::inspectionInvalidated);
    pollTimer_ = new QTimer(this); pollTimer_->setInterval(16); connect(pollTimer_, &QTimer::timeout, this, &PairAnalysisView::poll);
    render(true);
}
PairAnalysisView::~PairAnalysisView() { worker_.cancel(); disconnect(plot_, nullptr, this, nullptr); delete plot_; }
PairSettings PairAnalysisView::settings() const
{
    PairSettings s;
    s.first = static_cast<std::size_t>(std::max(0, first_->currentIndex())); s.second = static_cast<std::size_t>(std::max(0, second_->currentIndex()));
    s.size = static_cast<std::size_t>(size_->currentData().toInt()); s.maxLag = static_cast<std::size_t>(maxLag_->value());
    s.densitySize = static_cast<std::size_t>(density_->currentText().toInt()); s.persistenceSteps = static_cast<std::uint64_t>(persistence_->value());
    s.autoCorrelation = kind_ == PairKind::Correlation && mode_->currentIndex() == 1; s.removeMean = removeMean_->isChecked(); s.absolutePeak = absolutePeak_->isChecked();
    s.normalization = static_cast<CorrelationNormalization>(normalization_->currentIndex()); return s;
}
void PairAnalysisView::reconfigure()
{
    const QSignalBlocker blocker(maxLag_); maxLag_->setMaximum(size_->currentData().toInt()-1);
    second_->setEnabled(kind_ == PairKind::Constellation || mode_->currentIndex() == 0);
    worker_.cancel(); ++revision_; result_.reset(); render(true); submit(); emit inspectionInvalidated();
}
void PairAnalysisView::setFrame(std::shared_ptr<const observation::LiveFrame> frame)
{
    if (!frame) return;
    const bool same = frame_ && frame_->runId == frame->runId && frame_->generation == frame->generation && frame_->received == frame->received;
    const bool newRun = frame_ && (frame_->runId != frame->runId || frame_->generation != frame->generation);
    QStringList names; for (const auto& channel : frame->channels) names.push_back(channel.name + " [" + unit(channel.unit) + "]");
    QStringList old; for (int i = 0; i < first_->count(); ++i) old.push_back(first_->itemText(i));
    if (names != old) {
        const QSignalBlocker a(first_), b(second_); const int i = first_->currentIndex(), j = second_->currentIndex();
        first_->clear(); first_->addItems(names); second_->clear(); second_->addItems(names);
        if (!names.empty()) { first_->setCurrentIndex(std::clamp(i, 0, static_cast<int>(names.size())-1)); second_->setCurrentIndex(std::clamp(j < 0 ? 1 : j, 0, static_cast<int>(names.size())-1)); }
    }
    frame_ = std::move(frame); if (newRun) reconfigure(); else if (!same) submit();
}
void PairAnalysisView::submit() { if (!suspended_ && isVisible() && frame_) { ++submitted_; worker_.submit(frame_, settings(), revision_); } updateStatus(); }
void PairAnalysisView::poll()
{
    if (suspended_ || !isVisible() || inspecting_) return;
    if (auto result = worker_.takeResult()) {
        if (!frame_ || result->revision != revision_ || result->runId != frame_->runId || result->generation != frame_->generation) return;
        result_ = std::move(*result); render(true);
    }
    updateStatus();
}
void PairAnalysisView::clear() { worker_.cancel(); ++revision_; frame_.reset(); result_.reset(); inspecting_ = false; frozen_ = false; emit inspectingChanged(false); render(true); }
std::shared_ptr<const observation::LiveFrame> PairAnalysisView::setFrozen(bool frozen)
{
    frozen_ = frozen; if (frozen) { worker_.cancel(); if (result_ && result_->frame) frame_ = result_->frame; else submit(); }
    updateStatus(); return frame_;
}
void PairAnalysisView::showEvent(QShowEvent* event) { QWidget::showEvent(event); if (!suspended_) pollTimer_->start(); submit(); }
void PairAnalysisView::hideEvent(QHideEvent* event) { pollTimer_->stop(); worker_.cancel(); inspecting_ = false; emit inspectingChanged(false); QWidget::hideEvent(event); }
void PairAnalysisView::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    if (!resizePending_) { resizePending_ = true; QTimer::singleShot(0, this, [this] { resizePending_ = false; render(true); }); }
}
void PairAnalysisView::setSuspended(bool suspended)
{
    if (suspended_ == suspended) return;
    suspended_ = suspended;
    if (suspended) { pollTimer_->stop(); worker_.cancel(); }
    else if (isVisible()) { pollTimer_->start(); submit(); }
}
QString PairAnalysisView::lagText(std::size_t index) const
{
    if (!result_ || index >= result_->lags.size()) return {};
    const auto& r = *result_; const auto lag = r.lags[index];
    const auto valueUnit = r.settings.normalization == CorrelationNormalization::Coefficient ? QStringLiteral("1") : unit(r.firstUnit)+"·"+unit(r.secondUnit);
    return QStringLiteral("Lag %1 samples • %2 s • r=%3 %4 • overlap %5/%6")
        .arg(lag).arg(static_cast<double>(lag)/r.sampleRate, 0, 'g', 5).arg(r.correlations[index], 0, 'g', 5).arg(valueUnit)
        .arg(r.settings.size-static_cast<std::size_t>(std::abs(lag))).arg(r.settings.size);
}
void PairAnalysisView::render(bool fit)
{
    if (rendering_) return;
    rendering_ = true; displayWarning_.clear();
    for (auto* series : plot_->series()) LinePlotView::setPoints(series, {});
    for (auto* series : {dots_, newest_, selected_}) LinePlotView::setScatterPoints(series, {});
    plot_->setDensityImage({}); plot_->bridge()->cursorVisible = false; emit plot_->bridge()->inspectionChanged();
    marker_->clear();
    plot_->setLegendEntryVisible(dots_, kind_ == PairKind::Constellation && mode_->currentIndex() != 2);
    plot_->setLegendEntryVisible(plot_->series()[0], kind_ == PairKind::Correlation || mode_->currentIndex() == 1);
    if (result_ && result_->state == PairResult::State::Ready) {
        const auto& r = *result_;
        if (kind_ == PairKind::Constellation) {
            plot_->setAxisTitles(r.firstName + " (" + unit(r.firstUnit) + ")", r.secondName + " (" + unit(r.secondUnit) + ")");
            if (fit) {
                const auto padX = (r.xMax-r.xMin)*.04, padY = (r.yMax-r.yMin)*.04;
                plot_->setXRange(std::isfinite(r.xMin-padX) ? r.xMin-padX : r.xMin, std::isfinite(r.xMax+padX) ? r.xMax+padX : r.xMax);
                plot_->setYRange(std::isfinite(r.yMin-padY) ? r.yMin-padY : r.yMin, std::isfinite(r.yMax+padY) ? r.yMax+padY : r.yMax);
                if (equalAspect_->isChecked()) {
                    const auto area = plotArea(plot_); const double w = std::max(1., area.width()), h = std::max(1., area.height());
                    const double scale = std::max((plot_->xMax()-plot_->xMin())/w, (plot_->yMax()-plot_->yMin())/h);
                    const double x = std::midpoint(plot_->xMin(), plot_->xMax()), y = std::midpoint(plot_->yMin(), plot_->yMax());
                    plot_->setXRange(x-scale*w/2, x+scale*w/2); plot_->setYRange(y-scale*h/2, y+scale*h/2);
                }
            }
            if (mode_->currentIndex() == 2) {
                const auto n = r.settings.densitySize;
                QImage image(static_cast<int>(n), static_cast<int>(n), QImage::Format_ARGB32); image.fill(Qt::transparent);
                const auto maximum = *std::max_element(r.density.begin(), r.density.end());
                for (std::size_t y = 0; y < n; ++y) for (std::size_t x = 0; x < n; ++x) if (const auto count = r.density[y*n+x]) {
                    const double fraction = std::log1p(static_cast<double>(count))/std::log1p(static_cast<double>(maximum));
                    image.setPixelColor(static_cast<int>(x), static_cast<int>(n-1-y), QColor::fromHsvF((1-fraction)*.65, .9, .95, .85));
                }
                plot_->setDensityImage(std::move(image), {r.xMin, r.yMin, r.xMax-r.xMin, r.yMax-r.yMin});
            } else {
                QList<QPointF> all;
                QList<QList<QPointF>> segments;
                for (const auto& p : r.points) { all.push_back({p.x, p.y}); if (p.segmentStart || segments.empty()) segments.push_back({}); segments.back().push_back({p.x, p.y}); }
                LinePlotView::setScatterPoints(dots_, all);
                if (mode_->currentIndex() == 1) {
                    const auto count = std::min<qsizetype>(64, segments.size());
                    while (plot_->series().size() < count) { auto* series = plot_->addSeries("Segment", {QColor("#54b8ff")}); plot_->setLegendEntryVisible(series, false); }
                    for (qsizetype i = 0; i < count; ++i) LinePlotView::setPoints(plot_->series()[i], segments[i]);
                    if (segments.size() > count) displayWarning_ = "Line segment cap reached; remaining raw pairs are shown as dots.";
                }
            }
            const auto& newest = r.points.back(); LinePlotView::setScatterPoints(newest_, {{newest.x, newest.y}});
            marker_->setText(QStringLiteral("Newest pair • step %1 • time %2 s • X=%3 %4 • Y=%5 %6")
                .arg(newest.step).arg(newest.time, 0, 'g', 5).arg(newest.x, 0, 'g', 5).arg(unit(r.firstUnit)).arg(newest.y, 0, 'g', 5).arg(unit(r.secondUnit)));
        } else {
            const bool samples = lagUnits_->currentIndex() == 1;
            plot_->setAxisTitles(samples ? "Lag (samples; + means B later)" : "Lag (model s; + means B later)",
                normalization_->currentText() + (r.settings.normalization == CorrelationNormalization::Coefficient ? " (1)" : " ("+unit(r.firstUnit)+"·"+unit(r.secondUnit)+")"));
            std::vector<double> x; for (auto lag : r.lags) x.push_back(samples ? static_cast<double>(lag) : static_cast<double>(lag)/r.sampleRate);
            if (fit) plot_->setXRange(x.front(), x.back());
            LinePlotView::setPoints(plot_->series()[0], envelope(x, r.correlations, static_cast<std::size_t>(std::max(64, plot_->width()))));
            LinePlotView::setScatterPoints(newest_, {{x[r.peakIndex], r.correlations[r.peakIndex]}});
            if (fit) plot_->fitYToVisibleData();
            marker_->setText("Peak • "+lagText(r.peakIndex));
        }
    }
    if (!inspecting_) readout_->setText(kind_ == PairKind::Constellation ? "Hover over a raw pair or density bin." : "Hover over an integer correlation lag.");
    rendering_ = false; updateStatus();
}
QString PairAnalysisView::inspect(double x, double y, bool active)
{
    inspecting_ = active && result_ && result_->state == PairResult::State::Ready;
    LinePlotView::setScatterPoints(selected_, {});
    plot_->bridge()->cursorVisible = false;
    if (inspecting_) {
        const auto& r = *result_;
        if (kind_ == PairKind::Correlation) {
            const double lag = lagUnits_->currentIndex() == 1 ? x : x*r.sampleRate;
            if (lag >= r.lags.front() && lag <= r.lags.back()) {
                const auto rounded = static_cast<int>(std::ceil(lag-.5)); // Ties toward the earlier lag.
                const auto index = static_cast<std::size_t>(rounded-r.lags.front());
                plot_->bridge()->cursorVisible = true; plot_->bridge()->cursorX = lagUnits_->currentIndex() == 1 ? rounded : static_cast<double>(rounded)/r.sampleRate;
                readout_->setText(lagText(index));
            } else readout_->setText("No correlation lag at this position.");
        } else if (mode_->currentIndex() == 2) {
            if (x >= r.xMin && x <= r.xMax && y >= r.yMin && y <= r.yMax) {
                const auto n = r.settings.densitySize;
                const auto ix = std::min(n-1, static_cast<std::size_t>((x-r.xMin)/(r.xMax-r.xMin)*static_cast<double>(n)));
                const auto iy = std::min(n-1, static_cast<std::size_t>((y-r.yMin)/(r.yMax-r.yMin)*static_cast<double>(n)));
                readout_->setText(QStringLiteral("Density aggregate • X [%1, %2] %3 • Y [%4, %5] %6 • count %7")
                    .arg(r.xMin+static_cast<double>(ix)/static_cast<double>(n)*(r.xMax-r.xMin), 0, 'g', 5)
                    .arg(r.xMin+static_cast<double>(ix+1)/static_cast<double>(n)*(r.xMax-r.xMin), 0, 'g', 5).arg(unit(r.firstUnit))
                    .arg(r.yMin+static_cast<double>(iy)/static_cast<double>(n)*(r.yMax-r.yMin), 0, 'g', 5)
                    .arg(r.yMin+static_cast<double>(iy+1)/static_cast<double>(n)*(r.yMax-r.yMin), 0, 'g', 5).arg(unit(r.secondUnit)).arg(r.density[iy*n+ix]));
            } else readout_->setText("No density bin at this position.");
        } else {
            const auto area = plotArea(plot_); const double sx = area.width()/(plot_->xMax()-plot_->xMin()), sy = area.height()/(plot_->yMax()-plot_->yMin());
            const XYPoint* closest = nullptr; double distance = 144;
            for (const auto& p : r.points) {
                if (p.x < plot_->xMin() || p.x > plot_->xMax() || p.y < plot_->yMin() || p.y > plot_->yMax()) continue;
                const double dx = (p.x-x)*sx, dy = (p.y-y)*sy, next = dx*dx+dy*dy;
                if (next < distance || (next == distance && closest && p.step > closest->step)) { closest = &p; distance = next; }
            }
            if (closest) {
                LinePlotView::setScatterPoints(selected_, {{closest->x, closest->y}});
                readout_->setText(QStringLiteral("Raw pair • run %1 • step %2 • time %3 s • X=%4 %5 • Y=%6 %7 (stored f64)")
                    .arg(r.runId).arg(closest->step).arg(closest->time, 0, 'g', 5).arg(closest->x, 0, 'g', 5).arg(unit(r.firstUnit)).arg(closest->y, 0, 'g', 5).arg(unit(r.secondUnit)));
            } else readout_->setText("No raw pair within 12 screen pixels.");
        }
    } else readout_->setText(kind_ == PairKind::Constellation ? "Hover over a raw pair or density bin." : "Hover over an integer correlation lag.");
    emit plot_->bridge()->inspectionChanged(); emit inspectingChanged(inspecting_); updateStatus(); return readout_->text();
}
std::optional<analysis::WindowSnapshot> PairAnalysisView::inspectionSnapshot() const
{
    if (!result_ || result_->state != PairResult::State::Ready) return {};
    auto data = analysis::pairWindowSnapshot(*result_);
    if (kind_ == PairKind::Correlation && lagUnits_->currentIndex() == 0) {
        for (auto& x : data.displayX) x /= result_->sampleRate;
        data.displayAxisName = "lag_time"; data.displayAxisUnit = "s";
    }
    data.settings.insert("display", QJsonObject{{"mode", mode_->currentText()}, {"equal_numeric_aspect", equalAspect_->isChecked()}, {"lag_axis", lagUnits_->currentText()}});
    return data;
}
void PairAnalysisView::updateStatus()
{
    busy_->setVisible(worker_.busy());
    QString text;
    if (!frame_) text = "Waiting for live acquisition.";
    else if (!result_) text = worker_.busy() ? "Computing paired analysis…" : "Analysis inactive.";
    else if (result_->state != PairResult::State::Ready) text = result_->message;
    else {
        const auto& r = *result_;
        text = QStringLiteral("Run %1 • steps %2–%3 • Fs=%4 Hz").arg(r.runId).arg(r.firstStep).arg(r.lastStep).arg(r.sampleRate, 0, 'g', 12);
        if (kind_ == PairKind::Constellation) text += QStringLiteral(" • %1 pairs • persistence %2 steps (%3 s) • evicted %4 • invalid pairs %5 • seen gaps %6")
            .arg(r.points.size()).arg(r.settings.persistenceSteps).arg(static_cast<double>(r.settings.persistenceSteps)/r.sampleRate, 0, 'g', 10).arg(r.evicted).arg(r.invalid).arg(r.gaps);
        text += inspecting_ ? " • Inspecting displayed result" : frozen_ ? " • Frozen result" : "";
    }
    if (worker_.busy() && result_) text += " • Updating (showing preceding result)";
    if (worker_.replacedRequests()) text += QStringLiteral(" • replaced requests %1").arg(worker_.replacedRequests());
    if (!displayWarning_.isEmpty()) text += " • "+displayWarning_;
    status_->setToolTip(text);
    if (result_ && result_->state == PairResult::State::Ready) {
        const auto& r = *result_;
        const auto summary = kind_ == PairKind::Constellation ? QStringLiteral("%1 pairs • %2 Hz").arg(r.points.size()).arg(r.sampleRate, 0, 'g', 5) :
            QStringLiteral("N=%1 • peak lag %2 samples").arg(r.settings.size).arg(r.lags[r.peakIndex]);
        status_->setText(QStringLiteral("%1 • %2\n%3%4")
            .arg(frame_ && frame_->finished ? "Run finished" : "Acquiring",
                 inspecting_ ? "Inspecting" : frozen_ ? "Frozen" : "Following live", summary,
                 displayWarning_.isEmpty() ? QString() : "\n"+displayWarning_));
    } else status_->setText(text);
}
}
