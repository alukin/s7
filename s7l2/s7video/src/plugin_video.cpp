// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-S7-plugin-permission

#include "s7video/mjpeg.hpp"
#include "jpeg_packet.hpp"
#include "video_metadata.hpp"

#include <s7/array_port.hpp>
#include <s7/element_plugin.hpp>
#include <s7/element_registry.hpp>
#include <s7/metadata_json.hpp>

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <limits>
#include <utility>

namespace {

using s7video::FrameFormat;
using s7video::MjpegCodec;

s7::ArrayDescriptor frameShape(const FrameFormat format)
{
    const auto width = static_cast<std::int64_t>(format.width);
    const auto height = static_cast<std::int64_t>(format.height);
    if (format.pixels == s7::ImagePixelFormat::Gray8)
        return s7::ArrayDescriptor(s7::SignalType::U8, std::array{height, width});
    return s7::ArrayDescriptor(s7::SignalType::U8, std::array{height, width, std::int64_t{3}});
}

// One committed value, one candidate and one optional retained observation.
constexpr std::size_t executionSlots = 2;
constexpr std::size_t observationSlots = 1;

auto poolBudget(const s7::ArrayDescriptor& shape)
{
    return std::make_shared<s7::ArrayMemoryBudget>((executionSlots + observationSlots) * shape.byte_size());
}

class VideoEncoder final : public s7::ArrayElement {
public:
    VideoEncoder(FrameFormat format, std::size_t capacity)
        : ArrayElement("VideoEncoder"), format_(format), codec_(format),
          packetShape_(s7::ArrayDescriptor::mjpeg_packet(capacity, {format.width, format.height})),
          pool_(packetShape_, executionSlots, observationSlots, poolBudget(packetShape_)),
          output_(register_array_output(s7::port_names::out, packetShape_))
    {
        register_array_input(s7::port_names::in, frameShape(format_));
    }

    void step() override
    {
        const auto input = array_input(0).share();
        if (!input.image || input.image->pixel_format != format_.pixels)
            throw std::invalid_argument("VideoEncoder input requires matching image format metadata.");
        auto writer = pool_.try_acquire();
        if (!writer) throw std::runtime_error("VideoEncoder packet pool exhausted.");
        const auto destination = writer->bytes();
        const auto size = codec_.encode(input.lease.view().bytes(), destination);
        std::ranges::fill(destination.subspan(size), std::byte{0});
        static_cast<void>(output_.exchange({std::move(*writer).seal(), input.origin, {}, {}, {}, size, input.media_timing}));
    }

private:
    const FrameFormat format_;
    const MjpegCodec codec_;
    const s7::ArrayDescriptor packetShape_;
    s7::ArrayPool pool_;
    s7::ArrayOutputSlot& output_;
};

class VideoDecoder final : public s7::ArrayElement {
public:
    VideoDecoder(FrameFormat format, std::size_t capacity)
        : ArrayElement("VideoDecoder"), format_(format), codec_(format), frameShape_(frameShape(format)),
          pool_(frameShape_, executionSlots, observationSlots, poolBudget(frameShape_)),
          output_(register_array_output(s7::port_names::out, frameShape_))
    {
        register_array_input(s7::port_names::in, s7::ArrayDescriptor::mjpeg_packet(capacity, {format.width, format.height}));
    }

    void step() override
    {
        const auto input = array_input(0).share();
        auto writer = pool_.try_acquire();
        if (!writer) throw std::runtime_error("VideoDecoder frame pool exhausted.");
        codec_.decode(input.encoded_bytes(), writer->bytes());
        static_cast<void>(output_.exchange({std::move(*writer).seal(), input.origin, {}, {},
                                           s7::ArrayImage{format_.pixels}, {}, input.media_timing}));
    }

private:
    const FrameFormat format_;
    const MjpegCodec codec_;
    const s7::ArrayDescriptor frameShape_;
    s7::ArrayPool pool_;
    s7::ArrayOutputSlot& output_;
};

class MjpegFileSource final : public s7::ArrayElement {
public:
    MjpegFileSource(std::filesystem::path path, s7::MjpegPacketFormat format, std::size_t capacity,
                    s7::Rate rate, std::uint64_t sourceId)
        : ArrayElement("MjpegFileSource", true), path_(std::move(path)), format_(format), rate_(rate),
          sourceId_(sourceId), shape_(s7::ArrayDescriptor::mjpeg_packet(capacity, format)),
          pool_(shape_, executionSlots, observationSlots, poolBudget(shape_)),
          output_(register_array_output(s7::port_names::out, shape_))
    {
        if (path_.empty()) throw std::invalid_argument("MjpegFileSource requires a file path.");
        register_output(1, "eof", s7::SignalType::U8, &eof_);
    }

    std::optional<s7::Rate> declared_rate() const override { return rate_; }
    void on_step_failure() noexcept override { if (input_.is_open()) faulted_ = true; }

    void step() override
    {
        if (faulted_) throw std::runtime_error("MjpegFileSource must be reopened after a failed domain step.");
        if (!input_.is_open()) {
            input_.open(path_, std::ios::binary);
            if (!input_) throw std::runtime_error("Could not open the MJPEG file.");
        }
        if (atEnd()) throw std::runtime_error("End of MJPEG file. The last complete frame has already been published.");
        if (position_ == std::numeric_limits<std::int64_t>::max())
            throw std::overflow_error("MJPEG frame index overflow.");
        auto writer = pool_.try_acquire();
        if (!writer) throw std::runtime_error("MjpegFileSource packet pool exhausted.");
        const auto storage = writer->bytes();
        std::size_t used{};
        const auto get = [&] {
            if (used == storage.size()) throw std::length_error("MJPEG file packet exceeds the configured capacity.");
            const auto byte = input_.get();
            if (byte != std::char_traits<char>::eof()) storage[used++] = static_cast<std::byte>(byte);
            return byte;
        };
        if (!s7video::detail::readJpegImage(get, format_))
            throw std::runtime_error("Truncated or invalid MJPEG image, or image dimensions differ from the file source.");
        const bool last = atEnd();
        std::ranges::fill(storage.subspan(used), std::byte{0});
        const s7::ArraySourceOccurrence origin{sourceId_, 0, static_cast<std::uint64_t>(position_) + 1};
        const s7::ArrayMediaTiming timing{s7::MediaPresentationTime{position_, rate_}, false};
        static_cast<void>(output_.exchange({std::move(*writer).seal(), origin, {}, {}, {}, used, timing}));
        eof_ = static_cast<std::uint8_t>(last);
        ++position_;
    }

private:
    bool atEnd()
    {
        const bool end = input_.peek() == std::char_traits<char>::eof();
        if (input_.bad() || (end && !input_.eof())) throw std::runtime_error("Could not read the MJPEG file.");
        return end;
    }

    const std::filesystem::path path_;
    const s7::MjpegPacketFormat format_;
    const s7::Rate rate_;
    const std::uint64_t sourceId_;
    const s7::ArrayDescriptor shape_;
    s7::ArrayPool pool_;
    s7::ArrayOutputSlot& output_;
    std::ifstream input_;
    std::int64_t position_{};
    std::uint8_t eof_{};
    bool faulted_{};
};

FrameFormat formatFrom(const s7::ElementCreationContext& context)
{
    return {static_cast<int>(context.get_int("width")), static_cast<int>(context.get_int("height")),
            context.get_string("format") == "gray8" ? s7::ImagePixelFormat::Gray8 : s7::ImagePixelFormat::RGB8};
}

const S7PluginInfo pluginInfo{S7_PLUGIN_API_VERSION, 3, "s7video", "0.1.0", "S7 Project"};
constexpr char manifest[] = R"json({
  "plugin_name": "s7video",
  "plugin_version": "0.1.0",
  "author": "S7 Project",
  "description": "Headless MJPEG file replay, encoder and decoder elements.",
  "element_count": 3,
  "elements": ["VideoEncoder", "VideoDecoder", "MjpegFileSource"]
})json";

} // namespace

extern "C" {

const S7PluginInfo* s7_plugin_query() { return &pluginInfo; }
const char* s7_plugin_manifest_json() { return manifest; }

void s7_register_elements(void* registryPtr)
{
    auto& registry = *static_cast<s7::ElementRegistry*>(registryPtr);
    static const s7::ElementMetadataLibrary metadata(s7::resources::video_metadata);
    if (!registry.find("VideoEncoder")) {
        registry.register_element(s7::ElementDescriptor("VideoEncoder", [](const s7::ElementCreationContext& context) {
            return std::make_unique<VideoEncoder>(formatFrom(context), static_cast<std::size_t>(context.get_int("packet_capacity")));
        }, metadata.at("VideoEncoder"), true));
    }
    if (!registry.find("VideoDecoder")) {
        registry.register_element(s7::ElementDescriptor("VideoDecoder", [](const s7::ElementCreationContext& context) {
            return std::make_unique<VideoDecoder>(formatFrom(context), static_cast<std::size_t>(context.get_int("packet_capacity")));
        }, metadata.at("VideoDecoder"), true));
    }
    if (!registry.find("MjpegFileSource")) {
        registry.register_element(s7::ElementDescriptor("MjpegFileSource", [](const s7::ElementCreationContext& context) {
            const auto path = context.get_string("file");
            const auto file = std::filesystem::path(std::u8string(path.begin(), path.end()));
            return std::make_unique<MjpegFileSource>(file,
                s7::MjpegPacketFormat{static_cast<int>(context.get_int("width")), static_cast<int>(context.get_int("height"))},
                static_cast<std::size_t>(context.get_int("packet_capacity")), s7::Rate::from_hz(context.get_real("frame_rate")),
                static_cast<std::uint64_t>(context.get_int("source_id")));
        }, metadata.at("MjpegFileSource"), true));
    }
}

} // extern "C"
