// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-S7-plugin-permission

#include "s7video/mjpeg.hpp"
#include "jpeg_packet.hpp"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/error.h>
#include <libswscale/swscale.h>
}

#include <array>
#include <cstring>
#include <memory>
#include <stdexcept>

namespace s7video {
namespace {

template<class T, void (*Free)(T**)>
struct AvFree {
    void operator()(T* pointer) const { Free(&pointer); }
};

using Codec = std::unique_ptr<AVCodecContext, AvFree<AVCodecContext, avcodec_free_context>>;
using Frame = std::unique_ptr<AVFrame, AvFree<AVFrame, av_frame_free>>;
using Packet = std::unique_ptr<AVPacket, AvFree<AVPacket, av_packet_free>>;

constexpr int maxDimension = s7::MjpegPacketFormat::max_dimension;
constexpr std::int64_t maxPixels = s7::MjpegPacketFormat::max_pixels;

void checked(int result, const char* operation)
{
    if (result >= 0) return;
    std::array<char, AV_ERROR_MAX_STRING_SIZE> message{};
    av_strerror(result, message.data(), message.size());
    throw std::runtime_error(std::string(operation) + " failed: " + message.data());
}

Codec makeCodec(const AVCodec* implementation)
{
    if (!implementation) throw std::runtime_error("Linked FFmpeg has no requested MJPEG codec.");
    Codec codec(avcodec_alloc_context3(implementation));
    if (!codec) throw std::bad_alloc();
    codec->thread_count = 1;
    return codec;
}

Frame makeFrame(FrameFormat geometry, AVPixelFormat pixelFormat)
{
    Frame frame(av_frame_alloc());
    if (!frame) throw std::bad_alloc();
    frame->format = pixelFormat;
    frame->width = geometry.width;
    frame->height = geometry.height;
    frame->color_range = AVCOL_RANGE_JPEG;
    frame->colorspace = AVCOL_SPC_SMPTE170M;
    checked(av_frame_get_buffer(frame.get(), 0), "Allocating an image");
    return frame;
}

Packet makePacket()
{
    Packet packet(av_packet_alloc());
    if (!packet) throw std::bad_alloc();
    return packet;
}

AVPixelFormat rawFormat(FrameFormat geometry)
{
    return geometry.pixels == s7::ImagePixelFormat::Gray8 ? AV_PIX_FMT_GRAY8 : AV_PIX_FMT_RGB24;
}

void normalizeJpegRange(AVFrame& frame)
{
    // Legacy JPEG pixel formats have the same plane layout as their regular
    // YUV equivalents, with full range encoded in the format identifier.
    switch (frame.format) {
    case AV_PIX_FMT_YUVJ420P: frame.format = AV_PIX_FMT_YUV420P; break;
    case AV_PIX_FMT_YUVJ422P: frame.format = AV_PIX_FMT_YUV422P; break;
    case AV_PIX_FMT_YUVJ444P: frame.format = AV_PIX_FMT_YUV444P; break;
    case AV_PIX_FMT_YUVJ440P: frame.format = AV_PIX_FMT_YUV440P; break;
    case AV_PIX_FMT_YUVJ411P: frame.format = AV_PIX_FMT_YUV411P; break;
    default: return;
    }
    frame.color_range = AVCOL_RANGE_JPEG;
}

void convert(const AVFrame& source, AVFrame& destination)
{
    std::unique_ptr<SwsContext, decltype(&sws_freeContext)> scaler(sws_alloc_context(), sws_freeContext);
    if (!scaler) throw std::bad_alloc();
    checked(sws_scale_frame(scaler.get(), &destination, &source), "Converting video pixels");
}

bool completeImage(std::span<const std::byte> packet, FrameFormat format)
{
    if (packet.size() > MjpegCodec::maxPacketBytes) return false;
    std::size_t position{};
    const auto get = [&] { return position < packet.size() ? std::to_integer<int>(packet[position++]) : -1; };
    return detail::readJpegImage(get, {format.width, format.height}) && position == packet.size();
}

} // namespace

MjpegCodec::MjpegCodec(FrameFormat format) : format_(format)
{
    if (format.width < 1 || format.height < 1 || format.width > maxDimension || format.height > maxDimension ||
        std::int64_t{format.width} * format.height > maxPixels)
        throw std::invalid_argument("MJPEG dimensions exceed the supported bounds.");
    if (format.pixels != s7::ImagePixelFormat::Gray8 && format.pixels != s7::ImagePixelFormat::RGB8)
        throw std::invalid_argument("MJPEG requires compact Gray8 or RGB8 frames.");
}

std::size_t MjpegCodec::frame_bytes() const noexcept
{
    return static_cast<std::size_t>(format_.width) * static_cast<std::size_t>(format_.height)
        * (format_.pixels == s7::ImagePixelFormat::Gray8 ? 1U : 3U);
}

std::size_t MjpegCodec::encode(std::span<const std::byte> frame, std::span<std::byte> output) const
{
    if (frame.size() != frame_bytes() || output.empty() || output.size() > maxPacketBytes)
        throw std::invalid_argument("MJPEG frame or packet buffer has an invalid size.");

    const auto* implementation = avcodec_find_encoder(AV_CODEC_ID_MJPEG);
    auto codec = makeCodec(implementation);
    codec->width = format_.width;
    codec->height = format_.height;
    codec->pix_fmt = AV_PIX_FMT_YUV444P;
    codec->color_range = AVCOL_RANGE_JPEG;
    codec->colorspace = AVCOL_SPC_SMPTE170M;
    codec->time_base = {1, 1};
    codec->flags |= AV_CODEC_FLAG_QSCALE;
    codec->global_quality = 2 * FF_QP2LAMBDA;
    checked(avcodec_open2(codec.get(), implementation, nullptr), "Opening the MJPEG encoder");

    auto raw = makeFrame(format_, rawFormat(format_));
    const auto stride = frame_bytes() / static_cast<std::size_t>(format_.height);
    const auto rawStride = static_cast<std::size_t>(raw->linesize[0]);
    for (std::size_t y = 0; y < static_cast<std::size_t>(format_.height); ++y) {
        auto* line = raw->data[0] + y * rawStride;
        std::memcpy(line, frame.data() + y * stride, stride);
        std::memset(line + stride, 0, rawStride - stride);
    }
    auto converted = makeFrame(format_, codec->pix_fmt);
    convert(*raw, *converted);
    converted->pts = 0;
    converted->quality = codec->global_quality;
    checked(avcodec_send_frame(codec.get(), converted.get()), "Sending a raw frame");
    auto packet = makePacket();
    checked(avcodec_receive_packet(codec.get(), packet.get()), "Receiving the encoded packet");
    const auto size = static_cast<std::size_t>(packet->size);
    if (size > output.size()) throw std::length_error("MJPEG packet exceeds the output capacity.");
    if (!completeImage(std::as_bytes(std::span(packet->data, size)), format_))
        throw std::runtime_error("MJPEG encoder did not produce a complete independent image.");
    auto extra = makePacket();
    if (avcodec_receive_packet(codec.get(), extra.get()) != AVERROR(EAGAIN))
        throw std::runtime_error("Expected exactly one encoded packet per image.");
    std::memcpy(output.data(), packet->data, size);
    return size;
}

void MjpegCodec::decode(std::span<const std::byte> bytes, std::span<std::byte> frame) const
{
    if (frame.size() != frame_bytes() || !completeImage(bytes, format_))
        throw std::invalid_argument("Expected one complete MJPEG image with the configured geometry.");

    const auto* implementation = avcodec_find_decoder(AV_CODEC_ID_MJPEG);
    auto codec = makeCodec(implementation);
    codec->max_pixels = 2 * maxPixels; // FFmpeg checks aligned decoder dimensions as well.
    codec->err_recognition = AV_EF_EXPLODE;
    checked(avcodec_open2(codec.get(), implementation, nullptr), "Opening the MJPEG decoder");
    auto packet = makePacket();
    checked(av_new_packet(packet.get(), static_cast<int>(bytes.size())), "Allocating an MJPEG packet");
    std::memcpy(packet->data, bytes.data(), bytes.size());
    checked(avcodec_send_packet(codec.get(), packet.get()), "Sending an MJPEG packet");
    Frame decoded(av_frame_alloc());
    Frame extra(av_frame_alloc());
    if (!decoded || !extra) throw std::bad_alloc();
    checked(avcodec_receive_frame(codec.get(), decoded.get()), "Decoding an MJPEG image");
    if (decoded->width != format_.width || decoded->height != format_.height ||
        (decoded->flags & AV_FRAME_FLAG_CORRUPT) || decoded->decode_error_flags ||
        avcodec_receive_frame(codec.get(), extra.get()) != AVERROR(EAGAIN))
        throw std::runtime_error("MJPEG did not decode to exactly one intact image with the configured geometry.");

    auto raw = makeFrame(format_, rawFormat(format_));
    normalizeJpegRange(*decoded);
    convert(*decoded, *raw);
    const auto stride = frame_bytes() / static_cast<std::size_t>(format_.height);
    for (std::size_t y = 0; y < static_cast<std::size_t>(format_.height); ++y)
        std::memcpy(frame.data() + y * stride, raw->data[0] + y * static_cast<std::size_t>(raw->linesize[0]), stride);
}

} // namespace s7video
