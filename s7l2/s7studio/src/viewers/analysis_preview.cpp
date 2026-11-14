// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-S7-plugin-permission
#include "analysis_preview.hpp"
#include <algorithm>
#include <cmath>
#include <s7spectral/analysis.hpp>

namespace s7studio::viewers {
std::optional<PreviewData> prepareAnalysisPreview(const PreviewRequest &request) {
    const bool map = request.descriptor.type == "TimeFrequency";
    if (!map && request.descriptor.type != "FrequencyResponse")
        return {};
    PreviewData result;
    result.revision = request.revision;
    result.summary = "Waiting for captured samples";
    if (!request.source)
        return result;
    result.run = request.source->runId();
    result.captureSource = request.source;
    auto state = request.source->status();
    result.capture = state;
    try {
        if (state.incomplete())
            throw std::invalid_argument("Incomplete capture; analysis unavailable");
        const int count = map ? 1 : 2;
        if (request.descriptor.inputs.size() != count)
            throw std::invalid_argument("Connect all analysis inputs");
        QVector<observation::ChannelId> ids;
        const auto channels = request.source->channels();
        const QStringList roles = map ? QStringList{"in"} : QStringList{"input", "output"};
        for (const auto &role : roles) {
            const auto input = std::find_if(request.descriptor.inputs.begin(), request.descriptor.inputs.end(),
                [&](const auto &value) { return value.name.endsWith('/' + role); });
            if (input == request.descriptor.inputs.end())
                throw std::invalid_argument("Connect all named analysis roles");
            const auto it = std::find_if(channels.begin(), channels.end(), [&](const auto &channel) {
                return channel.name == input->name;
            });
            if (it == channels.end())
                throw std::invalid_argument("Analysis input has no captured channel");
            ids.push_back(it->id);
        }
        QVector<QVector<observation::Sample>> samples(count);
        // Complete small recordings preserve early transients as well as their
        // tails. A bounded paginated scan; completed results are cached by the host.
        const bool whole = state.finished && state.samples <= 65536;
        if (whole) {
            std::uint64_t cursor = 0;
            while (cursor < state.samples) {
                const auto page = request.source->read(cursor, qsizetype(state.samples - cursor));
                if (!page.error.isEmpty() || page.next <= cursor || page.next > state.samples)
                    throw std::invalid_argument("Could not read the complete analysis capture");
                for (const auto &sample : page.samples)
                    for (int i = 0; i < ids.size(); ++i)
                        if (sample.channel == ids[i])
                            samples[i].push_back(sample);
                cursor = page.next;
            }
        } else {
            const auto tail = request.source->tail(ids);
            for (int i = 0; i < count; ++i)
                samples[i] = tail.channels[i].samples;
        }
        if (samples[0].size() < (map ? 16 : 2)) {
            result.summary = map ? "Waiting for at least 16 samples" : "Waiting for the impulse capture";
            return result;
        }
        const auto &first = samples[0].front();
        const auto n = samples[0].size();
        const double dt = (samples[0].back().timeSeconds - first.timeSeconds) / double(n - 1);
        if (!(dt > 0) || !std::isfinite(dt))
            throw std::invalid_argument("Invalid analysis sampling interval");
        std::vector<std::vector<double>> input(count);
        for (int c = 0; c < count; ++c) {
            if (samples[c].size() != n)
                throw std::invalid_argument("Unpaired analysis samples");
            for (qsizetype i = 0; i < n; ++i) {
                const auto &s = samples[c][i];
                if (s.step != first.step + i || !std::isfinite(s.value) || !std::isfinite(s.timeSeconds) ||
                    std::abs(s.timeSeconds - (first.timeSeconds + double(i) * dt)) > dt * 1e-6)
                    throw std::invalid_argument("Analysis needs finite, consecutive, uniformly sampled data");
                input[c].push_back(s.value);
            }
            result.rawPositions += std::size_t(n);
        }
        const auto interval = QString("%1–%2 s").arg(first.timeSeconds, 0, 'g', 6)
                                                   .arg(samples[0].back().timeSeconds, 0, 'g', 6);
        if (map) {
            std::size_t window = 8;
            while (window < 256 && window * 2 <= std::size_t(n) / 2)
                window *= 2;
            const auto transform = s7spectral::spectrogram(input[0], 1 / dt, window, window / 2);
            auto grid = std::make_shared<analysis::SurfaceGrid>();
            grid->x = transform.times;
            for (auto &time : grid->x)
                time += first.timeSeconds;
            const auto rows = std::clamp<std::size_t>(
                std::size_t(std::upper_bound(transform.frequencies.begin(), transform.frequencies.end(), 100) -
                            transform.frequencies.begin()), 2, transform.frequencies.size());
            grid->y.assign(transform.frequencies.begin(), transform.frequencies.begin() + rows);
            const auto columns = grid->x.size();
            for (std::size_t f = 0; f < rows; ++f)
                for (std::size_t t = 0; t < columns; ++t) {
                    const auto index = t * transform.frequencies.size() + f;
                    grid->values.push_back({std::abs(transform.coefficients[index]), 0});
                    grid->present.push_back(transform.valid[index]);
                }
            grid->received = grid->values.size();
            result.vertices = grid->received;
            result.surface = std::move(grid);
            result.axes = {"Time (s)", "Frequency (Hz)", "STFT magnitude"};
            result.summary = "STFT magnitude • " + interval;
            result.status = QString("%1 • %2 samples • periodic Hann, FFT %3, hop %4 • peak-tone amplitude")
                                .arg(whole ? "Complete captured interval" : "Recent captured interval")
                                .arg(n).arg(window).arg(window / 2);
        } else {
            if (!state.finished || !whole) {
                result.summary = state.finished ? "Long recording — use the full response viewer"
                                                : "Finish the capture to preview its frequency response";
                return result;
            }
            const auto response = s7spectral::impulseResponse(input[0], input[1], 1 / dt);
            QList<QPointF> points;
            const auto last = response.frequency.size() - 1;
            std::size_t previous = 0;
            for (int i = 0; i < 1024; ++i) {
                const auto k = std::clamp<std::size_t>(std::size_t(std::llround(std::exp(std::log(double(last)) * i / 1023))), 1, last);
                if (k == previous)
                    continue;
                previous = k;
                const auto magnitude = std::abs(response.values[k]);
                if (magnitude > 0)
                    points.append({std::log10(response.frequency[k]), 20 * std::log10(magnitude)});
            }
            result.vertices = std::size_t(points.size());
            result.traces.push_back({"Measured gain", {std::move(points)}});
            result.timeAxis = "log10(f / 1 Hz)";
            result.axes = {result.timeAxis, "Gain (dB re 1)"};
            result.summary = "Measured frequency response • " + interval;
            result.status = "Finite-record impulse estimate • actual pulse-height normalization • " + interval;
        }
    } catch (const std::exception &e) {
        result.summary = "Analysis unavailable";
        result.status = QString::fromUtf8(e.what());
    }
    return result;
}
} // namespace s7studio::viewers
