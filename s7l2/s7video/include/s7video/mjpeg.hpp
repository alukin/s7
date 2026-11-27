// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-S7-plugin-permission

#pragma once

#include <s7/array_value.hpp>

#include <cstddef>
#include <span>

namespace s7video {

struct FrameFormat {
    int width;
    int height;
    s7::ImagePixelFormat pixels;
};

// Each operation uses fresh codec state. Input and output spans describe one
// complete image or packet. Output is written only after successful validation
// and conversion. The tail of the encoded output span is left to the caller.
class MjpegCodec {
public:
    static constexpr std::size_t maxPacketBytes = 32U << 20;

    explicit MjpegCodec(FrameFormat format);
    [[nodiscard]] std::size_t frame_bytes() const noexcept;
    [[nodiscard]] std::size_t encode(std::span<const std::byte> frame, std::span<std::byte> output) const;
    void decode(std::span<const std::byte> packet, std::span<std::byte> frame) const;

private:
    const FrameFormat format_;
};

} // namespace s7video
