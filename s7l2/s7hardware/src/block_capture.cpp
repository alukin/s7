// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: LGPL-3.0-or-later

#include <s7/hardware.hpp>

#include <array>
#include <charconv>
#include <cstring>
#include <limits>
#include <numeric>
#include <utility>

namespace s7::hardware {
namespace {

Request checked_capture(Request request)
{
    request.validate();
    const bool iq = request.type == SignalType::C32 && request.channels.size() == 1;
    const bool audio = request.type == SignalType::F32 && request.channels.size() <= 2;
    if (request.direction != Direction::Capture || !(iq || audio))
        throw std::invalid_argument("Block capture requires one C32 IQ channel or one or two F32 audio channels");
    return request;
}

Rate block_rate(const Request& request)
{
    const auto divisor = std::gcd(request.rate.numerator, request.block_frames);
    const auto scale = request.block_frames / divisor;
    if (request.rate.denominator > std::numeric_limits<std::uint64_t>::max() / scale) {
        throw std::overflow_error("IQ block rate exceeds the rational rate range");
    }
    return Rate::reduced(request.rate.numerator / divisor, request.rate.denominator * scale);
}

ArrayDescriptor block_shape(const Request& request)
{
    const auto frames = static_cast<std::int64_t>(request.block_frames);
    if (request.type == SignalType::C32) return ArrayDescriptor{request.type, std::array{frames}};
    return ArrayDescriptor{request.type, std::array{static_cast<std::int64_t>(request.channels.size()), frames}};
}

std::optional<Rate> sample_rate_readback(const Descriptor& descriptor)
{
    const auto key = "channel." + descriptor.actual.channels.front() + ".sample_rate_hz";
    const auto setting = descriptor.backend_details.find(key);
    if (setting == descriptor.backend_details.end()) {
        // Audio providers validate their negotiated application rate and channel layout.
        if (descriptor.actual.type == SignalType::F32) return descriptor.actual.rate;
        return std::nullopt;
    }

    const auto& text = setting->second;
    double rate{};
    const auto end = text.data() + text.size();
    const auto parsed = std::from_chars(text.data(), end, rate);
    if (parsed.ec != std::errc{} || parsed.ptr != end) {
        throw std::runtime_error("SDR provider reported an invalid sample rate");
    }
    return Rate::from_hz(rate);
}

} // namespace

CaptureBlockSource::CaptureBlockSource(std::string_view name, Request request, std::uint64_t source_id)
    : ArrayElement(name, true), request_(checked_capture(std::move(request))), source_id_(source_id),
      graph_rate_(block_rate(request_)),
      shape_(block_shape(request_)),
      pool_(shape_, 2, 1, std::make_shared<ArrayMemoryBudget>(3 * shape_.byte_size())),
      output_(register_array_output(port_names::out, shape_))
{}

void CaptureBlockSource::step()
{
    if (!pending_) throw std::runtime_error("Block source requires explicit hardware Run activation");
    static_cast<void>(output_.exchange(std::exchange(pending_, {})));
}

RunStats Run::execute_block_capture(std::uint64_t steps, std::chrono::milliseconds timeout,
                                    std::stop_token stop, const StepObserver& observe)
{
    RunStats stats;
    auto& source = static_cast<CaptureBlockSource&>(*boundary_);
    std::stop_callback on_cancel(stop, [this] { cancel(); });
    const auto sample_rate = sample_rate_readback(descriptor());
    const auto frame_count = request().block_frames;
    const auto frame_size = request().frame_bytes();
    const bool stereo = request().channels.size() == 2;

    try {
        if (!stream_->cancelled()) stream_->start();
        while (stats.graph_frames < steps && !stream_->cancelled()) {
            auto writer = source.pool_.try_acquire();
            if (!writer) throw std::runtime_error("Block source execution pool exhausted");

            auto payload = writer->bytes();
            const auto capture = stereo ? block_.bytes() : payload;
            std::uint64_t first_frame{};
            std::size_t filled{};
            const auto deadline = std::chrono::steady_clock::now() + timeout;
            while (filled < frame_count) {
                const auto input = stream_->read(capture.subspan(filled * frame_size),
                                                 frame_count - filled, deadline, stop);
                if (!filled) first_frame = input.first_frame;
                filled += input.frames;
                stats.transferred_frames += input.frames;
                if (input.status != Status::Ok || !input.frames) {
                    stats.status = input.status == Status::Ok ? Status::BackendError : input.status;
                    stats.native_error = input.native_error;
                    break;
                }
            }
            if (stats.status != Status::Ok || stream_->cancelled()) break;

            if (stereo) {
                // Devices deliver interleaved audio. Planar graph rows are contiguous.
                for (std::size_t frame = 0; frame < frame_count; ++frame) {
                    std::memcpy(payload.data() + frame * sizeof(float),
                                capture.data() + frame * frame_size, sizeof(float));
                    std::memcpy(payload.data() + (frame_count + frame) * sizeof(float),
                                capture.data() + frame * frame_size + sizeof(float), sizeof(float));
                }
            }
            source.pending_ = {
                std::move(*writer).seal(),
                ArraySourceOccurrence{source.source_id_, descriptor().segment, stats.graph_frames + 1},
                ArraySampleWindow{sample_rate, first_frame, frame_count, false, false}
            };
            step_to(stats, stats.graph_frames + 1, observe);
        }
        if (stream_->cancelled()) stats.status = Status::Cancelled;
    } catch (...) {
        source.pending_ = {};
        stream_->stop();
        throw;
    }
    stream_->stop();
    return stats;
}

} // namespace s7::hardware
