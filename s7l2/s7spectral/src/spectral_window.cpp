// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: MIT-0

#include "s7spectral/spectral_window.hpp"
#include <stdexcept>
#include <numbers>

namespace s7spectral {

SpectralWindow::SpectralWindow(size_t size, WindowType type)
    : normalization_(1.0f), type_(type)
{
    coefficients_.resize(size);

    switch (type) {
        case WindowType::RECTANGULAR:
            compute_rectangular(size);
            break;
        case WindowType::HANN:
            compute_hann(size);
            break;
        case WindowType::HAMMING:
            compute_hamming(size);
            break;
        case WindowType::BLACKMAN:
            compute_blackman(size);
            break;
        case WindowType::BLACKMAN_HARRIS:
            compute_blackman_harris(size);
            break;
        case WindowType::NUTTALL:
            compute_nuttall(size);
            break;
        default:
            throw std::invalid_argument("Unknown window type");
    }
}

void SpectralWindow::apply(float* signal, size_t length) const {
    if (length > coefficients_.size()) {
        throw std::invalid_argument("Signal length exceeds window size");
    }

    for (size_t i = 0; i < length; ++i) {
        signal[i] *= coefficients_[i];
    }
}

void SpectralWindow::compute_rectangular(size_t size) {
    for (size_t i = 0; i < size; ++i) {
        coefficients_[i] = 1.0f;
    }
    normalization_ = 1.0f;
}

void SpectralWindow::compute_hann(size_t size) {
    const float pi = std::numbers::pi_v<float>;
    float sum = 0.0f;

    for (size_t i = 0; i < size; ++i) {
        float w = 0.5f * (1.0f - std::cos(2.0f * pi * i / (size - 1)));
        coefficients_[i] = w;
        sum += w;
    }

    normalization_ = sum / size;
}

void SpectralWindow::compute_hamming(size_t size) {
    const float pi = std::numbers::pi_v<float>;
    const float alpha = 0.54f;
    const float beta = 0.46f;
    float sum = 0.0f;

    for (size_t i = 0; i < size; ++i) {
        float w = alpha - beta * std::cos(2.0f * pi * i / (size - 1));
        coefficients_[i] = w;
        sum += w;
    }

    normalization_ = sum / size;
}

void SpectralWindow::compute_blackman(size_t size) {
    const float pi = std::numbers::pi_v<float>;
    const float a0 = 0.42f;
    const float a1 = 0.5f;
    const float a2 = 0.08f;
    float sum = 0.0f;

    for (size_t i = 0; i < size; ++i) {
        float w = a0
                - a1 * std::cos(2.0f * pi * i / (size - 1))
                + a2 * std::cos(4.0f * pi * i / (size - 1));
        coefficients_[i] = w;
        sum += w;
    }

    normalization_ = sum / size;
}

void SpectralWindow::compute_blackman_harris(size_t size) {
    const float pi = std::numbers::pi_v<float>;
    const float a0 = 0.35875f;
    const float a1 = 0.48829f;
    const float a2 = 0.14128f;
    const float a3 = 0.01168f;
    float sum = 0.0f;

    for (size_t i = 0; i < size; ++i) {
        float w = a0
                - a1 * std::cos(2.0f * pi * i / (size - 1))
                + a2 * std::cos(4.0f * pi * i / (size - 1))
                - a3 * std::cos(6.0f * pi * i / (size - 1));
        coefficients_[i] = w;
        sum += w;
    }

    normalization_ = sum / size;
}

void SpectralWindow::compute_nuttall(size_t size) {
    const float pi = std::numbers::pi_v<float>;
    const float a0 = 0.355768f;
    const float a1 = 0.487396f;
    const float a2 = 0.144232f;
    const float a3 = 0.012604f;
    float sum = 0.0f;

    for (size_t i = 0; i < size; ++i) {
        float w = a0
                - a1 * std::cos(2.0f * pi * i / (size - 1))
                + a2 * std::cos(4.0f * pi * i / (size - 1))
                - a3 * std::cos(6.0f * pi * i / (size - 1));
        coefficients_[i] = w;
        sum += w;
    }

    normalization_ = sum / size;
}

float SpectralWindow::scallop_loss_db() const {
    // Peak amplitude at frequency halfway between bin centers
    // Varies by window type
    switch (type_) {
        case WindowType::RECTANGULAR:
            return -3.99f;  // dB
        case WindowType::HANN:
            return -1.42f;
        case WindowType::HAMMING:
            return -1.30f;
        case WindowType::BLACKMAN:
            return -1.10f;
        case WindowType::BLACKMAN_HARRIS:
            return -0.77f;
        case WindowType::NUTTALL:
            return -0.80f;
        default:
            return -1.0f;
    }
}

float SpectralWindow::coherent_gain() const {
    // Peak amplitude for constant input
    // Normalized by window normalization
    return 1.0f / normalization_;
}

} // namespace s7spectral

