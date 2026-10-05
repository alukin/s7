#include "s7spectral/fft_element.hpp"

#include <cmath>
#include <numeric>
#include <stdexcept>

namespace s7spectral {

WindowedFFT::WindowedFFT(const std::uint32_t fft_size,
                         const std::uint32_t hop_size,
                         const double sample_rate_hz,
                         const WindowType window_type)
    : s7::Element("WindowedFFT", true)
    , fft_size_(fft_size)
    , hop_size_(hop_size)
    , sample_rate_hz_(sample_rate_hz)
    , window_(fft_size, window_type)
    , window_sum_(std::accumulate(window_.coefficients().begin(), window_.coefficients().end(), 0.0))
{
    if (fft_size < 4 || (fft_size & (fft_size - 1)) != 0) {
        throw std::invalid_argument("WindowedFFT fft_size must be a power of 2 and at least 4.");
    }
    if (hop_size == 0 || hop_size > fft_size) {
        throw std::invalid_argument("WindowedFFT hop_size must be in [1, fft_size].");
    }
    if (!(sample_rate_hz > 0.0)) {
        throw std::invalid_argument("WindowedFFT sample_rate_hz must be positive.");
    }

    fft_in_ = fftwf_alloc_real(fft_size);
    fft_out_ = fftwf_alloc_complex(fft_size / 2 + 1);
    if (fft_in_ && fft_out_) {
        plan_ = fftwf_plan_dft_r2c_1d(static_cast<int>(fft_size), fft_in_, fft_out_, FFTW_ESTIMATE);
    }
    if (!plan_) {
        fftwf_free(fft_in_);
        fftwf_free(fft_out_);
        throw std::runtime_error("WindowedFFT could not create its FFTW plan.");
    }

    register_input(0, "in", s7::SignalType::DOUBLE, nullptr);
    register_output(0, "peak_frequency_hz", s7::SignalType::DOUBLE, &peak_frequency_hz_);
    register_output(1, "peak_amplitude", s7::SignalType::DOUBLE, &peak_amplitude_);
}

WindowedFFT::~WindowedFFT()
{
    fftwf_destroy_plan(plan_);
    fftwf_free(fft_in_);
    fftwf_free(fft_out_);
}

void WindowedFFT::step()
{
    history_.push_back(static_cast<float>(read_input<double>(0)));
    if (history_.size() > fft_size_) {
        history_.pop_front();
    }
    ++samples_since_frame_;
    if (history_.size() == fft_size_ && samples_since_frame_ >= hop_size_) {
        samples_since_frame_ = 0;
        compute_fft();
    }
}

void WindowedFFT::compute_fft()
{
    std::copy(history_.begin(), history_.end(), fft_in_);
    window_.apply(fft_in_, fft_size_);
    fftwf_execute(plan_);

    const std::size_t bins = fft_size_ / 2 + 1;
    auto magnitude = [this](const std::size_t bin) {
        return std::hypot(static_cast<double>(fft_out_[bin][0]), static_cast<double>(fft_out_[bin][1]));
    };

    // Strongest bin, excluding DC.
    std::size_t peak = 1;
    for (std::size_t bin = 2; bin < bins; ++bin) {
        if (magnitude(bin) > magnitude(peak)) {
            peak = bin;
        }
    }

    // Parabolic interpolation on the log magnitudes of the neighbours.
    double offset = 0.0;
    if (peak + 1 < bins) {
        const double left = std::log(magnitude(peak - 1) + 1e-30);
        const double centre = std::log(magnitude(peak) + 1e-30);
        const double right = std::log(magnitude(peak + 1) + 1e-30);
        const double denominator = left - 2.0 * centre + right;
        if (denominator < 0.0) {
            offset = 0.5 * (left - right) / denominator;
        }
    }

    peak_frequency_hz_ = (static_cast<double>(peak) + offset) * sample_rate_hz_ / fft_size_;
    // A sine of amplitude A gives |X| ~ A * sum(w) / 2 at its bin (less the
    // window's scalloping loss when it falls between bins).
    peak_amplitude_ = 2.0 * magnitude(peak) / window_sum_;
}

} // namespace s7spectral
