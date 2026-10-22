// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-S7-plugin-permission
#pragma once
#include <complex>
#include <memory>
#include <span>
#include <vector>

namespace s7studio::analysis {
// Private binary64 adapter. Forward is the unnormalized negative-exponent DFT;
// inverse divides by N. Plans and aligned scratch are reused until N changes.
class Fft {
public:
    Fft();
    ~Fft();
    Fft(const Fft&) = delete;
    Fft& operator=(const Fft&) = delete;
    static void initialize();
    std::vector<std::complex<double>> transform(std::span<const std::complex<double>> input, bool inverse = false);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
