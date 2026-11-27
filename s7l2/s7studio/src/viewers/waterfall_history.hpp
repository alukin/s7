// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-S7-plugin-permission

#pragma once

#include <s7/runtime.hpp>

#include <QImage>

#include <deque>
#include <optional>
#include <vector>

namespace s7studio::viewers {

enum class WaterfallPalette { Gray, Heat };

struct WaterfallScale {
    double minimumDb = -100.0;
    double maximumDb = 0.0;
    double reference = 1.0;
    WaterfallPalette palette = WaterfallPalette::Heat;
};

struct WaterfallAxes {
    int bins;
    s7::SpectrumUnits units;
    std::optional<s7::Rate> physicalRate;
    s7::Rate domainRate;

    [[nodiscard]] int signedBin(int column) const;
    [[nodiscard]] std::optional<double> frequencyHz(int column) const;
    [[nodiscard]] double modelSeconds(std::uint64_t domainStep) const;
    bool operator==(const WaterfallAxes&) const = default;
};

struct WaterfallRow {
    std::uint64_t domainStep;
    std::optional<s7::ArraySourceOccurrence> origin;
    std::optional<s7::ArraySampleWindow> sampleWindow;
    std::uint64_t skippedDomainSteps;
    bool sourceDiscontinuity;
};

struct WaterfallSnapshot {
    std::optional<WaterfallAxes> axes;
    WaterfallScale scale;
    QImage image;
    // Oldest row first. A disengaged entry is a gap stripe before the next row.
    std::vector<std::optional<WaterfallRow>> lines;
};

// GUI-thread history. The caller can release the lease after append copies
// the powers. Each snapshot owns its raster and row metadata.
class WaterfallHistory {
public:
    static constexpr std::size_t maxBins = 1U << 20;
    static constexpr std::size_t maxRows = 1024;
    static constexpr std::size_t powerBudget = 16U << 20;
    static constexpr QRgb gapColor = qRgb(0, 104, 176);

    enum class Append { Added, Duplicate, AxesChanged };

    explicit WaterfallHistory(std::size_t rowLimit = 256);
    [[nodiscard]] Append append(const s7::CommittedArraySample& sample);
    void clear();
    [[nodiscard]] WaterfallSnapshot snapshot(WaterfallScale scale = {}) const;

private:
    struct Row {
        WaterfallRow position;
        std::vector<float> powers;
    };

    const std::size_t rowLimit_;
    std::optional<WaterfallAxes> axes_;
    std::deque<Row> rows_;
};

} // namespace s7studio::viewers
