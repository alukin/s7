// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-S7-plugin-permission
#include "analysis/window_inspection.hpp"
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QThreadPool>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <limits>
#include <mutex>
#include <stdexcept>

namespace s7studio::analysis {
QString exactNumber(double value)
{
    if (std::isnan(value)) return "NaN";
    if (std::isinf(value)) return value < 0 ? "-Infinity" : "Infinity";
    if (value == 0 && std::signbit(value)) return "-0";
    return QString::number(value, 'g', 17);
}
QString windowKind(WindowSnapshot::Kind kind)
{
    switch (kind) {
    case WindowSnapshot::Kind::Time: return "time";
    case WindowSnapshot::Kind::Spectrum: return "spectrum";
    case WindowSnapshot::Kind::XY: return "xy";
    case WindowSnapshot::Kind::Correlation: return "correlation";
    }
    return {};
}
namespace {
QString unit(const QString& u) { return u.isEmpty() ? QStringLiteral("[unit unspecified]") : u; }
QJsonArray columnInfo(const std::vector<WindowColumn>& columns)
{
    QJsonArray result;
    for (const auto& c : columns) result.append(QJsonObject{{"name", c.name}, {"unit", c.unit}, {"stored_type", "f64"}});
    return result;
}
void validate(const WindowSnapshot& d)
{
    if (!d.raw || d.axis.empty() || d.axis.size() > 131071 || d.columns.empty() || d.columns.size() > 8)
        throw std::invalid_argument("No bounded displayed window is available.");
    const auto& f = *d.raw;
    if (f.steps.empty() || f.steps.size() > observation::LiveAcquisition::maxPositions || f.channels.empty() || f.channels.size() > 8 ||
        f.times.size() != f.steps.size() || f.values.size() != f.steps.size()*static_cast<std::size_t>(f.channels.size()))
        throw std::invalid_argument("Inconsistent raw acquisition frame.");
    for (const auto& c : d.columns) if (c.values.size() != d.axis.size()) throw std::invalid_argument("Inconsistent displayed columns.");
    if ((!d.steps.empty() && d.steps.size() != d.axis.size()) || (!d.times.empty() && d.times.size() != d.axis.size()))
        throw std::invalid_argument("Inconsistent displayed positions.");
}
void interval(WindowSnapshot& d, std::int64_t first, std::int64_t last) { d.inputFirstStep = first; d.inputLastStep = last; }
struct Sum {
    double value = 0, correction = 0;
    void add(double x) {
        const auto t = value+x;
        correction += std::abs(value) >= std::abs(x) ? (value-t)+x : (x-t)+value;
        value = t;
    }
    double total() const { return value+correction; }
};
QString position(const WindowSnapshot& d, std::size_t i)
{
    QString s = QStringLiteral("index %1 • %2=%3 %4").arg(i).arg(d.axisName, exactNumber(d.axis[i]), unit(d.axisUnit));
    if (!d.displayAxisName.isEmpty()) s += QStringLiteral(" • %1=%2 %3").arg(d.displayAxisName, exactNumber(d.displayX[i]), unit(d.displayAxisUnit));
    if (!d.steps.empty()) s += QStringLiteral(" • step %1 • time %2 s").arg(d.steps[i]).arg(exactNumber(d.times[i]));
    return s;
}
QString optionalNumber(std::optional<double> x) { return x ? exactNumber(*x) : QStringLiteral("unavailable"); }
QString statsText(const WindowColumn& c, const RegionStatistics& s, const WindowSnapshot* d = nullptr)
{
    QString text = QStringLiteral("%1 [%2] • count %3 • nonfinite %4").arg(c.name, unit(c.unit)).arg(s.count).arg(s.invalid);
    if (s.invalid) return text+" • aggregates unavailable: nonfinite coverage";
    text += QStringLiteral(" • min %1 • max %2 • mean %3 • RMS %4 • peak-to-peak %5 • population σ %6")
        .arg(optionalNumber(s.minimum), optionalNumber(s.maximum), optionalNumber(s.mean), optionalNumber(s.rms), optionalNumber(s.peakToPeak), optionalNumber(s.standardDeviation));
    if (d && s.minimum && s.maximum) text += "\n  min at "+position(*d, s.minimumIndex)+"; max at "+position(*d, s.maximumIndex);
    return text;
}
QJsonObject rawMetadata(const observation::LiveFrame& f)
{
    QJsonArray channels;
    for (const auto& c : f.channels) channels.append(QJsonObject{{"name", c.name}, {"unit", c.unit}, {"source_type", c.sourceType}, {"stored_type", "f64"}});
    return {{"run_id", QString::number(f.runId)}, {"generation", QString::number(f.generation)}, {"received", QString::number(f.received)},
        {"evicted", QString::number(f.evicted)}, {"gaps_seen", QString::number(f.gaps)}, {"capacity", static_cast<int>(f.capacity)},
        {"rate_numerator", QString::number(f.rateNumerator)}, {"rate_denominator", QString::number(f.rateDenominator)},
        {"dt_s", exactNumber(f.dt)}, {"finished", f.finished}, {"error", f.error}, {"provenance", f.provenance}, {"trigger", observation::triggerMetadata(f)},
        {"first_step", QString::number(f.steps.front())}, {"last_step", QString::number(f.steps.back())}, {"channels", channels}};
}
QByteArray json(const QJsonObject& value) { return QJsonDocument(value).toJson(QJsonDocument::Compact); }
QByteArray csv(QString value) { value.replace('"', "\"\""); return ('"'+value+'"').toUtf8(); }
}
WindowSnapshot timeWindow(std::shared_ptr<const observation::LiveFrame> frame)
{
    WindowSnapshot d; d.raw = std::move(frame); d.axisName = "model_time"; d.axisUnit = "s";
    if (!d.raw || d.raw->steps.empty()) return d;
    const auto& f = *d.raw; d.axis = d.times = f.times; d.displayX = d.axis; d.steps = f.steps;
    const auto width = static_cast<std::size_t>(f.channels.size());
    if (!width || f.values.size() != f.steps.size()*width) throw std::invalid_argument("Inconsistent acquisition columns.");
    for (std::size_t c = 0; c < width; ++c) {
        WindowColumn column{f.channels[static_cast<qsizetype>(c)].name, f.channels[static_cast<qsizetype>(c)].unit, {}};
        for (std::size_t i = 0; i < f.steps.size(); ++i) column.values.push_back(f.values[i*width+c]);
        d.columns.push_back(std::move(column));
    }
    if (f.trigger && f.trigger->triggered) {
        for (std::size_t i = 0; i < d.displayX.size(); ++i) d.displayX[i] = f.plottedTime(i);
        d.displayAxisName = "trigger_time"; d.displayAxisUnit = "s";
        d.settings.insert("trigger", observation::triggerMetadata(f));
    }
    interval(d, f.steps.front(), f.steps.back()); return d;
}
WindowSnapshot spectrumWindowSnapshot(const SpectrumResult& r)
{
    WindowSnapshot d; d.kind = WindowSnapshot::Kind::Spectrum; d.raw = r.frame; d.axisName = "frequency"; d.axisUnit = "Hz";
    if (r.state != SpectrumResult::State::Ready) return d;
    d.axis = d.displayX = r.frequencies; d.displayY = r.values;
    const auto u = r.settings.quantity == SpectrumQuantity::Amplitude ? r.unit : r.unit+(r.settings.quantity == SpectrumQuantity::Power ? "²" : "²/Hz");
    d.columns.push_back({"linear_value", u, r.values});
    if (!r.peakHold.empty()) d.columns.push_back({"linear_peak_hold", u, r.peakHold});
    d.settings = {{"N", static_cast<int>(r.size)}, {"channel", static_cast<int>(r.settings.channel)}, {"quadrature", static_cast<int>(r.settings.quadrature)},
        {"iq", r.settings.iq}, {"declared_unit", r.settings.declaredUnit}, {"window", static_cast<int>(r.settings.window)},
        {"window_names", QJsonArray{"rectangular", "hann", "hamming", "blackman", "blackman-harris"}},
        {"quantity", static_cast<int>(r.settings.quantity)}, {"quantity_names", QJsonArray{"amplitude", "power", "psd"}},
        {"remove_mean", r.settings.removeMean}, {"average", static_cast<int>(r.settings.average)},
        {"average_names", QJsonArray{"none", "block", "exponential"}}, {"average_count_setting", static_cast<int>(r.settings.averageCount)},
        {"alpha", exactNumber(r.settings.alpha)}, {"averaged", static_cast<int>(r.averaged)}, {"peak_hold", r.settings.peakHold},
        {"average_first_step", QString::number(r.averageFirstStep)}, {"hold_first_step", QString::number(r.holdFirstStep)},
        {"skipped_hops", QString::number(r.skippedHops)}, {"coherent_gain", exactNumber(r.coherentGain)}, {"enbw_hz", exactNumber(r.enbw)},
        {"bin_spacing_hz", exactNumber(r.binSpacing)}, {"normalization", "amplitude=abs(DFT)/sum(w); power=abs(DFT)^2/(N*sum(w^2)); psd=abs(DFT)^2/(Fs*sum(w^2)); real interior bins doubled"},
        {"average_history_raw_available", r.averageFirstStep >= r.firstStep && r.holdFirstStep >= r.firstStep}};
    interval(d, r.firstStep, r.lastStep); return d;
}
WindowSnapshot pairWindowSnapshot(const PairResult& r)
{
    WindowSnapshot d; d.raw = r.frame; d.kind = r.kind == PairKind::Constellation ? WindowSnapshot::Kind::XY : WindowSnapshot::Kind::Correlation;
    if (r.state != PairResult::State::Ready) return d;
    const auto& s = r.settings;
    d.settings = {{"first_channel", static_cast<int>(s.first)}, {"second_channel", static_cast<int>(s.second)}, {"auto_correlation", s.autoCorrelation},
        {"remove_mean", s.removeMean}, {"absolute_peak", s.absolutePeak}, {"N", static_cast<int>(s.size)}, {"max_lag", static_cast<int>(s.maxLag)},
        {"normalization", static_cast<int>(s.normalization)}, {"normalization_names", QJsonArray{"raw", "biased", "unbiased", "coefficient-full-window-energy"}},
        {"formula", "rAB[l]=sum(A[n]*B[n+l]); positive lag means B later"}, {"persistence_steps", QString::number(s.persistenceSteps)},
        {"density_size", static_cast<int>(s.densitySize)}, {"invalid_pairs", QString::number(r.invalid)}, {"gaps_seen", QString::number(r.gaps)}, {"evicted_pairs", QString::number(r.evicted)}};
    if (r.kind == PairKind::Constellation) {
        d.axisName = "X"; d.axisUnit = r.firstUnit;
        d.columns = {{r.firstName, r.firstUnit, {}}, {r.secondName, r.secondUnit, {}}};
        for (const auto& p : r.points) { d.axis.push_back(p.x); d.steps.push_back(p.step); d.times.push_back(p.time); d.columns[0].values.push_back(p.x); d.columns[1].values.push_back(p.y); }
        d.displayX = d.axis; d.displayY = d.columns[1].values;
        d.settings.insert("density_bounds", QJsonArray{exactNumber(r.xMin), exactNumber(r.xMax), exactNumber(r.yMin), exactNumber(r.yMax)});
    } else {
        d.axisName = "lag"; d.axisUnit = "samples";
        for (const auto lag : r.lags) d.axis.push_back(lag);
        d.displayX = d.axis; d.displayY = r.correlations;
        d.columns.push_back({"correlation", s.normalization == CorrelationNormalization::Coefficient ? QStringLiteral("1") : unit(r.firstUnit)+"·"+unit(r.secondUnit), r.correlations});
        d.settings.insert("peak_lag", r.lags[r.peakIndex]);
    }
    interval(d, r.firstStep, r.lastStep); return d;
}
RegionStatistics regionStatistics(std::span<const double> values, std::size_t first, std::size_t last)
{
    if (first > last) std::swap(first, last);
    if (last >= values.size()) throw std::out_of_range("Measurement endpoints outside the pinned window.");
    RegionStatistics s; s.count = last-first+1; double scale = 0;
    s.minimumIndex = s.maximumIndex = first;
    for (auto i = first; i <= last; ++i) {
        const auto v = values[i]; if (!std::isfinite(v)) { ++s.invalid; continue; }
        scale = std::max(scale, std::abs(v));
        if (!s.minimum || v < *s.minimum) { s.minimum = v; s.minimumIndex = i; }
        if (!s.maximum || v > *s.maximum) { s.maximum = v; s.maximumIndex = i; }
    }
    if (s.invalid) { s.minimum.reset(); s.maximum.reset(); return s; }
    Sum sum, squares, shifts, variance;
    const double anchor = values[first];
    auto shifted = [&](double value) {
        if (scale == 0) return 0.;
        const double difference = value-anchor;
        return std::isfinite(difference) ? difference/scale : value/scale-anchor/scale;
    };
    for (auto i = first; i <= last; ++i) { const auto x = scale == 0 ? 0 : values[i]/scale; sum.add(x); squares.add(x*x); shifts.add(shifted(values[i])); }
    const double mean = sum.total()/static_cast<double>(s.count);
    const double shiftMean = shifts.total()/static_cast<double>(s.count);
    for (auto i = first; i <= last; ++i) { const auto delta = shifted(values[i])-shiftMean; variance.add(delta*delta); }
    auto finite = [](double value) -> std::optional<double> { return std::isfinite(value) ? std::optional<double>(value) : std::nullopt; };
    s.mean = finite(mean*scale); s.rms = finite(std::sqrt(std::max(0., squares.total()/static_cast<double>(s.count)))*scale);
    s.standardDeviation = finite(std::sqrt(std::max(0., variance.total()/static_cast<double>(s.count)))*scale);
    s.peakToPeak = finite(*s.maximum-*s.minimum); return s;
}
QString cursorReadout(const WindowSnapshot& d, std::size_t a, std::size_t b)
{
    validate(d); if (a >= d.axis.size() || b >= d.axis.size()) throw std::out_of_range("Cursor outside pinned window.");
    QStringList lines{"A • "+position(d, a), "B • "+position(d, b)};
    lines << QStringLiteral("B−A • Δ%1=%2 %3").arg(d.axisName, exactNumber(d.axis[b]-d.axis[a]), unit(d.axisUnit));
    if (!d.steps.empty()) {
        const double dt = d.raw->rateNumerator ? static_cast<double>(d.steps[b]-d.steps[a])*static_cast<double>(d.raw->rateDenominator)/static_cast<double>(d.raw->rateNumerator) : d.times[b]-d.times[a];
        lines << QStringLiteral("Δstep=%1 • Δt=%2 s • 1/Δt=%3 Hz").arg(d.steps[b]-d.steps[a]).arg(exactNumber(dt), dt == 0 ? QStringLiteral("unavailable") : exactNumber(1/dt));
    } else if (d.kind == WindowSnapshot::Kind::Correlation && d.raw->rateNumerator) {
        lines << "Δlag_time="+exactNumber((d.axis[b]-d.axis[a])*static_cast<double>(d.raw->rateDenominator)/static_cast<double>(d.raw->rateNumerator))+" s";
    }
    for (const auto& c : d.columns) {
        const auto va = c.values[a], vb = c.values[b], delta = vb-va;
        const auto difference = !std::isfinite(va) || !std::isfinite(vb) ? QStringLiteral("unavailable (nonfinite input)") :
            !std::isfinite(delta) ? QStringLiteral("unavailable (overflow)") : exactNumber(delta);
        lines << QStringLiteral("%1 [%2] • A=%3 • B=%4 • B−A=%5").arg(c.name, unit(c.unit), exactNumber(va), exactNumber(vb), difference);
    }
    return lines.join('\n');
}
QString measurementReadout(const WindowSnapshot& d, std::size_t a, std::size_t b)
{
    validate(d); if (a > b) std::swap(a, b);
    if (b >= d.axis.size()) throw std::out_of_range("Measurement outside pinned window.");
    const bool raw = !d.steps.empty();
    QStringList lines{raw ? "A–B inclusive raw samples/pairs (display gain/offset ignored)" : "A–B inclusive derived bins/lags in linear units (not time-domain signal RMS)"};
    if (raw) {
        std::uint64_t missing = 0;
        for (auto i = a+1; i <= b; ++i) if (d.steps[i] > d.steps[i-1]+1) missing += static_cast<std::uint64_t>(d.steps[i]-d.steps[i-1]-1);
        lines << QStringLiteral("Steps %1–%2 • missing steps %3 • statistics cover observed positions only").arg(d.steps[a]).arg(d.steps[b]).arg(missing);
    }
    for (const auto& c : d.columns) lines << statsText(c, regionStatistics(c.values, a, b), &d);
    if (!raw) {
        const auto input = timeWindow(d.raw);
        const auto first = static_cast<std::size_t>(std::lower_bound(input.steps.begin(), input.steps.end(), d.inputFirstStep)-input.steps.begin());
        const auto end = static_cast<std::size_t>(std::upper_bound(input.steps.begin(), input.steps.end(), d.inputLastStep)-input.steps.begin());
        lines << QStringLiteral("Raw latest-transform input • steps %1–%2 (earlier averaging/hold input is not retained here)").arg(d.inputFirstStep).arg(d.inputLastStep);
        if (end > first) for (const auto& c : input.columns) lines << statsText(c, regionStatistics(c.values, first, end-1), &input);
        else lines << "Raw transform input unavailable.";
    }
    return lines.join('\n');
}
QJsonObject windowMetadata(const WindowSnapshot& d, std::size_t a, std::size_t b)
{
    validate(d);
    return {{"format", "s7.window"}, {"version", 1}, {"bounded_window", true}, {"complete_run", false},
        {"kind", windowKind(d.kind)}, {"number_encoding", "f64 round-trip decimal strings; NaN, Infinity, -Infinity; integer identities/steps as decimal strings"},
        {"raw", rawMetadata(*d.raw)}, {"analysis", d.settings}, {"input_first_step", QString::number(d.inputFirstStep)}, {"input_last_step", QString::number(d.inputLastStep)},
        {"axis_name", d.axisName}, {"axis_unit", d.axisUnit}, {"columns", columnInfo(d.columns)},
        {"display_axis_name", d.displayAxisName}, {"display_axis_unit", d.displayAxisUnit},
        {"cursor_a_index", QString::number(a)}, {"cursor_b_index", QString::number(b)}, {"cursors", cursorReadout(d, a, b)},
        {"measurements", measurementReadout(d, a, b)}};
}
WindowExportResult exportWindow(const QString& path, const WindowSnapshot& d, std::size_t a, std::size_t b, WindowExportFormat format,
    const std::function<bool()>& cancelled, const std::function<void(int)>& progress)
{
    WindowExportResult result;
    try {
        const auto metadata = windowMetadata(d, a, b);
        QSaveFile file(path); file.setDirectWriteFallback(false);
        if (!file.open(QIODevice::WriteOnly)) { result.error = file.errorString(); return result; }
        auto write = [&](const QByteArray& bytes) { if (file.write(bytes) != bytes.size()) throw std::runtime_error(file.errorString().toStdString()); };
        auto stop = [&] { if (cancelled && cancelled()) { result.cancelled = true; throw std::runtime_error("Export cancelled; destination preserved."); } };
        const bool rawCsv = format == WindowExportFormat::RawCsv;
        const auto raw = timeWindow(d.raw);
        const auto total = format == WindowExportFormat::WindowJson ? raw.axis.size()+d.axis.size() : (rawCsv ? raw.axis.size() : d.axis.size());
        std::size_t done = 0;
        auto rows = [&](const WindowSnapshot& data, bool csvFormat) {
            for (std::size_t i = 0; i < data.axis.size(); ++i) {
                if (i % 128 == 0) { stop(); if (progress) progress(static_cast<int>(done*100/total)); }
                if (csvFormat) {
                    QByteArray line = QByteArray::number(i)+',';
                    if (!data.steps.empty()) line += QByteArray::number(data.steps[i]);
                    line += ','; if (!data.times.empty()) line += exactNumber(data.times[i]).toUtf8();
                    line += ','+exactNumber(data.axis[i]).toUtf8();
                    for (const auto& c : data.columns) line += ','+exactNumber(c.values[i]).toUtf8();
                    write(line+'\n');
                } else {
                    QJsonArray values; for (const auto& c : data.columns) values.append(exactNumber(c.values[i]));
                    QJsonObject row{{"index", QString::number(i)}, {"axis", exactNumber(data.axis[i])}, {"values", values}};
                    if (!data.displayAxisName.isEmpty()) row.insert("display_axis", exactNumber(data.displayX[i]));
                    if (!data.steps.empty()) { row.insert("step", QString::number(data.steps[i])); row.insert("time_s", exactNumber(data.times[i])); }
                    if (i) write(",");
                    write(json(row));
                }
                ++done;
            }
        };
        stop();
        if (format == WindowExportFormat::WindowJson) {
            write("{\"metadata\":"+json(metadata)+",\"raw_samples\":["); rows(raw, false);
            write("],\"displayed_rows\":["); rows(d, false); write("]}\n");
        } else {
            const auto& data = rawCsv ? raw : d;
            auto csvMetadata = metadata; csvMetadata.insert("csv_data", rawCsv ? "raw_acquisition" : "displayed_raw_pairs_or_derived_values");
            csvMetadata.insert("csv_axis_name", data.axisName); csvMetadata.insert("csv_axis_unit", data.axisUnit); csvMetadata.insert("csv_columns", columnInfo(data.columns));
            write("# "+json(csvMetadata)+'\n');
            QByteArray header = "index,step,model_time_s,axis";
            for (const auto& c : data.columns) header += ','+csv(c.name+" ["+unit(c.unit)+"]");
            write(header+'\n'); rows(data, true);
        }
        stop();
        if (!file.commit()) { result.error = file.errorString(); return result; }
        result.success = true; if (progress) progress(100);
    } catch (const std::exception& ex) { result.error = QString::fromUtf8(ex.what()); }
    return result;
}
struct WindowExportWorker::State {
    mutable std::mutex mutex;
    std::atomic<bool> cancel{false}, running{false};
    std::atomic<int> progress{0};
    std::optional<WindowExportResult> ready;
};
WindowExportWorker::WindowExportWorker() : state_(std::make_shared<State>()) {}
WindowExportWorker::~WindowExportWorker() { cancel(); }
bool WindowExportWorker::start(QString path, std::shared_ptr<const WindowSnapshot> snapshot, std::size_t a, std::size_t b, WindowExportFormat format)
{
    if (!snapshot) return false;
    {
        std::lock_guard lock(state_->mutex); if (state_->running.load()) return false;
        state_->running = true; state_->cancel = false; state_->progress = 0; state_->ready.reset();
    }
    static QThreadPool pool;
    static const bool initialized = [] { pool.setMaxThreadCount(1); return true; }(); (void)initialized;
    pool.start([state = state_, path = std::move(path), snapshot = std::move(snapshot), a, b, format] {
        auto result = exportWindow(path, *snapshot, a, b, format, [&] { return state->cancel.load(); }, [&](int value) { state->progress = value; });
        std::lock_guard lock(state->mutex); state->ready = std::move(result); state->running = false;
    });
    return true;
}
void WindowExportWorker::cancel() { state_->cancel = true; }
bool WindowExportWorker::busy() const { return state_->running.load(); }
int WindowExportWorker::progress() const { return state_->progress.load(); }
std::optional<WindowExportResult> WindowExportWorker::takeResult()
{
    std::lock_guard lock(state_->mutex); auto result = std::move(state_->ready); state_->ready.reset(); return result;
}
}
