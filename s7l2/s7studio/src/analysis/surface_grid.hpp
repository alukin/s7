// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-S7-plugin-permission
#pragma once
#include <array>
#include <complex>
#include <cstddef>
#include <vector>

namespace s7studio::analysis {
// Immutable to renderers once published. Index/dimensions are explicit source
// values, not inferred from coordinates or delivery order. NaN values are holes.
class SurfaceGrid {
  public:
    static constexpr std::size_t maxAxis = 513;
    // x,y,index,columns,rows,active,re,im (im=0 for real surfaces).
    bool append(const std::array<double, 8> &sample);
    void clear() { *this = SurfaceGrid{}; }
    std::size_t columns() const { return x.size(); }
    std::size_t rows() const { return y.size(); }
    bool complete() const { return !values.empty() && received == values.size(); }
    bool finite(std::size_t index) const;
    std::vector<double> x, y;
    std::vector<std::complex<double>> values;
    std::vector<bool> present;
    std::size_t received = 0;
};
} // namespace s7studio::analysis
