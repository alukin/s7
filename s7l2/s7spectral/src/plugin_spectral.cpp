// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: MIT-0

#include "s7spectral/audio_block_psd.hpp"
#include "s7spectral/fft_element.hpp"
#include "s7spectral/fourier_element.hpp"
#include "s7spectral/spectral_window.hpp"
#include "s7spectral/vector_fft.hpp"
#include "s7spectral/vector_psd.hpp"

#include "s7/metadata_json.hpp"
#include "spectral_metadata.hpp"
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
    .element_count = 5,
    .plugin_name = "s7spectral",
    .plugin_version = "1.0.0",
    .author = "S7 Project",
};

static const char plugin_manifest_json[] = R"json(
{
  "plugin_name": "s7spectral",
  "plugin_version": "1.0.0",
  "author": "S7 Project",
  "description": "Spectral analysis elements; reference out-of-tree plugin.",
  "element_count": 5,
  "elements": ["WindowedFFT", "FourierCoefficient", "VectorFFT", "VectorPSD", "AudioBlockPSD"]
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
    static const s7::ElementMetadataLibrary metadata(s7::resources::spectral_metadata);
    if (!registry->find("FourierCoefficient")) registry->register_element(s7::ElementDescriptor(
        "FourierCoefficient", [](const s7::ElementCreationContext& ctx) {
            return std::make_unique<s7spectral::FourierCoefficient>(ctx.get_real("frequency_hz"),ctx.get_real("window_time"),ctx.get_real("hop_time"));
        }, metadata.at("FourierCoefficient")));
    if (!registry->find("VectorFFT")) registry->register_element(s7::ElementDescriptor(
        "VectorFFT", [](const s7::ElementCreationContext& ctx) {
            return std::make_unique<s7spectral::VectorFFT>(
                static_cast<std::size_t>(ctx.get_int("components")), ctx.params.at("inverse").as_bool(),
                static_cast<std::size_t>(ctx.get_int("execution_slots")));
        }, metadata.at("VectorFFT"), true));
    if (!registry->find("AudioBlockPSD")) registry->register_element(s7::ElementDescriptor(
        "AudioBlockPSD", [](const s7::ElementCreationContext& ctx) {
            return std::make_unique<s7spectral::AudioBlockPSD>(
                static_cast<std::size_t>(ctx.get_int("channels")), static_cast<std::size_t>(ctx.get_int("components")));
        }, metadata.at("AudioBlockPSD"), true));
    if (!registry->find("VectorPSD")) registry->register_element(s7::ElementDescriptor(
        "VectorPSD", [](const s7::ElementCreationContext& ctx) {
            const auto sample_rate = ctx.get_real("sample_rate_hz");
            const auto rate = sample_rate > 0 ? std::optional{s7::Rate::from_hz(sample_rate)} : std::nullopt;
            const auto units = ctx.get_string("units") == "per_hz" ? s7::SpectrumUnits::PowerPerHz
                                                                  : s7::SpectrumUnits::PowerPerBin;
            return std::make_unique<s7spectral::VectorPSD>(
                static_cast<std::size_t>(ctx.get_int("components")), units, rate,
                static_cast<std::size_t>(ctx.get_int("execution_slots")));
        }, metadata.at("VectorPSD"), true));
    if (!registry->find("WindowedFFT")) registry->register_element(s7::ElementDescriptor(
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
        metadata.at("WindowedFFT")));
}

} // extern "C"
