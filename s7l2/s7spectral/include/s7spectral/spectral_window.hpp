#ifndef S7SPECTRAL_INCLUDE_S7SPECTRAL_SPECTRAL_WINDOW_HPP
#define S7SPECTRAL_INCLUDE_S7SPECTRAL_SPECTRAL_WINDOW_HPP

#include <vector>
#include <cstdint>
#include <cmath>

namespace s7spectral {

// Window function types for spectral analysis
enum class WindowType : uint8_t {
    RECTANGULAR,
    HANN,
    HAMMING,
    BLACKMAN,
    BLACKMAN_HARRIS,
    NUTTALL
};

// Windowing for FFT analysis
class SpectralWindow {
public:
    explicit SpectralWindow(size_t size, WindowType type = WindowType::HANN);

    // Apply window to signal (element-wise multiply)
    void apply(float* signal, size_t length) const;

    // Get window coefficients
    [[nodiscard]] const std::vector<float>& coefficients() const { return coefficients_; }

    // Get window normalization factor (for power preservation)
    [[nodiscard]] float normalization_factor() const { return normalization_; }

    // Get scallop loss (frequency bin amplitude response)
    [[nodiscard]] float scallop_loss_db() const;

    // Get coherent gain (for constant input)
    [[nodiscard]] float coherent_gain() const;

private:
    std::vector<float> coefficients_;
    float normalization_;
    WindowType type_;

    void compute_rectangular(size_t size);
    void compute_hann(size_t size);
    void compute_hamming(size_t size);
    void compute_blackman(size_t size);
    void compute_blackman_harris(size_t size);
    void compute_nuttall(size_t size);
};

} // namespace s7spectral

#endif // S7SPECTRAL_INCLUDE_S7SPECTRAL_SPECTRAL_WINDOW_HPP

