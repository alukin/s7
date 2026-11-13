// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-S7-plugin-permission
#include "surface_grid.hpp"
#include <cmath>
#include <limits>
#include <stdexcept>

namespace s7studio::analysis {
namespace {
std::size_t integer(double value, std::size_t low, std::size_t high) {
    if (!std::isfinite(value) || value < double(low) || value > double(high) || std::floor(value) != value)
        throw std::invalid_argument("Surface cell index/dimensions must be bounded exact integers.");
    return static_cast<std::size_t>(value);
}
void coordinate(std::vector<double> &axis, std::size_t index, double value) {
    if (!std::isfinite(value))
        throw std::invalid_argument("Surface coordinates must be finite.");
    if (std::isfinite(axis[index]) && axis[index] != value)
        throw std::invalid_argument("Surface coordinate changed for an existing row/column.");
    if ((index && std::isfinite(axis[index - 1]) && !(axis[index - 1] < value)) ||
        (index + 1 < axis.size() && std::isfinite(axis[index + 1]) && !(value < axis[index + 1])))
        throw std::invalid_argument("Surface axes must be strictly increasing.");
    axis[index] = value;
}
} // namespace
bool SurfaceGrid::append(const std::array<double, 8> &s) {
    if (s[5] == 0)
        return false;
    if (s[5] != 1)
        throw std::invalid_argument("Surface active must be zero or one.");
    const auto nx = integer(s[3], 2, maxAxis), ny = integer(s[4], 2, maxAxis);
    const auto index = integer(s[2], 0, nx * ny - 1);
    if (!values.empty() && (columns() != nx || rows() != ny))
        throw std::invalid_argument("Surface dimensions changed during a scan.");
    if (values.empty()) {
        const auto nan = std::numeric_limits<double>::quiet_NaN();
        x.assign(nx, nan);
        y.assign(ny, nan);
        values.assign(nx * ny, {nan, nan});
        present.assign(nx * ny, false);
    }
    if (present[index])
        throw std::invalid_argument("Duplicate surface cell identity.");
    coordinate(x, index % nx, s[0]);
    coordinate(y, index / nx, s[1]);
    values[index] = {s[6], s[7]};
    present[index] = true;
    ++received;
    return true;
}
bool SurfaceGrid::finite(std::size_t index) const {
    return index < values.size() && present[index] && std::isfinite(values[index].real()) &&
           std::isfinite(values[index].imag());
}
} // namespace s7studio::analysis
