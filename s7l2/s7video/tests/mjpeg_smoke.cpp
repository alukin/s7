// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-S7-plugin-permission

#include "s7video/mjpeg.hpp"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace {
using s7video::FrameFormat;
using s7video::MjpegCodec;
using s7::ImagePixelFormat;

void require(bool condition, std::string_view message)
{
    if (!condition) throw std::runtime_error(std::string(message));
}

template<class Error, class Operation>
void rejects(Operation operation, std::string_view message)
{
    try { operation(); }
    catch (const Error&) { return; }
    throw std::runtime_error(std::string(message));
}

void checkRoundTrip(const FrameFormat format, const int channels)
{
    const MjpegCodec codec(format);
    std::vector<std::byte> input(codec.frame_bytes());
    for (int y = 0; y < format.height; ++y)
        for (int x = 0; x < format.width; ++x)
            for (int c = 0; c < channels; ++c)
                input[static_cast<std::size_t>((y * format.width + x) * channels + c)] =
                    static_cast<std::byte>(24 + x + 2 * y + 24 * c);
    const auto original = input;
    constexpr std::byte sentinel{0xa5};
    std::vector<std::byte> packet(65536, sentinel);
    const auto size = codec.encode(input, packet);
    require(size > 4 && input == original && packet.at(size) == sentinel, "encoder changed input or packet tail");
    const auto jpeg = std::span(packet).first(size);
    require(jpeg[0] == std::byte{0xff} && jpeg[1] == std::byte{0xd8} &&
            jpeg[size - 2] == std::byte{0xff} && jpeg.back() == std::byte{0xd9}, "encoded packet has wrong JPEG boundaries");
    std::vector<std::byte> decoded(input.size(), sentinel);
    const auto rejectPacket = [&](std::span<const std::byte> bytes) {
        rejects<std::invalid_argument>([&] { codec.decode(bytes, decoded); }, "malformed or bundled images accepted");
        require(std::ranges::all_of(decoded, [=](std::byte pixel) { return pixel == sentinel; }), "rejected packet changed output");
    };
    rejectPacket(jpeg.first(size - 1));
    std::vector<std::byte> concatenated(jpeg.begin(), jpeg.end());
    concatenated.insert(concatenated.end(), jpeg.begin(), jpeg.end());
    rejectPacket(concatenated);
    auto malformed = std::vector<std::byte>(jpeg.begin(), jpeg.end());
    malformed[2] = std::byte{0};
    rejectPacket(malformed);
    const MjpegCodec wrongGeometry({format.width + 1, format.height, format.pixels});
    std::vector<std::byte> wrong(wrongGeometry.frame_bytes());
    rejects<std::invalid_argument>([&] { wrongGeometry.decode(jpeg, wrong); }, "wrong JPEG dimensions accepted");

    std::vector<std::byte> tiny(8, sentinel);
    rejects<std::length_error>([&] { static_cast<void>(codec.encode(input, tiny)); }, "packet room limit ignored");
    require(std::ranges::all_of(tiny, [=](std::byte byte) { return byte == sentinel; }), "encoder wrote a partial packet");
    codec.decode(jpeg, decoded);
    int maxError = 0;
    std::size_t errorSum = 0;
    for (std::size_t i = 0; i < input.size(); ++i) {
        const auto error = std::abs(std::to_integer<int>(input[i]) - std::to_integer<int>(decoded[i]));
        maxError = std::max(maxError, error);
        errorSum += static_cast<std::size_t>(error);
    }
    require(maxError <= 6 && errorSum <= 2 * input.size(), "decoded pixels exceed the lossy round-trip tolerance");
    auto next = std::vector<std::byte>(decoded.size());
    codec.decode(jpeg, next);
    require(next == decoded, "repeated independent decode changed the frame");
    const std::array comment{std::byte{0xff}, std::byte{0xfe}, std::byte{0}, std::byte{6},
                             std::byte{0xff}, std::byte{0xd8}, std::byte{0xff}, std::byte{0xd9}};
    auto withComment = std::vector<std::byte>(jpeg.begin(), jpeg.end());
    withComment.insert(withComment.begin() + 2, comment.begin(), comment.end());
    codec.decode(withComment, next);
    require(next == decoded, "JPEG marker-like bytes inside a comment changed packet framing");
    std::cout << channels << " channel(s), " << size << " bytes, max pixel error " << maxError
              << ", mean pixel error " << static_cast<double>(errorSum) / static_cast<double>(input.size()) << '\n';
}
} // namespace

int main() try {
    rejects<std::invalid_argument>([] { const MjpegCodec invalid({4096, 4096, ImagePixelFormat::RGB8}); },
                                   "out-of-budget frame accepted");
    checkRoundTrip({65, 33, ImagePixelFormat::Gray8}, 1);
    checkRoundTrip({65, 33, ImagePixelFormat::RGB8}, 3);
    return 0;
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
}
