// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "s7/element.hpp"
#include <array>
#include <cmath>
#include <cstdint>
#include <stdexcept>

namespace s7 {
// Finite row-major coordinate scan. All ports are F64; bounded integer
// identities are represented exactly. X advances fastest. One cell is emitted
// per logical evaluation, independently of wall time or the model sample rate.
// After the last cell, coordinates/identity hold and active becomes zero.
class GridScan final : public Element {
  public:
    GridScan(double x_min = -2, double x_max = 2, double y_min = -2, double y_max = 2,
             std::int64_t columns = 129, std::int64_t rows = 129)
        : Element("GridScan", true), x_min_(x_min), x_max_(x_max), y_min_(y_min), y_max_(y_max) {
        if (columns < 2 || columns > 513 || rows < 2 || rows > 513)
            throw std::invalid_argument("GridScan rows and columns must be in [2, 513].");
        if (!std::isfinite(x_min) || !std::isfinite(x_max) || !std::isfinite(y_min) ||
            !std::isfinite(y_max) || !(x_min < x_max) || !(y_min < y_max) || !std::isfinite(x_max - x_min) ||
            !std::isfinite(y_max - y_min))
            throw std::invalid_argument(
                "GridScan requires finite increasing coordinate bounds and finite spans.");
        columns_ = static_cast<std::uint64_t>(columns);
        rows_ = static_cast<std::uint64_t>(rows);
        values_ = {x_min, y_min, 0, double(columns), double(rows), 0};
        constexpr std::array names{"x", "y", "index", "columns", "rows", "active"};
        for (std::size_t i = 0; i < names.size(); ++i)
            register_output(i, names[i], SignalType::F64, &values_[i]);
    }
    void step() override {
        if (next_ == columns_ * rows_) {
            values_[5] = 0;
            return;
        }
        const auto column = next_ % columns_, row = next_ / columns_;
        // Explicit endpoints avoid rounding residue at the declared boundary.
        values_[0] = column == columns_ - 1
                         ? x_max_
                         : x_min_ + (x_max_ - x_min_) * (double(column) / double(columns_ - 1));
        values_[1] =
            row == rows_ - 1 ? y_max_ : y_min_ + (y_max_ - y_min_) * (double(row) / double(rows_ - 1));
        values_[2] = double(next_++);
        values_[5] = 1;
    }

  private:
    double x_min_, x_max_, y_min_, y_max_;
    std::uint64_t columns_{}, rows_{}, next_{};
    std::array<double, 6> values_{};
};
} // namespace s7
