// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-S7-plugin-permission
#include "analysis/fft.hpp"
#include <fftw3.h>
#include <mutex>
#include <stdexcept>

namespace s7studio::analysis {
namespace {
std::mutex& plannerMutex() { static std::mutex mutex; return mutex; }
}
struct Fft::Impl {
    std::size_t size = 0;
    fftw_complex *input = nullptr, *output = nullptr;
    fftw_plan forward = nullptr, backward = nullptr;
    ~Impl() {
        std::lock_guard lock(plannerMutex());
        if (forward) fftw_destroy_plan(forward);
        if (backward) fftw_destroy_plan(backward);
        fftw_free(input); fftw_free(output);
    }
};
Fft::Fft() = default;
Fft::~Fft() = default;
void Fft::initialize() { (void)plannerMutex(); }
std::vector<std::complex<double>> Fft::transform(std::span<const std::complex<double>> input, bool inverse)
{
    if (input.empty() || input.size() > 131072)
        throw std::invalid_argument("FFT size must be no larger than 131072.");
    if (!impl_ || impl_->size != input.size()) {
        auto next = std::make_unique<Impl>();
        next->size = input.size();
        {
            std::lock_guard lock(plannerMutex());
            next->input = fftw_alloc_complex(next->size);
            next->output = fftw_alloc_complex(next->size);
            if (next->input && next->output) {
                next->forward = fftw_plan_dft_1d(static_cast<int>(next->size), next->input, next->output, FFTW_FORWARD, FFTW_ESTIMATE);
                next->backward = fftw_plan_dft_1d(static_cast<int>(next->size), next->input, next->output, FFTW_BACKWARD, FFTW_ESTIMATE);
            }
        }
        if (!next->input || !next->output || !next->forward || !next->backward)
            throw std::runtime_error("FFTW could not allocate scratch or create a plan.");
        impl_ = std::move(next);
    }
    for (std::size_t i = 0; i < input.size(); ++i) {
        impl_->input[i][0] = input[i].real(); impl_->input[i][1] = input[i].imag();
    }
    fftw_execute(inverse ? impl_->backward : impl_->forward);
    std::vector<std::complex<double>> result(input.size());
    const double divisor = inverse ? static_cast<double>(input.size()) : 1.;
    for (std::size_t i = 0; i < input.size(); ++i)
        result[i] = {impl_->output[i][0]/divisor, impl_->output[i][1]/divisor};
    return result;
}
}
