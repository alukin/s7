// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-S7-plugin-permission

#include "viewers/video_frame.hpp"

#include <cstring>
#include <stdexcept>

namespace s7studio::viewers {

VideoFrame makeVideoFrame(const s7::CommittedArraySample& sample)
{
    if (!sample.value || !sample.value.image)
        throw std::invalid_argument("The committed frame has no pixel format metadata.");
    const auto view = sample.value.lease.view();
    const auto& shape = view.descriptor();
    const auto extents = shape.extents();
    const auto pixels = sample.value.image->pixel_format;
    QImage::Format format = QImage::Format_Invalid;
    if (pixels == s7::ImagePixelFormat::Gray8 && extents.size() == 2) format = QImage::Format_Grayscale8;
    if (pixels == s7::ImagePixelFormat::RGB8 && extents.size() == 3 && extents[2] == 3) format = QImage::Format_RGB888;
    if (format == QImage::Format_Invalid || shape.type() != s7::SignalType::U8)
        throw std::invalid_argument("Video Player requires a compact U8 Gray8 or RGB8 frame.");
    if (extents[0] > 8192 || extents[1] > 8192)
        throw std::invalid_argument("Video Player accepts frame dimensions up to 8192 pixels.");
    const auto stride = view.stride_bytes(0);
    const auto paddedStride = (stride + 3) & ~std::size_t{3};
    if (paddedStride * extents[0] > (32U << 20))
        throw std::invalid_argument("Video Player frame exceeds its 32 MiB image budget.");

    QImage image(static_cast<int>(extents[1]), static_cast<int>(extents[0]), format);
    if (image.isNull()) throw std::runtime_error("Could not allocate the video image.");
    for (int y = 0; y < image.height(); ++y) {
        const auto row = view.bytes().subspan(static_cast<std::size_t>(y) * stride, stride);
        auto* line = image.scanLine(y);
        std::memcpy(line, row.data(), row.size());
        std::memset(line + row.size(), 0, static_cast<std::size_t>(image.bytesPerLine()) - row.size());
    }
    return {std::move(image), pixels, sample.domain_step, sample.value.origin};
}

QRect fitVideoFrame(QSize image, QRect viewport)
{
    if (image.isEmpty() || viewport.isEmpty()) return {};
    auto fitted = image.scaled(viewport.size(), Qt::KeepAspectRatio);
    fitted = fitted.expandedTo(QSize(1, 1));
    return {QPoint(viewport.x() + (viewport.width() - fitted.width()) / 2,
                   viewport.y() + (viewport.height() - fitted.height()) / 2), fitted};
}

} // namespace s7studio::viewers
