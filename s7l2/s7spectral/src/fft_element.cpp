#include "s7spectral/fft_element.hpp"
#include <stdexcept>
#include <cstring>
#include <chrono>

namespace s7spectral {

WindowedFFT::WindowedFFT(
    uint32_t fft_size,
    uint32_t hop_size,
    float sample_rate_hz,
    WindowType window_type
)
    : s7::Element("WindowedFFT", true),
      fft_size_(fft_size),
      hop_size_(hop_size),
      sample_rate_hz_(sample_rate_hz),
      window_(fft_size, window_type),
      frame_ready_(false),
      frame_counter_(0),
      input_value_(0.0)
{
    // Validate parameters
    if (fft_size == 0 || (fft_size & (fft_size - 1)) != 0) {
        throw std::invalid_argument("FFT size must be power of 2");
    }
    if (hop_size == 0 || hop_size > fft_size) {
        throw std::invalid_argument("hop_size must be > 0 and <= fft_size");
    }
    if (sample_rate_hz <= 0.0f) {
        throw std::invalid_argument("sample_rate_hz must be positive");
    }

    // Allocate FFTW buffers
    fft_out_ = fftwf_alloc_complex(fft_size);
    if (!fft_out_) {
        throw std::runtime_error("Failed to allocate FFTW output buffer");
    }

    // Create FFTW plan for real-to-complex FFT
    std::vector<float> dummy_input(fft_size);
    fft_plan_ = fftwf_plan_dft_r2c_1d(
        fft_size,
        dummy_input.data(),
        fft_out_,
        FFTW_MEASURE
    );

    if (!fft_plan_) {
        fftwf_free(fft_out_);
        throw std::runtime_error("Failed to create FFTW plan");
    }

    // Allocate working buffer
    fft_input_.resize(fft_size, 0.0f);

    // Initialize spectral frame
    latest_frame_.bins.resize(fft_size / 2 + 1);
    latest_frame_.metadata.fft_size = fft_size;
    latest_frame_.metadata.sample_rate_hz = sample_rate_hz;
    latest_frame_.metadata.hop_size = hop_size;
    latest_frame_.metadata.window_type =
        (s7::SpectralFrame::Metadata::WindowType)window_type;
    latest_frame_.metadata.freq_resolution_hz =
        sample_rate_hz / fft_size;
    latest_frame_.metadata.nyquist_hz = sample_rate_hz / 2.0f;

    // Register ports
    register_input(0, "in", s7::SignalType::DOUBLE, nullptr);
}

WindowedFFT::~WindowedFFT() {
    if (fft_plan_) {
        fftwf_destroy_plan(fft_plan_);
    }
    if (fft_out_) {
        fftwf_free(fft_out_);
    }
}

void WindowedFFT::step() {
    frame_ready_ = false;

    // Read input sample
    input_value_ = read_input<double>(0);

    // Add to circular buffer
    input_buffer_.push_back(static_cast<float>(input_value_));

    // Keep buffer at max size
    if (input_buffer_.size() > fft_size_) {
        input_buffer_.pop_front();
    }

    // Check if we have enough samples for a new frame
    if (input_buffer_.size() < fft_size_) {
        return;
    }

    // We have a full window - compute FFT
    // Copy window from buffer, skipping the oldest samples
    size_t start_idx = input_buffer_.size() - fft_size_;
    for (size_t i = 0; i < fft_size_; ++i) {
        fft_input_[i] = input_buffer_[start_idx + i];
    }

    // Apply window function
    window_.apply(fft_input_.data(), fft_size_);

    // Execute FFT
    fftwf_execute_dft_r2c(
        fft_plan_,
        fft_input_.data(),
        fft_out_
    );

    // Copy results to spectral frame
    size_t num_bins = fft_size_ / 2 + 1;
    for (size_t i = 0; i < num_bins; ++i) {
        latest_frame_.bins[i] = std::complex<float>(
            fft_out_[i][0],
            fft_out_[i][1]
        );
    }

    // Update metadata
    latest_frame_.metadata.timestamp = std::chrono::system_clock::now();
    latest_frame_.metadata.frame_number = frame_counter_++;

    frame_ready_ = true;

    // Remove old samples (overlap processing)
    for (uint32_t i = 0; i < hop_size_ && !input_buffer_.empty(); ++i) {
        input_buffer_.pop_front();
    }
}

void WindowedFFT::reset() {
    input_buffer_.clear();
    frame_ready_ = false;
    frame_counter_ = 0;
    std::fill(fft_input_.begin(), fft_input_.end(), 0.0f);
}

// ============================================================================
// MagnitudeSpectrum Implementation
// ============================================================================

MagnitudeSpectrum::MagnitudeSpectrum()
    : s7::Element("MagnitudeSpectrum", false)
{
    // Register ports
    register_input(0, "frame", s7::SignalType::DOUBLE, nullptr);
}

MagnitudeSpectrum::~MagnitudeSpectrum() = default;

void MagnitudeSpectrum::bind_inputs(std::span<const s7::InputBinding> bindings) {
    // Type-erased binding for SpectralFrame
    // Not using the standard binding mechanism since we have complex data
    s7::Element::bind_inputs(bindings);
}

void MagnitudeSpectrum::step() {
    // In practice, this would receive spectral frame data
    // For now, it's a placeholder for magnitude computation
    output_frame_.bins.resize(input_frame_.bins.size());

    for (size_t i = 0; i < input_frame_.bins.size(); ++i) {
        // Compute magnitude
        float mag = std::abs(input_frame_.bins[i]);
        output_frame_.bins[i] = std::complex<float>(mag, 0.0f);
    }

    output_frame_.metadata = input_frame_.metadata;
}

// ============================================================================
// PowerSpectrumDB Implementation
// ============================================================================

PowerSpectrumDB::PowerSpectrumDB(float reference, float floor_db)
    : s7::Element("PowerSpectrumDB", false),
      reference_(reference),
      floor_db_(floor_db)
{
    register_input(0, "frame", s7::SignalType::DOUBLE, nullptr);
}

PowerSpectrumDB::~PowerSpectrumDB() = default;

void PowerSpectrumDB::bind_inputs(std::span<const s7::InputBinding> bindings) {
    s7::Element::bind_inputs(bindings);
}

void PowerSpectrumDB::step() {
    output_frame_.bins.resize(input_frame_.bins.size());

    for (size_t i = 0; i < input_frame_.bins.size(); ++i) {
        float mag = std::abs(input_frame_.bins[i]);

        float db;
        if (mag < 1e-10f) {
            db = floor_db_;
        } else {
            db = 20.0f * std::log10(mag / reference_);
            if (db < floor_db_) {
                db = floor_db_;
            }
        }

        output_frame_.bins[i] = std::complex<float>(db, 0.0f);
    }

    output_frame_.metadata = input_frame_.metadata;
}

} // namespace s7spectral

