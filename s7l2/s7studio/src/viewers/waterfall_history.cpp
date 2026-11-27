// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-S7-plugin-permission

#include "viewers/waterfall_history.hpp"

#include "analysis/spectrum.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace s7studio::viewers {
namespace {

WaterfallAxes axesFor(const s7::CommittedArraySample& sample)
{
    if (!sample.value || !sample.value.spectrum)
        throw std::invalid_argument("Waterfall requires a committed array with spectrum metadata.");
    const auto view = sample.value.lease.view();
    const auto& shape = view.descriptor();
    if (shape.type() != s7::SignalType::F32 || shape.rank() != 1 ||
        shape.element_count() < 2 || shape.element_count() > WaterfallHistory::maxBins)
        throw std::invalid_argument("Waterfall requires an F32 spectrum with 2 to 1048576 bins.");
    const auto& spectrum = *sample.value.spectrum;
    if (spectrum.window != s7::SpectrumWindow::PeriodicHann ||
        (spectrum.units != s7::SpectrumUnits::PowerPerBin && spectrum.units != s7::SpectrumUnits::PowerPerHz))
        throw std::invalid_argument("Unsupported Waterfall spectrum window or power units.");

    const auto normalize = [](s7::Rate rate) { return s7::Rate::reduced(rate.numerator, rate.denominator); };
    auto physicalRate = spectrum.sample_rate;
    if (physicalRate) physicalRate = normalize(*physicalRate);
    if (spectrum.units == s7::SpectrumUnits::PowerPerHz && !physicalRate)
        throw std::invalid_argument("Power per Hz requires a physical sample rate.");
    if (const auto& window = sample.value.sample_window) {
        if (window->missing_samples || (window->valid_sample_count && *window->valid_sample_count != shape.element_count()))
            throw std::invalid_argument("Waterfall requires a complete PSD sample block.");
        if (window->sample_rate && normalize(*window->sample_rate) != physicalRate)
            throw std::invalid_argument("PSD axis and source window have different physical sample rates.");
    }
    if (sample.domain_step == 0) throw std::invalid_argument("Waterfall requires a committed domain step.");
    return {static_cast<int>(shape.element_count()), spectrum.units, physicalRate, normalize(sample.rate)};
}

bool sourceGap(const WaterfallRow& previous, const WaterfallRow& current)
{
    if (previous.origin && current.origin) {
        if (previous.origin->source_id != current.origin->source_id || previous.origin->epoch != current.origin->epoch)
            return true;
        if (current.skippedDomainSteps == 0) {
            const auto before = previous.origin->sequence;
            const auto after = current.origin->sequence;
            if (after < before || after - before > 1) return true;
        }
    }
    if (current.skippedDomainSteps != 0 || !previous.sampleWindow || !current.sampleWindow) return false;
    const auto& previousWindow = *previous.sampleWindow;
    const auto& currentWindow = *current.sampleWindow;
    if (!previousWindow.first_sample || !currentWindow.first_sample) return false;
    const auto before = *previousWindow.first_sample;
    const auto after = *currentWindow.first_sample;
    return after < before || (previousWindow.valid_sample_count && after - before > *previousWindow.valid_sample_count);
}

QRgb powerColor(float power, const WaterfallScale& scale)
{
    const auto db = analysis::spectrumDecibels(power, analysis::SpectrumQuantity::Power, scale.reference);
    const auto level = (std::clamp(db, scale.minimumDb, scale.maximumDb) - scale.minimumDb)
        / (scale.maximumDb - scale.minimumDb);
    const auto channel = [](double value) { return static_cast<int>(std::lround(255.0 * std::clamp(value, 0.0, 1.0))); };
    if (scale.palette == WaterfallPalette::Gray) {
        const auto gray = channel(level);
        return qRgb(gray, gray, gray);
    }
    return qRgb(channel(3.0 * level), channel(3.0 * level - 1.0), channel(3.0 * level - 2.0));
}

} // namespace

int WaterfallAxes::signedBin(int column) const
{
    if (column < 0 || column >= bins) throw std::out_of_range("Waterfall column is outside the spectrum.");
    return column - bins / 2;
}

std::optional<double> WaterfallAxes::frequencyHz(int column) const
{
    const auto bin = signedBin(column);
    if (!physicalRate) return std::nullopt;
    return static_cast<double>(bin) * (physicalRate->to_hz() / static_cast<double>(bins));
}

double WaterfallAxes::modelSeconds(std::uint64_t domainStep) const
{
    return static_cast<double>(domainStep) / domainRate.to_hz();
}

WaterfallHistory::WaterfallHistory(std::size_t rowLimit) : rowLimit_(rowLimit)
{
    if (rowLimit == 0 || rowLimit > maxRows) throw std::invalid_argument("Waterfall row limit must be from 1 to 1024.");
}

WaterfallHistory::Append WaterfallHistory::append(const s7::CommittedArraySample& sample)
{
    const auto nextAxes = axesFor(sample);
    const bool axesChanged = axes_ && *axes_ != nextAxes;
    const auto& value = sample.value;
    WaterfallRow position{sample.domain_step, value.origin, value.sample_window, 0,
                         value.sample_window && value.sample_window->discontinuity_before};
    if (axes_ && !axesChanged) {
        const auto& previous = rows_.back().position;
        if (position.domainStep == previous.domainStep) return Append::Duplicate;
        if (position.domainStep < previous.domainStep)
            throw std::invalid_argument("Waterfall domain steps moved backwards. Clear the history before rebinding.");
        position.skippedDomainSteps = position.domainStep - previous.domainStep - 1;
        position.sourceDiscontinuity |= sourceGap(previous, position);
    }
    const auto powers = value.lease.values<s7::SignalType::F32>();
    if (!std::ranges::all_of(powers, [](float power) { return std::isfinite(power) && power >= 0; }))
        throw std::invalid_argument("Waterfall power must be finite and nonnegative.");

    // The first displayed column is the most-negative signed bin. In the
    // source's DFT order the negative-frequency tail precedes DC on display.
    const auto negativeStart = powers.end() - static_cast<std::ptrdiff_t>(powers.size() / 2);
    Row row{std::move(position), std::vector<float>(powers.size())};
    std::rotate_copy(powers.begin(), negativeStart, powers.end(), row.powers.begin());
    if (axesChanged) {
        std::deque<Row> replacement;
        replacement.push_back(std::move(row));
        rows_.swap(replacement);
    } else {
        rows_.push_back(std::move(row));
    }
    axes_ = nextAxes;
    const auto capacity = std::min(rowLimit_, powerBudget / (powers.size() * sizeof(float)));
    if (rows_.size() > capacity) rows_.pop_front();
    return axesChanged ? Append::AxesChanged : Append::Added;
}

void WaterfallHistory::clear()
{
    rows_.clear();
    axes_.reset();
}

WaterfallSnapshot WaterfallHistory::snapshot(WaterfallScale scale) const
{
    if (!std::isfinite(scale.minimumDb) || !std::isfinite(scale.maximumDb - scale.minimumDb) ||
        scale.maximumDb <= scale.minimumDb || !std::isfinite(scale.reference) || scale.reference <= 0.0 ||
        (scale.palette != WaterfallPalette::Gray && scale.palette != WaterfallPalette::Heat))
        throw std::invalid_argument("Waterfall requires a finite increasing dB range, positive reference and supported palette.");
    WaterfallSnapshot snapshot{axes_, scale, {}, {}};
    if (rows_.empty()) return snapshot;

    int lineCount = static_cast<int>(rows_.size());
    for (const auto& row : rows_)
        if (row.position.skippedDomainSteps || row.position.sourceDiscontinuity) ++lineCount;
    snapshot.image = QImage(axes_->bins, lineCount, QImage::Format_RGB32);
    if (snapshot.image.isNull()) throw std::runtime_error("Could not allocate the Waterfall image.");
    snapshot.lines.reserve(static_cast<std::size_t>(lineCount));
    for (const auto& row : rows_) {
        if (row.position.skippedDomainSteps || row.position.sourceDiscontinuity) {
            auto* line = reinterpret_cast<QRgb*>(snapshot.image.scanLine(static_cast<int>(snapshot.lines.size())));
            std::fill_n(line, axes_->bins, gapColor);
            snapshot.lines.emplace_back(std::nullopt);
        }
        auto* line = reinterpret_cast<QRgb*>(snapshot.image.scanLine(static_cast<int>(snapshot.lines.size())));
        std::ranges::transform(row.powers, line, [&](float power) { return powerColor(power, scale); });
        snapshot.lines.emplace_back(row.position);
    }
    return snapshot;
}

} // namespace s7studio::viewers
