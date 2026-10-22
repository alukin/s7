// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-S7-plugin-permission
#pragma once
#include "analysis/spectrum.hpp"
#include "analysis/pair_analysis.hpp"
#include <QStringList>

namespace s7studio::analysis {
struct WindowColumn { QString name, unit; std::vector<double> values; };
struct WindowSnapshot {
    enum class Kind { Time, Spectrum, XY, Correlation } kind = Kind::Time;
    std::shared_ptr<const observation::LiveFrame> raw;
    QString axisName, axisUnit;
    QString displayAxisName, displayAxisUnit;
    std::vector<double> axis, displayX, displayY, times;
    std::vector<std::int64_t> steps; // Empty for derived bins/lags.
    std::vector<WindowColumn> columns;
    QJsonObject settings;
    std::int64_t inputFirstStep = 0, inputLastStep = 0;
};
WindowSnapshot timeWindow(std::shared_ptr<const observation::LiveFrame> frame);
WindowSnapshot spectrumWindowSnapshot(const SpectrumResult& result);
WindowSnapshot pairWindowSnapshot(const PairResult& result);
QString exactNumber(double value); // C locale, round-trip f64; explicit nonfinite tokens.
QString windowKind(WindowSnapshot::Kind kind);
struct RegionStatistics {
    std::size_t count = 0, invalid = 0, minimumIndex = 0, maximumIndex = 0;
    std::optional<double> minimum, maximum, mean, rms, peakToPeak, standardDeviation;
};
RegionStatistics regionStatistics(std::span<const double> values, std::size_t first, std::size_t last);
QString cursorReadout(const WindowSnapshot& data, std::size_t a, std::size_t b);
QString measurementReadout(const WindowSnapshot& data, std::size_t a, std::size_t b);
QJsonObject windowMetadata(const WindowSnapshot& data, std::size_t a, std::size_t b);
enum class WindowExportFormat { WindowJson, RawCsv, DisplayedCsv };
struct WindowExportResult { bool success = false, cancelled = false; QString error; };
WindowExportResult exportWindow(const QString& path, const WindowSnapshot& data, std::size_t a, std::size_t b,
    WindowExportFormat format, const std::function<bool()>& cancelled = {},
    const std::function<void(int)>& progress = {});
// A single bounded, explicit export per panel, using a shared serial I/O pool.
// Jobs own immutable snapshots and never post callbacks into a deleted widget.
class WindowExportWorker {
public:
    WindowExportWorker();
    ~WindowExportWorker();
    bool start(QString path, std::shared_ptr<const WindowSnapshot> snapshot, std::size_t a, std::size_t b, WindowExportFormat format);
    void cancel();
    bool busy() const;
    int progress() const;
    std::optional<WindowExportResult> takeResult();
private:
    struct State;
    std::shared_ptr<State> state_;
};
}
