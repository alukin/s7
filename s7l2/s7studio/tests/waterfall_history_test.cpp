// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-S7-plugin-permission

#include "viewers/waterfall_history.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
using namespace s7studio::viewers;
using s7::ArrayDescriptor;
using s7::ArraySpectrum;
using s7::CommittedArraySample;
using s7::Rate;
using s7::SignalType;
using s7::SpectrumUnits;
using s7::SpectrumWindow;

void require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

template<class Operation>
void rejects(Operation operation, const char* message)
{
    try {
        operation();
    } catch (const std::invalid_argument&) {
        return;
    }
    throw std::runtime_error(message);
}

CommittedArraySample sample(s7::ArrayPool& pool, std::span<const float> powers, std::uint64_t step,
                            std::optional<Rate> frequencyRate = Rate{1600, 1})
{
    auto writer = pool.try_acquire().value();
    std::ranges::copy(powers, writer.values<SignalType::F32>().begin());
    return {{25, 1}, step, {std::move(writer).seal(), {}, {},
        ArraySpectrum{SpectrumUnits::PowerPerBin, SpectrumWindow::PeriodicHann, frequencyRate}}};
}

void checkAxisAndPixels()
{
    const ArrayDescriptor shape(SignalType::F32, std::array<std::int64_t, 1>{8});
    s7::ArrayPool pool(shape, 1, 0, std::make_shared<s7::ArrayMemoryBudget>(shape.byte_size()));
    const std::array<float, 8> powers{1, 0.1f, 0.01f, 0.001f, 0, 2, 0.0000001f, 0.0001f};
    WaterfallHistory history;
    {
        const auto input = sample(pool, powers, 10);
        require(history.append(input) == WaterfallHistory::Append::Added, "Initial PSD row did not append");
        require(std::ranges::equal(input.value.lease.values<SignalType::F32>(), powers),
                "Presentation changed the graph's bin order or power values");
    }
    require(pool.try_acquire().has_value(), "History retained a graph buffer");
    const auto gray = history.snapshot({-60, 0, 1, WaterfallPalette::Gray});
    const auto heat = history.snapshot({-60, 0, 1, WaterfallPalette::Heat});
    const auto& axes = *gray.axes;
    require(gray.image.size() == QSize(8, 1) && gray.lines.size() == 1, "Incorrect raster geometry");
    require(axes.units == SpectrumUnits::PowerPerBin && axes.physicalRate == Rate{1600, 1},
            "Frequency axes lost the spectrum provenance");
    require(gray.lines.front()->domainStep == 10 && std::abs(axes.modelSeconds(10) - 0.4) < 1e-12,
            "Row time used physical sample cadence instead of the model domain");

    // The PSD inputs are powers at 0, -10, -20, -30, -infinity, ~3, -70
    // and -40 dB. Values in image order start at negative Nyquist.
    constexpr std::array<int, 8> grayscale{0, 255, 0, 85, 255, 213, 170, 128};
    constexpr std::array<QRgb, 8> thermal{
        qRgb(0, 0, 0), qRgb(255, 255, 255), qRgb(0, 0, 0), qRgb(255, 0, 0),
        qRgb(255, 255, 255), qRgb(255, 255, 128), qRgb(255, 255, 0), qRgb(255, 128, 0)
    };
    constexpr std::array<double, 8> hertz{-800, -600, -400, -200, 0, 200, 400, 600};
    for (int x = 0; x < 8; ++x) {
        const auto column = static_cast<std::size_t>(x);
        const auto level = grayscale[column];
        require(axes.signedBin(x) == x - 4 && axes.frequencyHz(x) == hertz[column], "Incorrect FFT bin/Hz order");
        require(gray.image.pixel(x, 0) == qRgb(level, level, level) && heat.image.pixel(x, 0) == thermal[column],
                "Power mapping differs from the exact color oracle");
    }
    const auto changed = history.snapshot({-20, 0, 10, WaterfallPalette::Gray});
    require(changed.image.pixel(4, 0) == qRgb(128, 128, 128) && changed.image.pixel(5, 0) == qRgb(0, 0, 0),
            "Changing the dB reference did not recolor existing rows");
}

void checkContinuityAndBounds()
{
    const ArrayDescriptor shape(SignalType::F32, std::array<std::int64_t, 1>{5});
    s7::ArrayPool pool(shape, 1, 0, std::make_shared<s7::ArrayMemoryBudget>(shape.byte_size()));
    WaterfallHistory history(4);
    const auto append = [&](std::uint64_t step, std::uint64_t sequence, std::uint64_t first,
                            bool discontinuity = false, std::uint64_t epoch = 0) {
        const std::array<float, 5> powers{1, 0, 0, 0.1f, 0};
        auto input = sample(pool, powers, step, std::nullopt);
        input.value.origin = s7::ArraySourceOccurrence{7, epoch, sequence};
        input.value.sample_window = s7::ArraySampleWindow{std::nullopt, first, 5, discontinuity, false};
        return history.append(input);
    };
    require(append(4, 8, 100) == WaterfallHistory::Append::Added, "Could not append unknown-rate row");
    require(append(4, 8, 100) == WaterfallHistory::Append::Duplicate, "Duplicate delivery advanced the history");
    static_cast<void>(append(7, 11, 115));
    static_cast<void>(append(8, 12, 130));
    static_cast<void>(append(9, 13, 135, true));
    const auto beforeEviction = history.snapshot({-40, 0, 1, WaterfallPalette::Gray});
    const auto& lines = beforeEviction.lines;
    require(lines.size() == 7 && lines[0]->domainStep == 4 &&
                !lines[1] && lines[2]->skippedDomainSteps == 2 && !lines[2]->sourceDiscontinuity &&
                !lines[3] && lines[4]->skippedDomainSteps == 0 && lines[4]->sourceDiscontinuity &&
                !lines[5] && lines[6]->sourceDiscontinuity,
            "Model step gaps, source interval gaps and source flags were conflated");
    require(lines[4]->sampleWindow->first_sample == 130 && lines[4]->origin->sequence == 12,
            "History lost source interval or sequence identity");
    require(beforeEviction.image.pixel(3, 1) == WaterfallHistory::gapColor,
            "A gap stripe was indistinguishable from a data row");
    require(beforeEviction.axes->signedBin(0) == -2 && beforeEviction.axes->signedBin(4) == 2 &&
                !beforeEviction.axes->frequencyHz(0) && beforeEviction.image.pixel(2, 0) == qRgb(255, 255, 255) &&
                beforeEviction.image.pixel(0, 0) == qRgb(191, 191, 191),
            "Odd-length transform was shifted incorrectly or acquired invented Hz");

    static_cast<void>(append(10, 13, 135));
    const auto bounded = history.snapshot();
    require(bounded.lines.size() == 7 && bounded.lines[1]->domainStep == 7 && bounded.lines.back()->domainStep == 10 &&
                !bounded.lines.back()->sourceDiscontinuity && beforeEviction.lines[0]->domainStep == 4,
            "History did not evict its oldest row or changed an existing snapshot");
    static_cast<void>(append(11, 1, 0, false, 1));
    const auto restarted = history.snapshot();
    require(restarted.lines.back()->origin->epoch == 1 && restarted.lines.back()->sourceDiscontinuity &&
                !restarted.lines[restarted.lines.size() - 2], "Source epoch change did not mark a gap");

    const std::array<float, 5> zero{};
    auto newRate = sample(pool, zero, 12, Rate{200, 2});
    newRate.value.spectrum->units = SpectrumUnits::PowerPerHz;
    require(history.append(newRate) == WaterfallHistory::Append::AxesChanged, "Axis change was not reported");
    const auto newSegment = history.snapshot();
    require(newSegment.lines.size() == 1 && newSegment.axes->frequencyHz(0) == -40 &&
                newSegment.axes->units == SpectrumUnits::PowerPerHz,
            "Changing axes joined incompatible spectra");
    history.clear();
    const auto empty = history.snapshot();
    require(!empty.axes && empty.image.isNull() && empty.lines.empty(), "Clearing left stale axes or rows");
}

void checkBadInputs()
{
    const ArrayDescriptor shape(SignalType::F32, std::array<std::int64_t, 1>{4});
    s7::ArrayPool pool(shape, 1, 0, std::make_shared<s7::ArrayMemoryBudget>(shape.byte_size()));
    WaterfallHistory history;
    const std::array<float, 4> zero{};
    static_cast<void>(history.append(sample(pool, zero, 2)));
    rejects([&] { static_cast<void>(history.append(sample(pool, zero, 1))); }, "Out-of-order row accepted");
    {
        const std::array<float, 4> bad{0, 1, 2, -1};
        rejects([&] { static_cast<void>(history.append(sample(pool, bad, 3))); }, "Negative power accepted");
    }
    {
        const std::array<float, 4> bad{0, 1, 2, std::numeric_limits<float>::quiet_NaN()};
        rejects([&] { static_cast<void>(history.append(sample(pool, bad, 3))); }, "Non-finite power accepted");
    }
    {
        auto density = sample(pool, zero, 3, std::nullopt);
        density.value.spectrum->units = SpectrumUnits::PowerPerHz;
        rejects([&] { static_cast<void>(history.append(density)); }, "Density accepted without a physical rate");
        density.value.spectrum.reset();
        rejects([&] { static_cast<void>(history.append(density)); }, "Unlabelled array accepted as a PSD");
    }
    {
        auto mismatched = sample(pool, zero, 3);
        mismatched.value.sample_window = s7::ArraySampleWindow{Rate{800, 1}, 0, 4, false, false};
        rejects([&] { static_cast<void>(history.append(mismatched)); }, "Inconsistent physical rates accepted");
        mismatched.value.sample_window = s7::ArraySampleWindow{Rate{1600, 1}, 0, 3, false, true};
        rejects([&] { static_cast<void>(history.append(mismatched)); }, "Incomplete source block accepted");
    }
    {
        const ArrayDescriptor grid(SignalType::F32, std::array<std::int64_t, 2>{2, 2});
        s7::ArrayPool gridPool(grid, 1, 0, std::make_shared<s7::ArrayMemoryBudget>(grid.byte_size()));
        rejects([&] { static_cast<void>(history.append(sample(gridPool, zero, 3))); }, "Rank-two data accepted as a PSD");
    }
    rejects([&] { static_cast<void>(history.snapshot({0, -20, 1})); }, "Inverted dB bounds accepted");
    rejects([&] { static_cast<void>(history.snapshot({-20, 0, 0})); }, "Zero power reference accepted");
    const auto intact = history.snapshot();
    require(intact.lines.size() == 1 && intact.lines.back()->domainStep == 2, "Failed append changed the history");
}

void checkByteLimit()
{
    constexpr auto bins = WaterfallHistory::maxBins;
    const ArrayDescriptor shape(SignalType::F32, std::array{static_cast<std::int64_t>(bins)});
    s7::ArrayPool pool(shape, 1, 0, std::make_shared<s7::ArrayMemoryBudget>(shape.byte_size()));
    const std::vector<float> zero(bins);
    WaterfallHistory history(WaterfallHistory::maxRows);
    const auto capacity = WaterfallHistory::powerBudget / shape.byte_size();
    for (std::size_t step = 1; step <= capacity + 2; ++step) {
        auto input = sample(pool, zero, step);
        input.value.sample_window = s7::ArraySampleWindow{std::nullopt, std::nullopt, bins, true, false};
        static_cast<void>(history.append(input));
    }
    const auto image = history.snapshot();
    require(image.lines.size() == 2 * capacity && !image.lines.front() && image.lines[1]->domainStep == 3 &&
                static_cast<std::size_t>(image.image.sizeInBytes()) == 2 * WaterfallHistory::powerBudget &&
                image.image.pixel(static_cast<int>(bins - 1), static_cast<int>(2 * capacity - 1)) == qRgb(0, 0, 0),
            "Large spectra escaped the history byte budget");
}

} // namespace

int main() try {
    checkAxisAndPixels();
    checkContinuityAndBounds();
    checkBadInputs();
    checkByteLimit();
    return 0;
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
}
