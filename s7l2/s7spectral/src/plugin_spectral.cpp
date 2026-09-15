#include "s7spectral/fft_element.hpp"
#include "s7spectral/spectral_window.hpp"
#include "s7/element_registry.hpp"
#include "s7/element_plugin.hpp"

#include <memory>

// ============================================================================
// Plugin Information
// ============================================================================

static const S7PluginInfo plugin_info{
    .api_version = S7_PLUGIN_API_VERSION,
    .element_count = 3,
    .plugin_name = "s7spectral",
    .plugin_version = "1.0.0",
    .author = "S7 Project"
};

extern "C" {

const S7PluginInfo* s7_plugin_query() {
    return &plugin_info;
}

} // extern "C"

// ============================================================================
// Element Metadata and Registration
// ============================================================================

namespace {

// WindowedFFT metadata
s7::ElementMetadata create_windowed_fft_metadata() {
    s7::ElementMetadata meta;
    meta.type_name = "WindowedFFT";
    meta.display_name = "Windowed FFT";
    meta.description = "Real-to-complex FFT with sliding window for spectral analysis";
    meta.category = "spectral";
    meta.version = "1.0.0";
    meta.tags = {"fft", "spectral", "waterfall", "stateful", "fftw3"};

    meta.input_ports.push_back({
        "in", "Input signal", s7::SignalType::DOUBLE, false
    });

    // Note: Output is SpectralFrame, not standard signal type
    meta.output_ports.push_back({
        "spectrum", "Spectral frame (complex FFT bins)", s7::SignalType::DOUBLE, false
    });

    meta.parameters = {
        {"fft_size", "int", "FFT window size (power of 2)", 1024, 64, 65536, true},
        {"hop_size", "int", "Samples between windows", 512, 1, 65536, true},
        {"sample_rate_hz", "real", "Sample rate in Hz", 44100.0, 1.0, 1e6, true},
        {"window_type", "enum", "Window function", "HANN",
         {"RECTANGULAR", "HANN", "HAMMING", "BLACKMAN", "BLACKMAN_HARRIS", "NUTTALL"},
         true}
    };

    meta.visual = {"box", "#9C27B0", 120, 50, "waveform"};
    return meta;
}

// MagnitudeSpectrum metadata
s7::ElementMetadata create_magnitude_spectrum_metadata() {
    s7::ElementMetadata meta;
    meta.type_name = "MagnitudeSpectrum";
    meta.display_name = "Magnitude Spectrum";
    meta.description = "Extract magnitude from complex FFT bins";
    meta.category = "spectral";
    meta.version = "1.0.0";
    meta.tags = {"spectral", "magnitude", "conversion"};

    meta.input_ports.push_back({
        "frame", "Complex spectral frame", s7::SignalType::DOUBLE, false
    });

    meta.output_ports.push_back({
        "magnitude", "Magnitude values", s7::SignalType::DOUBLE, false
    });

    meta.visual = {"box", "#9C27B0", 100, 50, "waveform"};
    return meta;
}

// PowerSpectrumDB metadata
s7::ElementMetadata create_power_spectrum_db_metadata() {
    s7::ElementMetadata meta;
    meta.type_name = "PowerSpectrumDB";
    meta.display_name = "Power Spectrum (dB)";
    meta.description = "Convert magnitude spectrum to dB scale with floor";
    meta.category = "spectral";
    meta.version = "1.0.0";
    meta.tags = {"spectral", "dB", "power", "conversion"};

    meta.input_ports.push_back({
        "magnitude", "Magnitude spectrum", s7::SignalType::DOUBLE, false
    });

    meta.output_ports.push_back({
        "power_db", "Power in dB scale", s7::SignalType::DOUBLE, false
    });

    meta.parameters = {
        {"reference", "real", "Reference level for dB calculation", 1.0, 0.0001, 1000.0, false},
        {"floor_db", "real", "Minimum dB floor", -200.0, -300.0, 0.0, false}
    };

    meta.visual = {"box", "#9C27B0", 120, 50, "waveform"};
    return meta;
}

} // anonymous namespace

// ============================================================================
// Plugin Registration
// ============================================================================

extern "C" {

void s7_register_elements(void* registry_ptr) {
    auto* registry = static_cast<s7::ElementRegistry*>(registry_ptr);

    // Register WindowedFFT
    registry->register_element(s7::ElementDescriptor(
        "WindowedFFT",
        [](const s7::ElementCreationContext& ctx) {
            uint32_t fft_size = static_cast<uint32_t>(ctx.get_int("fft_size"));
            uint32_t hop_size = static_cast<uint32_t>(ctx.get_int("hop_size"));
            float sample_rate_hz = static_cast<float>(ctx.get_real("sample_rate_hz"));

            std::string window_name = ctx.get_string("window_type");
            s7spectral::WindowType window_type = s7spectral::WindowType::HANN;

            if (window_name == "RECTANGULAR") {
                window_type = s7spectral::WindowType::RECTANGULAR;
            } else if (window_name == "HAMMING") {
                window_type = s7spectral::WindowType::HAMMING;
            } else if (window_name == "BLACKMAN") {
                window_type = s7spectral::WindowType::BLACKMAN;
            } else if (window_name == "BLACKMAN_HARRIS") {
                window_type = s7spectral::WindowType::BLACKMAN_HARRIS;
            } else if (window_name == "NUTTALL") {
                window_type = s7spectral::WindowType::NUTTALL;
            }

            return std::make_unique<s7spectral::WindowedFFT>(
                fft_size, hop_size, sample_rate_hz, window_type
            );
        },
        create_windowed_fft_metadata()
    ));

    // Register MagnitudeSpectrum
    registry->register_element(s7::ElementDescriptor(
        "MagnitudeSpectrum",
        [](const s7::ElementCreationContext& ctx) {
            return std::make_unique<s7spectral::MagnitudeSpectrum>();
        },
        create_magnitude_spectrum_metadata()
    ));

    // Register PowerSpectrumDB
    registry->register_element(s7::ElementDescriptor(
        "PowerSpectrumDB",
        [](const s7::ElementCreationContext& ctx) {
            float reference = static_cast<float>(ctx.get_real("reference"));
            float floor_db = static_cast<float>(ctx.get_real("floor_db"));
            return std::make_unique<s7spectral::PowerSpectrumDB>(reference, floor_db);
        },
        create_power_spectrum_db_metadata()
    ));
}

} // extern "C"

