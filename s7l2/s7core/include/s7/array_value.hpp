// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: LGPL-3.0-or-later

#ifndef S7CORE_INCLUDE_S7_ARRAY_VALUE_HPP
#define S7CORE_INCLUDE_S7_ARRAY_VALUE_HPP

#include "s7/array_pool.hpp"
#include "s7/rate.hpp"

#include <cstdint>
#include <optional>

namespace s7 {

// Source IDs are unique within a run. The source changes epoch before it
// reuses a sequence. These fields describe the occurrence, not its buffer.
struct ArraySourceOccurrence {
    std::uint64_t source_id{};
    std::uint64_t epoch{};
    std::uint64_t sequence{};

    bool operator==(const ArraySourceOccurrence&) const = default;
};

struct ArraySampleWindow {
    std::optional<Rate> sample_rate;
    std::optional<std::uint64_t> first_sample;
    std::optional<std::uint64_t> valid_sample_count;
    bool discontinuity_before{};
    bool missing_samples{};

    bool operator==(const ArraySampleWindow&) const = default;
};

enum class SpectrumUnits { PowerPerBin, PowerPerHz };
enum class SpectrumWindow { PeriodicHann };

// The vector holds all bins in standard DFT order.
struct ArraySpectrum {
    SpectrumUnits units;
    SpectrumWindow window;
    std::optional<Rate> sample_rate;
};

enum class ImagePixelFormat { Gray8, RGB8 };

// Image layout is compact, interleaved, row-major CPU storage.
struct ArrayImage {
    ImagePixelFormat pixel_format;
};

struct ArrayValue {
    ArrayPool::ReadLease lease;
    std::optional<ArraySourceOccurrence> origin{};
    std::optional<ArraySampleWindow> sample_window{};
    std::optional<ArraySpectrum> spectrum{};
    std::optional<ArrayImage> image{};

    explicit operator bool() const noexcept { return static_cast<bool>(lease); }
};

} // namespace s7

#endif // S7CORE_INCLUDE_S7_ARRAY_VALUE_HPP
