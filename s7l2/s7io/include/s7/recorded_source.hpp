// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: LGPL-3.0-or-later

#ifndef S7IO_INCLUDE_S7_RECORDED_SOURCE_HPP
#define S7IO_INCLUDE_S7_RECORDED_SOURCE_HPP

#include "s7/file_io.hpp"
#include <cmath>
#include <vector>
#include <type_traits>

namespace s7::io {
enum class ReplayEnd { Error, Hold, Repeat };
enum class RecordingFormat { Gnuplot, CompactBinary };

/// Preloaded, uniformly sampled recording. File indices must start at zero
/// and be contiguous: no silent interpolation or skipped missing samples.
template <typename T>
class RecordedSource final : public Element {
public:
    RecordedSource(const std::filesystem::path& path, double sample_rate_hz = 0,
                   RecordingFormat format = RecordingFormat::CompactBinary, ReplayEnd end = ReplayEnd::Error)
        : Element("RecordedSource", true), end_(end)
    {
        if (!std::isfinite(sample_rate_hz) || sample_rate_hz < 0)
            throw std::invalid_argument("RecordedSource sample_rate_hz must be zero (file rate) or finite and positive.");
        std::ifstream file(path, std::ios::binary);
        if (!file) throw std::runtime_error("Cannot open recording: '" + path.string() + "'.");
        auto reader = format == RecordingFormat::Gnuplot ? make_gnuplot_format<T>() : make_compact_binary_format<T>();
        reader->read_header(file);
        rate_=reader->rate();
        description_=reader->description();
        if (sample_rate_hz>0 && Rate::from_hz(sample_rate_hz)!=rate_)
            throw std::invalid_argument("RecordedSource sample_rate_hz does not match the recording header.");
        std::uint64_t index{};
        T value{};
        while (reader->read_sample(file, index, value)) {
            if (index != samples_.size()) throw std::invalid_argument("Recording indices must start at zero and be contiguous.");
            if constexpr (std::is_same_v<T, RealValue>) {
                if (math::isnan(value) || math::isinf(value)) throw std::invalid_argument("Recording samples must be finite.");
            } else {
                if (math::isnan(value.real()) || math::isinf(value.real()) ||
                    math::isnan(value.imag()) || math::isinf(value.imag()))
                    throw std::invalid_argument("Recording samples must be finite.");
            }
            samples_.push_back(value);
        }
        if (file.bad()) throw std::runtime_error("Failed to read recording: '" + path.string() + "'.");
        if (samples_.empty()) throw std::invalid_argument("Recording must contain at least one sample.");
        register_output(0, port_names::out, detail::SignalTraits<T>::type, &output_);
    }
    void step() override {
        if (index_ == samples_.size()) {
            if (end_ == ReplayEnd::Error) throw std::runtime_error("RecordedSource reached end of recording.");
            if (end_ == ReplayEnd::Hold) { output_ = samples_.back(); return; }
            index_ = 0;
        }
        output_ = samples_[index_++];
    }
    [[nodiscard]] std::optional<Rate> declared_rate() const override { return rate_; }
    [[nodiscard]] const std::string& description() const { return description_; }
private:
    Rate rate_{};
    std::string description_;
    ReplayEnd end_;
    std::vector<T> samples_;
    std::size_t index_{};
    T output_{};
};
/// Buffered matching sink. Opening/truncation is deferred until its first step.
template <typename T>
class RecordingSink final : public FileSinkElement<T> {
public:
    RecordingSink(const std::filesystem::path& path, double sample_rate_hz = 1000,
                  RecordingFormat format = RecordingFormat::CompactBinary, std::string description = {})
        : FileSinkElement<T>("RecordingSink",path,
            format == RecordingFormat::CompactBinary ?
                make_compact_binary_format<T>(Rate::from_hz(sample_rate_hz),std::move(description)) :
                make_gnuplot_format<T>(Rate::from_hz(sample_rate_hz),std::move(description)),
            std::ios::binary | std::ios::trunc) {}
};
using RealRecordingSink = RecordingSink<RealValue>;
using ComplexRecordingSink = RecordingSink<ComplexValue>;
using RealRecordedSource = RecordedSource<RealValue>;
using ComplexRecordedSource = RecordedSource<ComplexValue>;
}
#endif
