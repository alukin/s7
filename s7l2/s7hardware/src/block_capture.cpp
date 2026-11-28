// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: LGPL-3.0-or-later

#include <s7/hardware.hpp>

#include <array>
#include <charconv>
#include <limits>
#include <numeric>
#include <utility>

namespace s7::hardware {
namespace {

Request checked_capture(Request request)
{
    request.validate();
    if (request.direction != Direction::Capture || request.type != SignalType::C32 || request.channels.size() != 1) {
        throw std::invalid_argument("IQ block capture requires one C32 receive channel");
    }
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

std::optional<Rate> sample_rate_readback(const Descriptor& descriptor)
{
    const auto key = "channel." + descriptor.actual.channels.front() + ".sample_rate_hz";
    const auto setting = descriptor.backend_details.find(key);
    if (setting == descriptor.backend_details.end()) return std::nullopt;

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
      shape_(SignalType::C32, std::array{static_cast<std::int64_t>(request_.block_frames)}),
      pool_(shape_, 2, 1, std::make_shared<ArrayMemoryBudget>(3 * shape_.byte_size())),
      output_(register_array_output(port_names::out, shape_))
{}

void CaptureBlockSource::step()
{
    if (!pending_) throw std::runtime_error("IQ block source requires explicit hardware Run activation");
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

    try {
        if (!stream_->cancelled()) stream_->start();
        while (stats.graph_frames < steps && !stream_->cancelled()) {
            auto writer = source.pool_.try_acquire();
            if (!writer) throw std::runtime_error("IQ block source execution pool exhausted");

            std::uint64_t first_frame{};
            std::size_t filled{};
            const auto deadline = std::chrono::steady_clock::now() + timeout;
            while (filled < frame_count) {
                const auto input = stream_->read(writer->bytes().subspan(filled * frame_size),
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
