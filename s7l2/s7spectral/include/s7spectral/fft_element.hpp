#ifndef S7SPECTRAL_INCLUDE_S7SPECTRAL_FFT_ELEMENT_HPP
#define S7SPECTRAL_INCLUDE_S7SPECTRAL_FFT_ELEMENT_HPP

#include "s7/element.hpp"
#include "spectral_window.hpp"

#include <fftw3.h>

#include <cstddef>
#include <cstdint>
#include <deque>

namespace s7spectral {

// Sliding windowed real FFT over a scalar input. Every hop_size samples (once
// fft_size samples are buffered) it transforms the latest window and updates
// two scalar outputs: the dominant frequency (with parabolic interpolation
// between bins) and the amplitude of that component. Frame-valued outputs
// (the full spectrum) wait for block signal types in the runtime.
class WindowedFFT final : public s7::Element {
public:
    WindowedFFT(std::uint32_t fft_size,
                std::uint32_t hop_size,
                double sample_rate_hz,
                WindowType window_type = WindowType::HANN);
    ~WindowedFFT() override;

    WindowedFFT(const WindowedFFT&) = delete;
    WindowedFFT& operator=(const WindowedFFT&) = delete;

    void step() override;

private:
    void compute_fft();

    std::uint32_t fft_size_;
    std::uint32_t hop_size_;
    double sample_rate_hz_;
    SpectralWindow window_;
    double window_sum_;

    std::deque<float> history_;
    std::uint32_t samples_since_frame_{0};

    // FFTW buffers are allocated by FFTW so they meet its alignment.
    float* fft_in_{nullptr};
    fftwf_complex* fft_out_{nullptr};
    fftwf_plan plan_{nullptr};

    double peak_frequency_hz_{0.0};
    double peak_amplitude_{0.0};
};

} // namespace s7spectral

#endif // S7SPECTRAL_INCLUDE_S7SPECTRAL_FFT_ELEMENT_HPP
