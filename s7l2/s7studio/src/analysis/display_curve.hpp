// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-S7-plugin-permission
#pragma once
#include <QList>
#include <QPointF>
#include <algorithm>
#include <array>
namespace s7studio::analysis {
// Retain endpoints and both coordinate extrema in source order. This also
// handles parametric curves (Nyquist/Nichols), whose X need not be monotonic.
inline QList<QPointF> displayCurve(const QList<QPointF> &source, qsizetype budget = 4096) {
    if (source.size() <= budget)
        return source;
    QList<QPointF> result;
    const auto buckets = std::max<qsizetype>(1, budget / 6);
    result.reserve(buckets * 6);
    for (qsizetype bucket = 0; bucket < buckets; ++bucket) {
        const auto first = bucket * source.size() / buckets;
        const auto end = (bucket + 1) * source.size() / buckets;
        std::array<qsizetype, 6> indices{first, end - 1, first, first, first, first};
        for (auto i = first + 1; i < end; ++i) {
            if (source[i].x() < source[indices[2]].x())
                indices[2] = i;
            if (source[i].x() > source[indices[3]].x())
                indices[3] = i;
            if (source[i].y() < source[indices[4]].y())
                indices[4] = i;
            if (source[i].y() > source[indices[5]].y())
                indices[5] = i;
        }
        std::sort(indices.begin(), indices.end());
        const auto uniqueEnd = std::unique(indices.begin(), indices.end());
        for (auto i = indices.begin(); i != uniqueEnd; ++i)
            result.append(source[*i]);
    }
    return result;
}
} // namespace s7studio::analysis
