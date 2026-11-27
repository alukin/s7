// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-S7-plugin-permission

#pragma once

#include <s7/array_descriptor.hpp>

namespace s7video::detail {

// Input yields a byte, or -1 at the end of storage. Stop at the structural
// EOI, skipping embedded marker bytes in segments and stuffed scan bytes.
template<class Get>
bool readJpegImage(Get get, s7::MjpegPacketFormat format)
{
    if (get() != 0xff || get() != 0xd8) return false;
    bool header = false;
    bool sawScan = false;
    bool inScan = false;
    for (;;) {
        const auto byte = get();
        if (byte < 0) return false;
        if (byte != 0xff) {
            if (!inScan) return false;
            continue;
        }
        int marker;
        do { marker = get(); } while (marker == 0xff);
        if (inScan && (marker == 0 || (marker >= 0xd0 && marker <= 0xd7))) continue;
        inScan = false;
        if (marker == 0xd9) return sawScan;
        if (marker < 0xc0 || (marker >= 0xd0 && marker <= 0xd8)) return false;
        const auto high = get();
        const auto low = get();
        if (high < 0 || low < 0) return false;
        auto remaining = 256 * high + low - 2;
        if (remaining < 0) return false;
        const bool frameHeader = marker <= 0xcf && marker != 0xc4 && marker != 0xc8 && marker != 0xcc;
        if (frameHeader) {
            if (header || marker > 0xc2 || remaining < 6 || get() != 8) return false;
            const auto heightHigh = get();
            const auto heightLow = get();
            const auto widthHigh = get();
            const auto widthLow = get();
            if (heightHigh < 0 || heightLow < 0 || widthHigh < 0 || widthLow < 0 ||
                256 * heightHigh + heightLow != format.height || 256 * widthHigh + widthLow != format.width)
                return false;
            remaining -= 5;
            header = true;
        }
        if (marker == 0xda) {
            if (!header) return false;
            sawScan = inScan = true;
        }
        for (int index = 0; index < remaining; ++index)
            if (get() < 0) return false;
    }
}

} // namespace s7video::detail
