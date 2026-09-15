#ifndef S7SPECTRAL_INCLUDE_S7SPECTRAL_FFT_ELEMENT_HPP
#define S7SPECTRAL_INCLUDE_S7SPECTRAL_FFT_ELEMENT_HPP

#include "s7/element.hpp"
#include "s7/spectral_types.hpp"
#include "spectral_window.hpp"

#include <fftw3.h>
#include <vector>
#include <deque>
#include <memory>

namespace s7spectral {

// Real-to-Complex FFT element for spectral analysis
// Performs windowed sliding FFT on input signal
// Outputs SpectralFrame for waterfall visualization
class WindowedFFT : public s7::Element {
public:
    // fft_size: FFT window size (must be power of 2)
    // hop_size: Number of samples between windows (typically fft_size/2 or fft_size/4)
    // sample_rate_hz: Sample rate of input signal
    // window_type: Type of window function to apply
    explicit WindowedFFT(
        uint32_t fft_size,
        uint32_t hop_size,
        float sample_rate_hz,
        s7spectral::WindowType window_type = s7spectral::WindowType::HANN
    );

    ~WindowedFFT() override;

    void step() override;

    // Get latest spectral frame
    [[nodiscard]] const s7::SpectralFrame& get_latest_frame() const {
        return latest_frame_;
    }

    // Check if new frame is ready
    [[nodiscard]] bool has_new_frame() const {
        return frame_ready_;
    }

    // Reset accumulators
    void reset();

private:
    // FFT parameters
    uint32_t fft_size_;
    uint32_t hop_size_;
    float sample_rate_hz_;

    // Input circular buffer (holds overlapping windows)
    std::deque<float> input_buffer_;

    // FFTW3 buffers and plan
    fftwf_complex* fft_out_;
    fftwf_plan fft_plan_;

    // Window function
    SpectralWindow window_;

    // Current spectral frame
    s7::SpectralFrame latest_frame_;
    bool frame_ready_;
    uint64_t frame_counter_;

    // Temporary working buffers
    std::vector<float> fft_input_;

    // Input member
    double input_value_;

    // Compute next FFT frame
    void compute_fft();
};

// Magnitude spectrum (converts complex to real magnitudes)
class MagnitudeSpectrum : public s7::Element {
public:
    explicit MagnitudeSpectrum();

    ~MagnitudeSpectrum() override;

    void step() override;

    void bind_inputs(std::span<const s7::InputBinding> bindings) override;

private:
    // We receive SpectralFrame via type-erased binding
    // Output is another SpectralFrame with magnitude data
    s7::SpectralFrame input_frame_;
    s7::SpectralFrame output_frame_;
};

// Power spectrum in dB (converts magnitudes to dB scale)
class PowerSpectrumDB : public s7::Element {
public:
    explicit PowerSpectrumDB(float reference = 1.0f, float floor_db = -200.0f);

    ~PowerSpectrumDB() override;

    void step() override;

    void bind_inputs(std::span<const s7::InputBinding> bindings) override;

private:
    float reference_;
    float floor_db_;
    s7::SpectralFrame input_frame_;
    s7::SpectralFrame output_frame_;
};

} // namespace s7spectral

#endif // S7SPECTRAL_INCLUDE_S7SPECTRAL_FFT_ELEMENT_HPP

