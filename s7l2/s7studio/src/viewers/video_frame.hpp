// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-S7-plugin-permission

#pragma once

#include <s7/runtime.hpp>

#include <QImage>

namespace s7studio::viewers {

struct VideoFrame {
    QImage image;
    s7::ImagePixelFormat format;
    std::uint64_t domainStep;
    std::optional<s7::ArraySourceOccurrence> origin;
    std::optional<s7::ArrayMediaTiming> timing;
    s7::Rate domainRate;
    std::size_t sourceStride;
};

// Copies the active bytes once into an owned image with Qt's required row alignment.
[[nodiscard]] VideoFrame makeVideoFrame(const s7::CommittedArraySample& sample);
[[nodiscard]] QRect fitVideoFrame(QSize image, QRect viewport);

} // namespace s7studio::viewers
