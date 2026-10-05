#include "s7spectral/fft_element.hpp"
#include "s7spectral/spectral_window.hpp"

#include "s7/element_metadata.hpp"
#include "s7/element_plugin.hpp"
#include "s7/element_registry.hpp"

#include <memory>
#include <optional>
#include <stdexcept>
#include <string>

// s7spectral is built out of tree against the installed S7 SDK
// (find_package(s7core)); it is the reference third-party plugin.

static const S7PluginInfo plugin_info{
    .api_version = S7_PLUGIN_API_VERSION,
    .element_count = 1,
    .plugin_name = "s7spectral",
    .plugin_version = "1.0.0",
    .author = "S7 Project",
};

static const char plugin_manifest_json[] = R"json(
{
  "api_version": 1,
  "plugin_name": "s7spectral",
  "plugin_version": "1.0.0",
  "author": "S7 Project",
  "description": "Spectral analysis elements (FFTW); reference out-of-tree plugin.",
  "element_count": 1,
  "elements": ["WindowedFFT"]
}
)json";

extern "C" {

const S7PluginInfo* s7_plugin_query()
{
    return &plugin_info;
}

const char* s7_plugin_manifest_json()
{
    return plugin_manifest_json;
}

} // extern "C"

namespace {

s7::ElementMetadata windowed_fft_metadata()
{
    s7::ElementMetadata meta;
    meta.type_name = "WindowedFFT";
    meta.display_name = "Windowed FFT";
    meta.description = "Sliding windowed FFT: dominant frequency and its amplitude";
    meta.category = "spectral";
    meta.version = "1.0.0";
    meta.tags = {"fft", "spectral", "stateful", "fftw3"};

    meta.input_ports.push_back({"in", "Input signal", s7::SignalType::F64, false});
    meta.output_ports.push_back({"peak_frequency_hz", "Dominant frequency (Hz)", s7::SignalType::F64, false});
    meta.output_ports.push_back({"peak_amplitude", "Amplitude of the dominant component", s7::SignalType::F64, false});

    meta.parameters = {
        {"fft_size", "int", "FFT window size (power of 2)", 1024, 4, 65536, {}, false},
        {"hop_size", "int", "Samples between transforms", 512, 1, 65536, {}, false},
        {"sample_rate_hz", "real", "Sample rate in Hz", 44100.0, 1e-3, 1e12, {}, false},
        {"window_type", "enum", "Window function", "HANN", std::nullopt, std::nullopt,
         {"RECTANGULAR", "HANN", "HAMMING", "BLACKMAN", "BLACKMAN_HARRIS", "NUTTALL"}, false},
    };

    meta.visual = {"box", "#9C27B0", 120, 50, "waveform"};
    return meta;
}

s7spectral::WindowType window_type_from_name(const std::string& name)
{
    using s7spectral::WindowType;
    if (name == "RECTANGULAR") return WindowType::RECTANGULAR;
    if (name == "HANN") return WindowType::HANN;
    if (name == "HAMMING") return WindowType::HAMMING;
    if (name == "BLACKMAN") return WindowType::BLACKMAN;
    if (name == "BLACKMAN_HARRIS") return WindowType::BLACKMAN_HARRIS;
    if (name == "NUTTALL") return WindowType::NUTTALL;
    throw std::invalid_argument("WindowedFFT: unknown window_type '" + name + "'.");
}

} // namespace

extern "C" {

void s7_register_elements(void* registry_ptr)
{
    auto* registry = static_cast<s7::ElementRegistry*>(registry_ptr);
    registry->register_element(s7::ElementDescriptor(
        "WindowedFFT",
        [](const s7::ElementCreationContext& ctx) {
            const auto fft_size = ctx.has_param("fft_size") ? ctx.get_int("fft_size") : 1024;
            const auto hop_size = ctx.has_param("hop_size") ? ctx.get_int("hop_size") : 512;
            if (fft_size <= 0 || hop_size <= 0) {
                throw std::invalid_argument("WindowedFFT: fft_size and hop_size must be positive.");
            }
            return std::make_unique<s7spectral::WindowedFFT>(
                static_cast<std::uint32_t>(fft_size),
                static_cast<std::uint32_t>(hop_size),
                ctx.has_param("sample_rate_hz") ? ctx.get_real("sample_rate_hz") : 44100.0,
                window_type_from_name(ctx.has_param("window_type") ? ctx.get_string("window_type") : "HANN"));
        },
        windowed_fft_metadata()));
}

} // extern "C"
